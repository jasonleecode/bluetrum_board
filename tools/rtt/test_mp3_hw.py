#!/usr/bin/env python3
"""Build an isolated RTT AUDEC probe or compare its UART PCM with FFmpeg.

Never edits the production configuration or opens a serial port. The probe
replays the CPU-side result of eight MP3 frames; it is not an MP3 player.
"""
import argparse
import array
import hashlib
import json
import math
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import zlib

ROOT = Path(__file__).resolve().parents[2]
FIXTURE = Path(__file__).resolve().parent / 'tests/mp3'
COUNT = 16


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def header():
    manifest = json.loads((FIXTURE / 'manifest.json').read_text())
    for name in ('tone.mp3', 'vectors.zlib'):
        assert sha(FIXTURE / name) == manifest['sha256'][name]
    data = zlib.decompress((FIXTURE / 'vectors.zlib').read_bytes())
    stride = 8 + 0x900 + 0x9c
    assert len(data) == COUNT * stride
    words, rows = [], []
    offsets = list(range(0x1000, 0x1900, 4)) + list(range(0x2b00, 0x2b9c, 4))
    for start in range(0, len(data), stride):
        control, kick = struct.unpack_from('<II', data, start)
        first = len(words)
        values = struct.unpack_from('<615I', data, start + 8)
        words.extend((o, v) for o, v in zip(offsets, values) if v)
        rows.append((control, kick, first, len(words) - first))
    text = ('#include <stdint.h>\n'
            'struct mp3_word {uint16_t offset; uint32_t value;} __attribute__((packed));\n'
            'struct mp3_vector {uint32_t control,kick; unsigned first,count;};\n'
            f'#define MP3_VECTOR_COUNT {COUNT}\n'
            'static const struct mp3_word mp3_words[]={\n')
    text += ''.join('{0x%x,0x%08x},\n' % w for w in words)
    text += '};\nstatic const struct mp3_vector mp3_vectors[]={\n'
    text += ''.join('{0x%08x,0x%08x,%d,%d},\n' % r for r in rows)
    return text + '};\n'


def build():
    work = Path(tempfile.mkdtemp(prefix='ab32-mp3-probe-'))
    print(work, flush=True)
    shutil.copytree(ROOT / 'rtt', work / 'rtt', ignore=shutil.ignore_patterns(
        'build', '.git', '*.o', '.sconsign.dblite', '__pycache__', 'nimble',
        'rtthread.elf', 'rtthread.bin', 'rtthread.map', 'rtthread.dcf'))
    (work / 'bare').mkdir()
    (work / 'bare/lib').symlink_to(ROOT / 'bare/lib', target_is_directory=True)
    shutil.copytree(ROOT / 'bare/drivers', work / 'bare/drivers',
                    ignore=shutil.ignore_patterns('*.o'))
    bsp = work / 'rtt/bsp'
    config = (bsp / 'rtconfig.h').read_text()
    # Preserve UART, keys, LED and RTC; no other audio/DMA or power owner.
    for key in ('BSP_USING_BTCTRL', 'BSP_USING_NIMBLE', 'BSP_USING_FMRX',
                'BSP_USING_AUDIO', 'BSP_USING_AUDIO_PLAY', 'RT_USING_AUDIO',
                'BSP_USING_USB_CDC', 'BSP_USING_TFCARD', 'RT_USING_DFS',
                'RT_USING_DFS_V1', 'RT_USING_DFS_ELMFAT', 'DFS_USING_POSIX',
                'DFS_USING_WORKDIR', 'BSP_USING_IRRX', 'BSP_USING_IRRX_HW',
                'BSP_USING_POWER'):
        config = re.sub(r'^#define\s+' + key + r'\b[^\n]*', '', config, flags=re.M)
    (bsp / 'rtconfig.h').write_text(config + '\n#define BSP_USING_AUDEC\n')
    shutil.copy2(FIXTURE / 'probe.c', bsp / 'app/mp3_hw.c')
    (bsp / 'app/mp3_vectors.h').write_text(header())
    env = os.environ.copy()
    env.setdefault('RTT_EXEC_PATH', str(ROOT / '.toolchains' /
        'riscv64-unknown-elf-gcc-10.1.0-2020.08.2-x86_64-linux-ubuntu14/bin'))
    with (work / 'build.log').open('w') as log:
        subprocess.run(['scons', '-j4'], cwd=bsp, env=env, stdout=log,
                       stderr=subprocess.STDOUT, check=True)
    linkmap = (bsp / 'rtthread.map').read_text()
    assert 'libcodecs.a' not in linkmap and 'libbtctrl.a' not in linkmap
    assert 'drv_audec.o' in linkmap
    assert re.search(r'\.audec_work\s+0x0*54000\s+0x3000', linkmap)
    dcf = work / 'probe.dcf'
    subprocess.run([str(ROOT / 'tools/linux/convert'), str(bsp / 'header.bin'),
                    str(bsp / 'rtthread.bin'), str(dcf)], check=True)
    print(f'Probe built: {dcf}\nRestore the integrated firmware after testing.')


