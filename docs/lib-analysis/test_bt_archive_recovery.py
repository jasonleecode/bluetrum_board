#!/usr/bin/env python3
"""Audit every libbtctrl.a member against the source replacement ELF symbols.

No linker garbage collection: missing functions/data cannot hide behind unused
application paths. Empty members and the absolute ROM binding are explicit.
"""
import argparse
import hashlib
import io
import json
import shutil
import subprocess
import tempfile
from pathlib import Path
from elftools.elf.elffile import ELFFile

ROOT=Path(__file__).resolve().parents[2]
ARCHIVE=ROOT/'bare/lib/libbtctrl.a'
OUT=ROOT/'docs/lib-analysis/generated/recovery/bt-archive-coverage.json'
RENAMES={'bb':'core','bb_rf':'rf','hct':'hct_transport','hct_msg':'hct','hct_proc':'hct',
         'baseband_platform':'platform','platform_asm':'rom_binding'}
EMPTY={'co_list.o','ble_txrx.o'}


def symbols(data):
    elf=ELFFile(io.BytesIO(data))
    result={}
    for s in elf.get_section_by_name('.symtab').iter_symbols():
        if not s.name or s['st_shndx']=='SHN_UNDEF' or s['st_info']['bind']=='STB_LOCAL':continue
        section=elf.get_section(s['st_shndx']).name if isinstance(s['st_shndx'],int) else s['st_shndx']
        result[s.name]=dict(binding=s['st_info']['bind'],type=s['st_info']['type'],size=s['st_size'],section=section,value=s['st_value'])
    return elf,result


def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--gcc');args=parser.parse_args()
    compilers={'clang':['clang','--target=riscv32-unknown-elf']}
    if args.gcc:compilers['gcc']=[args.gcc,'-msave-restore']
    names=subprocess.check_output(['ar','t',str(ARCHIVE)],text=True).splitlines()
    assert len(names)==32
    report=dict(archive_sha256=hashlib.sha256(ARCHIVE.read_bytes()).hexdigest(),members=[])
    with tempfile.TemporaryDirectory(prefix='bt-archive-audit-') as directory:
        tmp=Path(directory);built={}
        for name in names:
            original=subprocess.check_output(['ar','p',str(ARCHIVE),name]);elf,expected=symbols(original)
            functions=[s.name for s in elf.get_section_by_name('.symtab').iter_symbols() if s['st_info']['type']=='STT_FUNC']
            entry=dict(member=name,sha256=hashlib.sha256(original).hexdigest(),functions=len(functions),exports=expected)
            if name in EMPTY:
                assert not expected and not functions
                assert not any(s['sh_size'] for s in elf.iter_sections() if s['sh_flags']&2)
                entry['status']='empty member; no replacement needed'
            else:
                stem=RENAMES.get(name[:-2],name[:-2]);source=ROOT/f'bare/drivers/src/bt_{stem}_recovered.c'
                entry.update(source=str(source.relative_to(ROOT)),source_sha256=hashlib.sha256(source.read_bytes()).hexdigest(),status='source replacement',compilers=list(compilers))
                for compiler,command in compilers.items():
                    key=(compiler,stem)
                    if key not in built:
                        path=tmp/f'{compiler}-{stem}.o'
                        subprocess.run([*command,'-march=rv32imc','-mabi=ilp32','-ffreestanding','-fno-builtin','-Os','-Wall','-Wextra','-Werror','-I',str(ROOT/'bare/drivers/inc'),'-c',str(source),'-o',str(path)],check=True)
                        built[key]=symbols(path.read_bytes())[1]
                    actual=built[key]
                    for symbol,old in expected.items():
                        assert symbol in actual,(compiler,name,'missing',symbol)
                        new=actual[symbol]
                        for field in ('binding','type','section'):
                            assert old[field]==new[field],(compiler,name,symbol,field,old,new)
                        if old['type']!='STT_FUNC':
                            assert old['size']==new['size'],(compiler,name,symbol,'size',old,new)
                            # Code-relative local offsets differ; public data offsets and ROM addresses do not.
                            assert old['value']==new['value'],(compiler,name,symbol,'offset',old,new)
            report['members'].append(entry)
    report['summary']=dict(members=len(names),empty_members=len(EMPTY),functions=sum(x['functions'] for x in report['members']),
        source_files=len({x['source'] for x in report['members'] if 'source' in x}))
    OUT.write_text(json.dumps(report,indent=2)+'\n')
    print('PASS controller archive inventory:',report['summary'])


if __name__=='__main__':main()
