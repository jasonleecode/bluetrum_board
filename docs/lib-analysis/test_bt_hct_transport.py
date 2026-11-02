#!/usr/bin/env python3
"""Compare original and recovered hct.o transport under deterministic ROM stubs.

These checks validate transport behavior, not hardware or ROM internals.
"""
import argparse
import hashlib
import io
import json
import shutil
import struct
import subprocess
import tempfile
from pathlib import Path

from unicorn import Uc, UC_ARCH_RISCV, UC_MODE_RISCV32, UC_HOOK_CODE, UC_HOOK_MEM_READ, UC_HOOK_MEM_WRITE
from unicorn.riscv_const import *
from elftools.elf.elffile import ELFFile

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'docs/lib-analysis/generated/recovery'
INPUT, CLASSIC, BLE, CONNECTION, NODE, ALLOC, EVENT, STOP = (
    0x50000, 0x51000, 0x52000, 0x53000, 0x54000, 0x55000, 0x56000, 0x70000)
BLE_POINTER = 0x58000
ROM = {0x843a0: 'allocate', 0x843a4: 'send', 0x843a8: 'empty_message',
       0x84328: 'free', 0x84598: 'classic_buffer', 0x843d0: 'queue_pop',
       0x8434c: 'scheduler_ack', 0x845a0: 'classic_free',
       0x84368:'register',0x84364:'state',0x843c0:'init_list',0x84340:'scheduler_register'}
ARGREGS = (UC_RISCV_REG_A0, UC_RISCV_REG_A1, UC_RISCV_REG_A2, UC_RISCV_REG_A3)
SAVED = (UC_RISCV_REG_S0, UC_RISCV_REG_S1, UC_RISCV_REG_S2, UC_RISCV_REG_S3,
         UC_RISCV_REG_S4, UC_RISCV_REG_S5, UC_RISCV_REG_S6, UC_RISCV_REG_S7,
         UC_RISCV_REG_S8, UC_RISCV_REG_S9, UC_RISCV_REG_S10, UC_RISCV_REG_S11)


def archive_member(archive, name):
    return subprocess.check_output(['ar', 'p', str(ROOT / 'bare/lib' / archive), name])


def build(linker, tmp, compiler=None):
    original = archive_member('libbtctrl.a', 'hct.o')
    obj, save = tmp / 'hct.o', tmp / 'save.o'
    obj.write_bytes(original)
    if compiler:
        subprocess.run([*compiler,'-march=rv32imc','-mabi=ilp32','-ffreestanding','-fno-builtin','-Os','-Wall','-Wextra','-Werror','-I',str(ROOT/'bare/drivers/inc'),'-c',str(ROOT/'bare/drivers/src/bt_hct_transport_recovered.c'),'-o',str(obj)],check=True)
    save.write_bytes(archive_member('libhal.a', 'save-restore.o'))
    symbols = ELFFile(io.BytesIO(original)).get_section_by_name('.symtab')
    names = sorted(s.name for s in symbols.iter_symbols() if s['st_shndx'] == 'SHN_UNDEF'
                   and s.name and not s.name.startswith('__riscv_'))
    externals = {name: 0x60000 + 16*i for i, name in enumerate(names)}
    externals['ble_con_keep_inside_cell'] = BLE_POINTER
    script = tmp / 'link.ld'
    script.write_text('SECTIONS { . = 0x10000; .text : { *(.text*) *(.vector*) } '
                      '. = 0x20000; .rodata : { *(.rodata*) } '
                      '. = 0x30000; .btmem.bb.hcic : { *(.btmem.bb.hcic) } .data : { *(.data*) *(.sdata*) } '
                      '.bss : { *(.bss*) *(.sbss*) } __global_pointer$ = 0x30800; }')
    elf = tmp / 'hct.elf'
    subprocess.run([linker, '-m', 'elf32lriscv', '--no-relax', '-T', str(script),
                    '-e', 'hct_send_command', '-o', str(elf), str(obj), str(save),
                    *[f'--defsym={name}={address}' for name, address in externals.items()]], check=True)
    disassembler = shutil.which('llvm-objdump-10') or shutil.which('llvm-objdump')
    if disassembler and not compiler:
        OUT.mkdir(parents=True, exist_ok=True)
        listing = subprocess.check_output([disassembler, '-dr', str(obj)])
        (OUT / 'bt-hct-transport.llvm.txt').write_bytes(listing.replace(str(obj).encode(), b'libbtctrl.a:hct.o'))
    return elf.read_bytes(), externals