def samples(data):
    result = array.array('h', data)
    if sys.byteorder != 'little':
        result.byteswap()
    return result


def compare(log, output, dcf):
    text = log.read_text(errors='replace')
    statuses = re.findall(r'GRANULE (\d+) status=([0-9a-f]+) con=([0-9a-f]+) '
                          r'ticks=(\d+) changed=(\d+) guards=(\d+)', text)
    assert len(statuses) == COUNT, 'missing hardware completion records'
    for index, (n, status, _, _, changed, guards) in enumerate(statuses):
        assert int(n) == index and int(status, 16) & 0x20
        assert int(changed) == 576 and int(guards) == 1, 'DMA incomplete or overrun'
    chunks = re.findall(r'PCM (\d+) 2304\n((?:[0-9a-f]{64}\n){72})', text)
    assert [int(n) for n, _ in chunks] == list(range(COUNT)), 'missing PCM data'
    assert 'MP3HW end rc=0 granules=16' in text
    pcm = b''.join(bytes.fromhex(block) for _, block in chunks)
    hardware = samples(pcm)
    reference_bytes = subprocess.check_output(['ffmpeg', '-hide_banner', '-loglevel',
        'error', '-i', str(FIXTURE / 'tone.mp3'), '-f', 's16le', '-acodec', 'pcm_s16le', '-'])
    reference = samples(reference_bytes)
    assert len(hardware) == len(reference) * 2 == 18432
    assert hardware[0::2] == hardware[1::2], 'mono must reach both output channels'
    x = hardware[0::2]
    errors = [a - b for a, b in zip(x, reference)]
    rms = math.sqrt(sum(e * e for e in errors) / len(errors))
    peak = max(map(abs, errors))
    assert max(map(abs, x)) > 3000, 'silent or truncated output'
    assert peak <= 2 and rms <= 0.5, 'PCM mismatch (no alignment or gain fitting allowed)'
    mx, my = sum(x) / len(x), sum(reference) / len(reference)
    covariance = sum((a - mx) * (b - my) for a, b in zip(x, reference))
    correlation = covariance / math.sqrt(sum((a-mx)**2 for a in x) *
                                        sum((b-my)**2 for b in reference))
    report = dict(result='pass', sample_rate=44100, mp3_frames=8, granules=COUNT,
        samples_per_channel=len(x), output_channels=2, pcm_peak_error=peak,
        pcm_rms_error=rms, correlation=correlation, all_guards_intact=True,
        wait_ticks=[int(s[3]) for s in statuses], uart_log_sha256=sha(log),
        hardware_pcm_sha256=hashlib.sha256(pcm).hexdigest(),
        reference_pcm_sha256=hashlib.sha256(reference_bytes).hexdigest(),
        probe_source_sha256=sha(FIXTURE / 'probe.c'),
        driver_source_sha256=sha(ROOT / 'rtt/bsp/libraries/hal_drivers/drv_audec.c'),
        vector_sha256=sha(FIXTURE / 'vectors.zlib'),
        scope='On-chip MP3 datapath only; CPU front-end precomputed on host; no file player or coexistence validation')
    if dcf:
        report['dcf_sha256'] = sha(dcf)
    if output:
        output.write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest='command', required=True)
    sub.add_parser('build')
    p = sub.add_parser('compare')
    p.add_argument('log', type=Path)
    p.add_argument('--output', type=Path)
    p.add_argument('--dcf', type=Path)
    args = parser.parse_args()
    if args.command == 'build':
        build()
    else:
        compare(args.log, args.output, args.dcf)
