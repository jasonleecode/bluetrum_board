#!/usr/bin/env python3
"""Audio wrapper/selection/isolated RV32 link checks; no boot or analog validation."""
import argparse
import itertools
from pathlib import Path
import shutil
import subprocess
import tempfile
import sys
sys.dont_write_bytecode = True
from test_usb_integration import exported_functions
ROOT=Path(__file__).resolve().parents[2]
INC=ROOT/'bare/drivers/inc'
SRC=ROOT/'bare/drivers/src'
RECOVERED=('audio_recovered','sdadc_recovered')
def run(args,**kw): return subprocess.run([str(x) for x in args],check=True,**kw)
def main():
    ap=argparse.ArgumentParser(description=__doc__); ap.add_argument('--linker',default=shutil.which('ld.lld')); args=ap.parse_args()
    if not args.linker: ap.error('provide --linker')
    for selection in itertools.product(('vendor','recovered'),repeat=5):
        names=('SPI','FM','BT_SUPPORT','USB','AUDIO')
        output=run(['make','-s','-C',ROOT/'bare','info',*[f'{n}_BACKEND={v}' for n,v in zip(names,selection)]],capture_output=True,text=True).stdout
        for n in RECOVERED: assert (n+'.o' in output)==(selection[-1]=='recovered')
        path='build'+''.join('/recovered-'+n.lower().replace('_','-') for n,v in zip(names,selection) if v=='recovered')
        assert path+'/startup.o' in output
    bad=subprocess.run(['make','-s','-C',str(ROOT/'bare'),'info','AUDIO_BACKEND=bad'],capture_output=True,text=True)
    assert bad.returncode and 'AUDIO_BACKEND must be' in bad.stderr
    print('PASS 32 backend combinations and invalid audio selector')
    with tempfile.TemporaryDirectory(prefix='ab32-audio-integration-') as d:
        tmp=Path(d)
        for default in (False,True):
            run(['cc','-std=c11','-Wall','-Wextra','-Werror','-I',INC, *(['-DDEFAULT_PLATFORM'] if default else []),ROOT/'docs/lib-analysis/tests/audio_wrapper_test.c',SRC/'drv_adc.c',SRC/'drv_audio.c','-o',tmp/'wrapper'])
            run([tmp/'wrapper'])
        flags=['clang','--target=riscv32-unknown-elf','-march=rv32imac','-mabi=ilp32','-Os','-ffreestanding','-fno-builtin','-ffunction-sections','-fdata-sections','-Wall','-Wextra','-Werror','-I',INC]
        objects={}
        for name in ('drv_adc','drv_audio','dac_output','drv_usb_audio','vendor_shim',*RECOVERED):
            objects[name]=tmp/(name+'.o'); run([*flags,'-c',SRC/(name+'.c'),'-o',objects[name]])
        for name,member in zip(RECOVERED,('audio.o','sdadc.o')):
            original=tmp/member
            original.write_bytes(run(['ar','p',ROOT/'bare/lib/libhal.a',member],capture_output=True).stdout)
            assert exported_functions(original)==exported_functions(objects[name]),name
        print('PASS 18 audio/SDADC exported functions: names, bindings and code sections')
        for app in ('mic_read','audio_play'):
            if (ROOT/f'bare/app/{app}.c').exists(): run([*flags,'-fsyntax-only',ROOT/f'bare/app/{app}.c'])
        for recovered in (False,True):
            selected=['drv_adc','drv_audio','dac_output','drv_usb_audio','vendor_shim']+(list(RECOVERED) if recovered else [])
            elf=tmp/('recovered.elf' if recovered else 'vendor.elf')
            linked=run([args.linker,'-m','elf32lriscv','--no-relax','--gc-sections','--trace','-Ttext=0x200000','-e','drv_adc_init', '-u','drv_adc_start_dma','-u','drv_adc_isr','-u','drv_adc_stop','-u','drv_audio_init','-u','drv_audio_write_frames','-u','drv_audio_stop','-o',elf,*[objects[n] for n in selected],'--start-group',ROOT/'bare/lib/libhal.a',ROOT/'bare/lib/libbtctrl.a','--end-group'],capture_output=True,text=True)
            for member in ('audio.o','sdadc.o'): assert (f'libhal.a({member})' in linked.stdout)!=recovered
            symbols=run(['readelf','-Ws',elf],capture_output=True,text=True).stdout
            assert not [l for l in symbols.splitlines() if ' UND ' in l and l.split()[-1]!='UND']
            print('PASS isolated audio RV32 link:', 'recovered' if recovered else 'vendor')
if __name__=='__main__': main()