class Machine:
    covered=set()
    def __init__(self, binary, externals, kind=1, connection=1, allocated=True, credits=5, features=255):
        self.uc = Uc(UC_ARCH_RISCV, UC_MODE_RISCV32)
        self.uc.mem_map(0, 0x100000)
        elf = ELFFile(io.BytesIO(binary))
        for section in elf.iter_sections():
            if section['sh_flags'] & 2 and section['sh_type'] != 'SHT_NOBITS':
                self.uc.mem_write(section['sh_addr'], section.data())
        self.symbols = {s.name: s['st_value'] for s in elf.get_section_by_name('.symtab').iter_symbols()}
        self.state = self.symbols['lm_proc_pass_to_knee']
        self.uc.mem_write(self.state, bytes(92))
        self.uc.mem_write(self.state + 2, bytes([1, credits]))
        self.uc.mem_write(INPUT, bytes((i % 251 + 1) for i in range(2048)))
        self.uc.mem_write(BLE_POINTER, struct.pack('<I', CONNECTION))
        self.uc.mem_write(CONNECTION + 12, struct.pack('<I', BLE))
        self.uc.mem_write(NODE + 4, b'\0\0')
        self.names = {address: name for name, address in externals.items()}
        self.kind, self.connection, self.allocated = kind, connection, allocated
        self.features=features
        self.trace = []
        for address in set(self.names) | set(ROM):
            if address == BLE_POINTER:
                continue
            self.uc.mem_write(address, b'\x67\x80\x00\x00')
            self.uc.hook_add(UC_HOOK_CODE, self.external, begin=address, end=address)
        self.uc.mem_write(0x540,struct.pack('<I',0x12345679))
        self.uc.hook_add(UC_HOOK_MEM_READ|UC_HOOK_MEM_WRITE,self.mmio,begin=0x540,end=0x543)
        for n in range(13):
            if '__riscv_restore_' + str(n) not in self.symbols:continue
            address = self.symbols['__riscv_restore_' + str(n)]
            self.uc.hook_add(UC_HOOK_CODE, self.restore, user_data=n, begin=address, end=address)

    def mmio(self,uc,access,address,size,value,_):
        self.trace.append(['mmio',access,address,size,value if access==17 else struct.unpack('<I',uc.mem_read(address,4))[0]])

    def restore(self, uc, address, size, n):
        sp = uc.reg_read(UC_RISCV_REG_SP)
        values = struct.unpack('<' + 'I' * (n + 1), uc.mem_read(sp, 4 * (n + 1)))
        for reg, value in zip(reversed(SAVED[:n]), values):
            uc.reg_write(reg, value)
        uc.reg_write(UC_RISCV_REG_SP, sp + 4 * (n + 1))
        uc.reg_write(UC_RISCV_REG_RA, values[-1])
        uc.reg_write(UC_RISCV_REG_PC, values[-1])

    def external(self, uc, address, size, _):
        a, b, c, d = [uc.reg_read(r) for r in ARGREGS]
        name = ROM.get(address) or self.names[address]
        result = 0
        if name=='ext_util_read_without_raw':
            self.trace.append([name,'stack']);uc.mem_write(a,bytes([self.features])*8);result=a
        elif name in ('register','state','scheduler_register'):
            if name=='scheduler_register':b=next(n for n,v in self.symbols.items() if v==b and n=='hct_pass_within_head')
            self.trace.append([name,a,b])
        elif name=='init_list':self.trace.append([name,a]);uc.mem_write(a,bytes(8))
        elif name == 'bb_utils_keep_of_age':
            self.trace.append([name, a])
            result = self.kind
        elif name == 'bb_utils_go_by_promise':
            self.trace.append([name, a])
            result = self.connection
        elif name == 'classic_buffer':
            self.trace.append([name])
            result = CLASSIC if self.allocated else 0
        elif name == 'queue_pop':
            self.trace.append([name, a])
            if a == CONNECTION:
                result = NODE if self.allocated else 0
            else:
                result = struct.unpack('<I', uc.mem_read(a, 4))[0]
                uc.mem_write(a, bytes(4))
        elif name == 'allocate':
            self.trace.append([name, a, b, c, d])
            result = ALLOC
        elif name == 'memcpy':
            self.trace.append([name, a, b, c])
            uc.mem_write(a, bytes(uc.mem_read(b, c)))
            result = a
        elif name == 'memset':
            self.trace.append([name, a, b, c])
            uc.mem_write(a, bytes([b & 255]) * c)
            result = a
        elif name == 'my_printf':
            fmt = bytes(uc.mem_read(a, 80)).split(b'\0')[0].decode()
            self.trace.append([name, fmt])
        elif name == 'hci_host_recv_packet':
            self.trace.append([name, a, b, bytes(uc.mem_read(a, b)).hex()])
        elif name in ('send', 'hct_get_on_birth', 'hct_find_with_problem'):
            self.trace.append([name, a, bytes(uc.mem_read(a, 16)).hex()])
        elif name == 'empty_message':
            self.trace.append([name, a, b, c])
        elif name in ('free', 'classic_free', 'bb_fetch_in_event', 'bb_get_without_card',
                      'bb_find_to_afternoon', 'scheduler_ack', 'll_sell_under_data'):
            self.trace.append([name, a])
        else:
            raise AssertionError(('unmodeled call', name, [a, b, c, d]))
        uc.reg_write(UC_RISCV_REG_A0, result)
        uc.reg_write(UC_RISCV_REG_PC, uc.reg_read(UC_RISCV_REG_RA))

    def call(self, name, *args):
        self.covered.add(name)
        self.uc.reg_write(UC_RISCV_REG_SP, 0x90000)
        self.uc.reg_write(UC_RISCV_REG_RA, STOP)
        for reg, value in zip(ARGREGS, args):
            self.uc.reg_write(reg, value&0xffffffff)
        self.uc.emu_start(self.symbols[name], STOP, count=100000)
        assert self.uc.reg_read(UC_RISCV_REG_PC) == STOP, (name, 'did not return')
        assert self.uc.reg_read(UC_RISCV_REG_SP) == 0x90000, (name, 'stack imbalance')
        return self.uc.reg_read(UC_RISCV_REG_A0)


