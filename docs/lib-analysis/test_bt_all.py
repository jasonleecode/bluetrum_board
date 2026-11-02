#!/usr/bin/env python3
"""Run the complete controller source-recovery regression suite.

Tests execute offline; they do not flash a board or claim radio functionality.
Unicorn and pyelftools must be installed for the selected Python interpreter.
"""
import argparse
import json
import shutil
import subprocess
import sys
from pathlib import Path

ROOT=Path(__file__).resolve().parents[2]
TESTS=Path(__file__).resolve().parent


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--gcc',required=True)
    parser.add_argument('--linker',default=shutil.which('ld.lld'))
    args=parser.parse_args()
    if not args.linker:parser.error('provide --linker /path/to/ld.lld')
    gcc=str(Path(args.gcc).resolve()) if '/' in args.gcc else args.gcc
    commands=[('test_bt_archive_recovery.py',['--gcc',gcc])]
    for name in ('hct','platform','nanos'):
        commands.append((f'test_bt_{name}_recovery.py',['--linker',args.linker]))
    common=['--linker',args.linker,'--gcc',gcc]
    for name in ('core','ext_init','complex','rf','support'):
        commands.append((f'test_bt_{name}_recovery.py',common))
    commands += [('test_bt_rf_recovery.py',common+['--real-math']),
                 ('test_bt_hct_transport.py',common+['--recovered']),
                 ('test_bt_integration.py',common)]
    results=[]
    for name,options in commands:
        print('RUN',name,*options,flush=True)
        subprocess.run([sys.executable,str(TESTS/name),*options],cwd=ROOT,check=True)
        results.append(dict(test=name,options=options,result='pass'))
    (TESTS/'generated/recovery/bt-suite.json').write_text(json.dumps(dict(checks=results),indent=2)+'\n')
    print(f'PASS all {len(commands)} controller recovery checks')


if __name__=='__main__':main()