class MirroredMemory:
    def __init__(self, primary, others):self.primary,self.others=primary,others
    def mem_write(self,address,data):
        self.primary.mem_write(address,data)
        for uc in self.others:uc.mem_write(address,data)
    def __getattr__(self,name):return getattr(self.primary,name)


class DifferentialMachine(Machine):
    binaries={}
    def __init__(self,binary,externals,**kwargs):
        super().__init__(binary,externals,**kwargs)
        self.others={n:Machine(b,externals,**kwargs) for n,b in self.binaries.items()}
        assert all(m.state==self.state for m in self.others.values())
        self.uc=MirroredMemory(self.uc,[m.uc for m in self.others.values()])
    def call(self,name,*args):
        result=super().call(name,*args)
        for compiler,m in self.others.items():
            try:actual=m.call(name,*args)
            except Exception:
                print(compiler,name,args,hex(m.uc.reg_read(UC_RISCV_REG_PC)),m.trace[-12:],flush=True);raise
            if name in ('hct_acl_segment','hct_leave_in_bother','lm_proc_save_about_hearing','lm_proc_save_about_ruin','hct_take_into_brain','hct_reserve_within_physics','hct_write_at_month'):
                assert actual==result,(compiler,name,result,actual)
            assert m.trace==self.trace,(compiler,name,self.trace,m.trace)
            for a,n in ((self.state,92),(INPUT,0x7000),(0x540,4)):
                assert bytes(m.uc.mem_read(a,n))==bytes(self.uc.mem_read(a,n)),(compiler,name,hex(a),'memory')
        return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--recovered',action='store_true')
    parser.add_argument('--gcc')
    parser.add_argument('--linker', default=shutil.which('ld.lld'))
    args = parser.parse_args()
    if not args.linker:
        parser.error('Supply --linker /path/to/ld.lld')
    with tempfile.TemporaryDirectory(prefix='ab32-bt-transport-') as directory:
        binary, externals = build(args.linker, Path(directory))
        if args.recovered:
            DifferentialMachine.binaries['clang']=build(args.linker,Path(directory),['clang','--target=riscv32-unknown-elf'])[0]
            if args.gcc:DifferentialMachine.binaries['gcc']=build(args.linker,Path(directory),[args.gcc,'-msave-restore'])[0]
    machine_type=DifferentialMachine if args.recovered else Machine
    report = {'original_object_sha256': hashlib.sha256(archive_member('libbtctrl.a', 'hct.o')).hexdigest(),
              'model': 'Original hct.o RV32 execution. Controller lookups, allocator, scheduler, memcpy and ROM services stubbed; vendor restore helper ABI shims. No RF or live scheduler validation.',
              'cases': []}

    def record(name, machine, **results):
        report['cases'].append({'name': name, **results, 'trace': machine.trace, 'result': 'pass'})

    for kind in (1, 4):
        for flags in (0, 1, 2, 3, 4, 15):
            capacity = 27 if kind == 4 and flags <= 3 else 679
            for length in (0, capacity, capacity + 1, 1024):
                m = machine_type(binary, externals, kind=kind)
                result = m.call('hct_acl_segment', 0xabc1, flags, length, INPUT)
                assert result == 1
                copies = [t for t in m.trace if t[0] == 'memcpy']
                assert len(copies) == 1 and copies[0][3] == min(length, capacity)
                assert bytes(m.uc.mem_read(copies[0][1], min(length, capacity))) == bytes(m.uc.mem_read(INPUT, min(length, capacity)))
                assert struct.unpack('<H', m.uc.mem_read(m.state + 32, 2))[0] == 0xbc1
                if kind == 4 and flags <= 3:
                    queued_length = struct.unpack('<H', m.uc.mem_read(ALLOC + 4, 2))[0]
                    queued_flags = m.uc.mem_read(ALLOC + 2, 1)[0]
                else:
                    queued_length = struct.unpack('<H', m.uc.mem_read(ALLOC + 2, 2))[0]
                    queued_flags = m.uc.mem_read(ALLOC + 8, 1)[0]
                assert queued_length == min(length, capacity) and queued_flags == flags
                record(f'ACL type {kind} flags {flags} length {length}', m, returned=result,
                       copied=copies[0][3], queued_length=queued_length, queued_flags=queued_flags)

    for kind in (1, 4):
        m = machine_type(binary, externals, kind=kind, allocated=False)
        result = m.call('hct_acl_segment', 1, 2, 10, INPUT)
        assert result == 1 and not any(t[0] == 'memcpy' for t in m.trace)
        # The only outgoing message is a buffer-exhaustion event, not ACL data.
        allocations = [t for t in m.trace if t[0] == 'allocate']
        assert len(allocations) == 1 and allocations[0][1] == 0x1467
        record(f'ACL allocation exhausted type {kind}', m, returned=result, copied=0)
    for kind, connection in ((3, 1), (1, 0), (4, 0)):
        m = machine_type(binary, externals, kind=kind, connection=connection)
        result = m.call('hct_acl_segment', 1, 2, 10, INPUT)
        assert result == 0 and not any(t[0] in ('allocate', 'memcpy') for t in m.trace)
        record(f'ACL invalid type {kind} connection {connection}', m, returned=result)

    for credits in (0, 1):
        for opcode, length in ((0x0c03, 0), (0x2006, 15), (0x0c35, 5), (0xffff, 32)):
            m = machine_type(binary, externals, credits=credits)
            m.call('hct_send_command', opcode, length, INPUT)
            copied = [t for t in m.trace if t[0] == 'memcpy']
            expected_copy = min(length, 16) if opcode == 0xffff else length
            assert (copied[0][3] if copied else 0) == expected_copy
            if credits == 0 and opcode != 0x0c35:
                assert not any(t[0] in ('send', 'hct_get_on_birth', 'hct_find_with_problem', 'empty_message') for t in m.trace)
                if opcode == 0x2006:
                    assert ['free', ALLOC - 16] in m.trace
            record(f'command {opcode:04x} credits {credits}', m,
                   credits_after=m.uc.mem_read(m.state + 3, 1)[0])

    command_aliases = []
    for opcode in (0x2006, 0x2106, 0x2206, 0x2306):
        m = machine_type(binary, externals)
        m.call('hct_send_command', opcode, 15, INPUT)
        command_aliases.append([t for t in m.trace if t[0] == 'allocate'][0][1:])
        record(f'command OCF alias {opcode:04x}', m)
    assert command_aliases == [command_aliases[0]] * 4

    m = machine_type(binary, externals)
    m.uc.mem_write(m.state + 44, struct.pack('<I', EVENT))
    m.uc.mem_write(EVENT + 5, b'\0\x03\0')
    m.uc.mem_write(EVENT + 16, b'\x0e\x01\0')
    m.call('hct_take_under_cook')
    callbacks = [t for t in m.trace if t[0] == 'hci_host_recv_packet']
    assert callbacks == [['hci_host_recv_packet', EVENT + 15, 4, '040e0100']]
    assert m.uc.mem_read(m.state + 2, 1) == b'\0'
    m.call('hct_take_under_cook')
    assert len([t for t in m.trace if t[0] == 'hci_host_recv_packet']) == 1
    assert not any(t[0] == 'free' for t in m.trace)
    m.call('hct_tx_done')
    assert m.trace[-1] == ['bb_find_to_afternoon', 3]
    assert not any(t[0] == 'free' for t in m.trace)
    m.call('hct_pass_within_head')
    assert ['free', EVENT] in m.trace and m.uc.mem_read(m.state + 2, 1) == b'\x01'
    record('RX H4 borrowed packet stalls without ack; scheduled ack frees queue head', m)

    # Receive-side evidence for the flags nibble: queue flags become bits
    # 12..15 of the H4 ACL header. TX retains the same raw flags value.
    for flags in range(16):
        m = machine_type(binary, externals, kind=1)
        m.uc.mem_write(m.state + 52, struct.pack('<I', EVENT))
        m.uc.mem_write(m.state + 76, struct.pack('<H', 27))
        m.uc.mem_write(EVENT + 16, struct.pack('<HHI', 0x123, 4, CLASSIC + 8))
        m.uc.mem_write(EVENT + 24, bytes([flags]))
        m.uc.mem_write(CLASSIC + 8, b'ABCD')
        m.call('hct_take_under_cook')
        callbacks = [t for t in m.trace if t[0] == 'hci_host_recv_packet']
        packet = bytes.fromhex(callbacks[0][3])
        assert packet == b'\x02' + struct.pack('<HH', 0x123 | (flags << 12), 4) + b'ABCD'
        record(f'RX ACL flags nibble {flags}', m, packet_hex=packet.hex())

    for features in (0,1,64,128,192,255):
        for value in (0,1,2,3,4,255,65535,0xffffffff):
            m=machine_type(binary,externals,features=features)
            m.call('hct_leave_in_bother',value)
            m.call('lm_proc_save_about_hearing',value,0,value,0)
            m.call('lm_proc_tell_to_complex',INPUT)
            record(f'features-{features}-{value}',m)
    for kind in (1,3,4):
        for flow in (0,1,2,3,255):
            for credit in (0,1,5,6,65535):
                m=machine_type(binary,externals,kind=kind)
                m.uc.mem_write(m.state+80,bytes([flow]))
                m.uc.mem_write(m.state+68,struct.pack('<4H',65534,5,4,5))
                m.uc.mem_write(INPUT,struct.pack('<2H',0,1));m.uc.mem_write(INPUT+16,struct.pack('<2H',credit,credit))
                m.call('lm_proc_save_about_ruin',2,INPUT,INPUT+16)
                m.call('hct_set_to_method',0);m.call('hct_give_in_discount',1)
                record(f'credits-{kind}-{flow}-{credit}',m)
    for connection in (0,1,2):
        for old in (0,5):
            for message_id in (0x1019,0x1400,0x1407,0x1408,0x1409,0x1411,0x1415,0x141a,0x141d,0x141f,0x1423):
                m=machine_type(binary,externals,connection=connection)
                m.uc.mem_write(INPUT+8,struct.pack('<HB',message_id,old))
                m.call('hct_leave_with_ability',INPUT,0)
                record(f'reroute-{connection}-{old}-{message_id}',m)
    for group in (0,1,2,3,4,5,6,7,8,9,62,63,64,65535):
        m=machine_type(binary,externals)
        for command in (0,1,14,15,31,63,255):m.call('hct_reserve_within_physics',group,command)
        record(f'command-table-{group}',m)
    for count in (-1,0):
        m=machine_type(binary,externals);m.call('lm_proc_save_about_ruin',count,INPUT,INPUT+16)
        record(f'credits-count-{count}',m)
    m=machine_type(binary,externals)
    m.call('bt21_save_under_dig');m.call('bt21_go_about_pool')
    m.call('hct_make_at_sell');m.call('hct_take_of_metal')
    m.call('hct_write_at_month');m.call('hct_set_by_god');m.call('hct_get_inside_minimum',INPUT)
    m.call('hct_take_into_brain');m.call('hct_take_into_host')
    record('initialize-and-empty-transport',m)
    apis={s.name for s in ELFFile(io.BytesIO(archive_member('libbtctrl.a','hct.o'))).get_section_by_name('.symtab').iter_symbols() if s['st_info']['type']=='STT_FUNC'}
    assert apis<=Machine.covered,apis-Machine.covered
    report['functions_exercised']=sorted(apis)

    if args.recovered:
        report['recovered_source_sha256']=hashlib.sha256((ROOT/'bare/drivers/src/bt_hct_transport_recovered.c').read_bytes()).hexdigest()
        report['tables_sha256']=hashlib.sha256((ROOT/'bare/drivers/inc/bt_hct_transport_tables.inc').read_bytes()).hexdigest()
        report['compilers']=list(DifferentialMachine.binaries)
        report['model']='Original/recovered RV32 transport differential execution; controller and ROM services modeled.'
    OUT.mkdir(parents=True, exist_ok=True)
    (OUT / ('bt-hct-transport-differential.json' if args.recovered else 'bt-hct-transport-probes.json')).write_text(json.dumps(report, indent=2) + '\n')
    print(f'{len(report["cases"])} hct.o transport groups passed')


if __name__ == '__main__':
    main()
