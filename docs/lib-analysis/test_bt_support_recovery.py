#!/usr/bin/env python3
"""Per-object differential checks for recovered controller support objects.

Original RV32 instructions execute against the same memory and external hooks
as Clang/GCC replacements. ROM service implementations remain outside scope.
"""
import argparse
import hashlib
import io
import itertools
from functools import lru_cache
import json
import random
import shutil
import struct
import subprocess
import tempfile
from pathlib import Path
import unicorn
from unicorn import Uc, UC_ARCH_RISCV, UC_MODE_RISCV32, UC_HOOK_CODE, UC_HOOK_MEM_READ, UC_HOOK_MEM_WRITE
from unicorn import riscv_const as R
from elftools.elf.elffile import ELFFile
from test_bt_core_recovery import Machine as CoreMachine, AREGS, SREGS, STOP, STACK, member

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT/'docs/lib-analysis/generated/recovery'
MODULES = ('ext_util', 'bb_utils', 'll_util', 'lm_sleep', 'ble_sleep', 'weak_symbol', 'bt21', 'ble', 'lm', 'bb_buf', 'mgr_util', 'bb_debug', 'baseband_platform', 'lm_proc', 'ble_evt', 'ble_con', 'mgr_proc', 'll', 'll_cntl', 'mgr', 'll_proc')
DATA = {'ext_init_reserve_on_test': 0x30000, 'll_give_about_aspect': 0x31000, 'ble_con_show_of_beat': 0x32000,
        'mgr_cause_within_nobody': 0x33000, 'mgr_util_fetch_into_grandfather': 0x34000,
        'mgr_take_with_try': 0x35000, 'lm_go_without_farmer': 0x36000,
        'lm_cause_by_subject': 0x37000, 'lm_get_inside_thanks': 0x38000, 'lm_proc_pass_to_knee': 0x39000, 'ble_con_make_on_name':0x3a000, 'ble_con_keep_inside_cell':0x3b000, 'mgr_pick_into_politics':0x3c000,'ll_send_within_collection':0x3d000,'ll_keep_to_hospital':0x3e000,'ll_show_for_chest':0x3f000}
DATA.update({n:0x61000+i*0x100 for i,n in enumerate(('mgr_read_for_boy','mgr_take_inside_band','mgr_find_within_welcome','mgr_set_into_theory','mgr_keep_under_combine'))})
BUF, ARG, STATUS, CONTEXT, OTHER = 0x50000, 0x50100, 0x50200, 0x40000, 0x41000
ROM = {0x84360: ('query', 1), 0x84364: ('set_state', 2), 0x844f4: ('sleep', 2),
       0x84368: ('register',2), 0x843a0: ('allocate',4), 0x843a4: ('send',1),
       0x845a4: ('get_buffers',2), 0x843e8: ('init_queue',4), 0x843c0: ('init_list',1), 0x843c8: ('append',2),
       0x84684: ('irq2',0), 0x8467c: ('irq128',0), 0x84680: ('irq8',0), 0x8461c: ('irq16',0)}
CALLS = {'memcpy': 3, 'memset': 3, 'memcmp': 3, 'ble_con_take_by_join': 1,
         'ble_evt_read_within_distribution': 0, 'bt_get_local_bd_addr': 1,
         'ble_take_for_screw':1, 'ble_leave_about_television':1, 'bb_find_to_afternoon':1,
         'mgr_find_at_writing':2, 'ble_con_make_on_burn':2, 'ble_get_local_bd_addr':1, 'my_printf':8, 'bt_get_packet':1, 'bthw_isr_do':0}
CALLS.update({n:0 for n in ('bt21_give_to_dust','bt21_find_on_you','bt21_save_under_dig',
             'bt21_set_on_tennis','bt21_keep_within_grocery','bt21_go_about_pool','bb_write_without_passenger',
             'bb_get_at_clue','bt21_save_in_design','bb_give_on_enthusiasm','ble_pick_to_teacher',
             'ble_make_of_distribution','bb_pick_under_strike')})
ROM.update({0x84324:('heap_alloc',2),0x84328:('heap_free',1),0x84340:('scheduler_register',2),0x84348:('scheduler_pending',1),
            0x84630:('uncertainty',2),0x84638:('advance',1),0x8463c:('prepare',1),0x84644:('anchor',2),
            0x84648:('schedule',1),0x84650:('update_ready',3),0x84654:('update_wait',3),
            0x84658:('complete',1),0x8465c:('cancel',1),0x84668:('detach',2)})
CALLS.update({'bthw_soft_kick':0,'ble_evt_go_by_elevator':2,'ble_evt_cause_for_impress':1,
              'ble_evt_find_without_school':1,'ble_evt_reserve_by_conflict':2})
ROM.update({0x84620:('bind_callbacks',1),0x84624:('bind_contexts',2),0x8460c:('attach_packet',2),
            0x84608:('enable_event',1),0x84660:('cancel_update',1),0x84670:('release_link',1)})
CALLS.update({'ble_con_fetch_at_pressure':1,'ble_con_show_without_uncle':5,'ble_con_do_without_notice':2,
              'ble_con_keep_on_muscle':2,'ble_con_give_to_series':6,'ble_con_give_at_black':1,
              'ble_con_cause_with_cold':4,'ble_con_make_in_spirit':2,'ble_con_give_within_suit':2,
              'ble_con_leave_of_dance':4,'bb_rf_go_inside_log':1,
              'ble_con_keep_by_success':1,'ble_con_show_to_slip':1,'ble_con_send_at_age':3,
              'ble_con_do_by_fill':0,'ble_con_tell_by_advantage':0})
ROM.update({0x84594:('random32',0),0x84380:('timer_set',3),0x843a8:('empty_message',3)})
MGR_CALLS=('mgr_pass_inside_top','mgr_send_for_rate','mgr_leave_on_night','mgr_send_without_alternative',
           'mgr_do_without_strength','mgr_show_in_wife','mgr_get_to_town','mgr_leave_by_roll','mgr_keep_by_commission')
CALLS.update({n:1 for n in MGR_CALLS})
CALLS.update({'ll_go_without_restaurant':3,'ll_sell_under_data':1,'mgr_get_at_coffee':1,'ll_save_under_decision':0,
              'mgr_save_under_green':1,'mgr_pick_of_answer':1,'mgr_show_within_nobody':4,'ll_proc_go_for_department':4,
              'mgr_proc_fetch_at_novel':1,'mgr_make_by_hand':1,'bb_rf_set_on_inflation':2,
              'ble_con_go_with_pen':1,'ble_con_write_under_state':1})
CALLS.update({'ll_get_on_spread':2,'ll_read_of_yellow':1,'ll_take_into_culture':1,
              'll_reserve_without_bathroom':1,'ble_con_keep_within_broad':1,'ll_leave_inside_instruction':2})
ROM.update({0x84384:('timer_clear',2)})
CALLS.update({'ll_cntl_leave_about_cup':3,'ll_read_under_responsibility':2,'ll_send_for_consideration':3,
              'll_send_under_supermarket':2,'ble_con_sell_by_champion':2,'ll_cntl_pick_without_bid':2,
              'ble_con_cause_about_mud':2,'ll_leave_without_cry':3,'ll_write_within_surprise':2})
ROM.update({0x843d0:('queue_pop',1),0x8434c:('scheduler_ack',1)})
CALLS.update({'ll_util_give_to_credit':1,'ll_util_fetch_to_staff':2,'ble_con_take_inside_pace':4,'ll_do_under_regret':2,
 'ble_con_find_of_blow':2,'mgr_take_on_diamond':1,'mgr_write_in_difference':3,'mgr_make_within_entrance':1,
 'mgr_write_at_other':0,'ble_con_find_inside_official':1,'ble_con_make_on_street':4,'ble_con_get_of_shoe':2,
 'ble_con_cause_within_youth':3,'ble_con_reserve_on_option':2,'ble_con_do_by_wall':1,
 'bb_fetch_in_event':1,'bb_get_without_card':1,'mgr_fetch_on_savings':0})
ROM.update({0x843ac:('forward',2)})
CALLS.update({'bb_rf_fetch_within_fix':2,'ll_tell_under_slip':4,'ll_tell_under_position':1,'ll_go_into_variation':1,
 'll_reserve_within_drawing':5,'ll_make_to_display':5,'ble_con_find_inside_service':3,'ll_get_into_contribution':2})
CALLS.update({'ll_cntl_'+n:2 for n in ('show_for_phase','read_within_record','find_about_visual','sell_into_beat',
 'cause_within_recommendation','reserve_of_football','cause_with_high','find_at_crazy','write_by_event',
 'take_without_hook','write_on_quiet','find_in_heat','leave_in_objective','sell_at_community','send_on_meaning','set_of_reward')})
CALLS.update({'ll_cntl_'+n:1 for n in ('pick_without_support','save_under_outcome','tell_on_spend','pick_in_peak',
 'show_of_interview','go_about_stupid','set_to_farm')})
RETURNS_CON={'ble_con_get_of_shoe','ble_con_cause_within_youth','ble_con_keep_within_broad',
             'ble_con_make_on_street','ble_con_take_inside_pace','ble_con_reserve_on_option','ble_con_do_by_wall'}
RETURNS_EVT={'ble_evt_sell_for_mortgage','ble_con_give_at_black','ble_con_leave_of_dance',
             'ble_evt_give_without_worth','ble_con_show_without_uncle','ble_con_give_to_series',
             'ble_con_cause_with_cold','ble_evt_find_for_stock','ble_evt_read_within_distribution'}
CALLS.update({'lm_leave_in_treat':2,'ext_util_write_within_brain':2,'ext_util_show_on_finger':2,
 'ext_util_read_without_raw':1,'ext_util_save_of_ease':1,'lm_proc_save_about_hearing':4,
 'lm_proc_tell_to_complex':1,'lm_proc_save_about_ruin':3,'lm_proc_do_on_kiss':0})
RETURNS = {'ext_init_cause_within_lift', 'ext_util_save_of_ease', 'ext_util_read_without_raw',
           'ext_util_leave_for_variety', 'ext_util_leave_about_wait', 'bb_utils_keep_on_pain',
           'mgr_find_at_writing', 'bb_utils_pass_without_hole', 'bb_utils_save_about_joke',
           'bb_utils_keep_of_age', 'bb_utils_go_by_promise', 'll_util_give_to_credit',
           'll_util_fetch_to_staff', 'bb_send_to_mall', 'bb_get_at_attitude',
           'bb_rf_get_about_importance', 'weak_symbol_give_to_preference', 'mgr_fetch_on_savings',
           'mgr_write_in_difference','mgr_take_on_diamond','ll_save_under_decision','ll_read_of_yellow',
           'll_take_into_culture','ble_con_do_without_notice','ll_reserve_without_bathroom',
           'bb_rf_reserve_into_strategy','bb_pass_within_library','register_isr'}


def build(args, tmp, module):
    source = ROOT/f'bare/drivers/src/bt_{"platform" if module=="baseband_platform" else module}_recovered.c'
    original = member('libbtctrl.a', module+'.o')
    (tmp/'original.o').write_bytes(original)
    (tmp/'save.o').write_bytes(member('libhal.a', 'save-restore.o'))
    obj = ELFFile(io.BytesIO(original))
    symbols = obj.get_section_by_name('.symtab')
    apis = {s.name: (obj.get_section(s['st_shndx']).name, s['st_info']['bind'])
            for s in symbols.iter_symbols() if s['st_info']['type'] == 'STT_FUNC'}
    sections = {s.name: (0x10000+i*0x1000, s['sh_size']) for i,s in enumerate(obj.iter_sections())
                if s['sh_flags'] & 2 and not s['sh_flags'] & 4 and s['sh_size']}
    external = [s.name for s in symbols.iter_symbols() if s.name and s['st_shndx'] == 'SHN_UNDEF'
                and s.name not in DATA and not s.name.startswith('__riscv_')]
    hooks = {0x180000+i*16: (name, CALLS[name]) for i,name in enumerate(external)}
    compilers = [('clang', ['clang', '--target=riscv32-unknown-elf'])]
    if args.gcc: compilers.append(('gcc', [args.gcc, '-msave-restore']))
    for name, command in compilers:
        subprocess.run([*command, '-march=rv32imc', '-mabi=ilp32', '-ffreestanding', '-fno-builtin',
                        '-Os', '-Wall', '-Wextra', '-Werror', '-I', str(ROOT/'bare/drivers/inc'),
                        '-c', str(source), '-o', str(tmp/(name+'.o'))], check=True)
        actual = ELFFile(io.BytesIO((tmp/(name+'.o')).read_bytes()))
        syms = actual.get_section_by_name('.symtab')
        assert {s.name for s in syms.iter_symbols() if s['st_info']['type']=='STT_FUNC'
                and s['st_info']['bind']!='STB_LOCAL'} == {n for n,(_,b) in apis.items() if b!='STB_LOCAL'}
        for n,(section,bind) in apis.items():
            sym=syms.get_symbol_by_name(n)[0]
            assert sym['st_info']['bind']==bind and actual.get_section(sym['st_shndx']).name==section, (module,name,n)
        for section in sections:
            if module in ('mgr','ll_proc') and section=='.rodata':continue # compiler-private jump tables
            left,right=actual.get_section_by_name(section),obj.get_section_by_name(section)
            assert left is not None, (module,name,section)
            for field in ('sh_size','sh_type','sh_addralign'):
                assert left[field]==right[field],(module,name,section,field,left[field],right[field])
            assert left.data()==right.data(), (module,name,section,'initial bytes')
    script=tmp/'link.ld'
    script.write_text('SECTIONS { '+ ' '.join(f'{s} 0x{a:x} : {{ *({s}) }}' for s,(a,_) in sections.items())+
                      ' .text 0x200000 : { *(.text*) *(.com_text*) *(.sleep_text*) } '
                      '.data 0x60000 : { *(.data*) *(.sdata*) *(.rodata*) } .bss : { *(.bss*) *(.sbss*) } '
                      '__global_pointer$ = 0x60800; }')
    binaries={}
    for name in ['original', *[n for n,_ in compilers]]:
        elf=tmp/(name+'.elf')
        subprocess.run([args.linker,'-m','elf32lriscv','--no-relax','-T',str(script),'-e','0x200000',
                        '-o',str(elf),str(tmp/(name+'.o')),str(tmp/'save.o'),
                        *[f'--defsym={n}={a}' for a,(n,_) in hooks.items()],
                        *[f'--defsym={n}={a}' for n,a in DATA.items() if any(s.name==n and s['st_shndx']=='SHN_UNDEF' for s in symbols.iter_symbols())]],check=True)
        binaries[name]=elf.read_bytes()
    OUT.mkdir(parents=True,exist_ok=True)
    listing=subprocess.check_output(['llvm-objdump-10','--mattr=+m','-dr',str(tmp/'original.o')],text=True)
    (OUT/f'bt-{module}.llvm.txt').write_text(listing.replace(str(tmp/'original.o'),f'libbtctrl.a:{module}.o'))
    return original,source,apis,sections,binaries,{**hooks,**ROM}


@lru_cache(maxsize=8)
def image_info(binary):
    elf=ELFFile(io.BytesIO(binary))
    sections=[(s['sh_addr'],s.data()) for s in elf.iter_sections() if s['sh_flags']&2 and s['sh_type']!='SHT_NOBITS']
    symbols={s.name:s['st_value'] for s in elf.get_section_by_name('.symtab').iter_symbols()}
    return sections,symbols


class Machine:
    put32,get32,restore=CoreMachine.put32,CoreMachine.get32,CoreMachine.restore
    def __init__(self,binary,apis,sections,hooks,config):
        self.uc=Uc(UC_ARCH_RISCV,UC_MODE_RISCV32);self.uc.mem_map(0,0x400000)
        loaded,self.symbols=image_info(binary)
        for address,data in loaded:self.uc.mem_write(address,data)
        self.api={n:self.symbols[n] for n in apis}
        self.config,self.sections=config,sections
        rng=random.Random(config.get('seed',0))
        for a in (BUF,ARG,CONTEXT,OTHER):self.uc.mem_write(a,bytes(rng.randrange(256) for _ in range(128)))
        self.regs={a:rng.getrandbits(32) for a in (0x800c,0x8018,0x8030,0x8034,0x8038,0x803c,0x8040,0x8044,0x540,0x544,0xf01c,0xf020,0xf0fc,0x8010,0xf010,0xf018,0xf050,0xf060)}
        self.regs.update({a:rng.getrandbits(32) for a in (0xf000,0xf024,0xf028,0xf0f0,0xf00c,0xf090,0xf0b0,0xf0b4,0xf0b8,0xf110,0xf02c,0xf0a0,0xf0c0,0xf0c4,0xf0c8,0xf0cc,0xf0d0,0xf0d4,0xf0e0)})
        self.regs[0xf01c]=config.get('now',1000);self.regs[0xf020]=config.get('phase',0);self.regs[0xf0fc]=0
        self.regs[0x8010]=config.get('pending',0);self.regs[0xf010]=config.get('pending',0)
        self.regs[0x8038]=config.get('ticks',self.regs[0x8038])
        self.uc.mem_write(DATA['ext_init_reserve_on_test'], bytes(36))
        self.uc.mem_write(DATA['ext_init_reserve_on_test']+16, bytes(config.get('features',[0]*8)))
        for i in range(4):self.put32(DATA['ll_give_about_aspect']+4*i,CONTEXT+i*128)
        self.put32(DATA['ble_con_show_of_beat'],0 if config.get('null') else CONTEXT)
        self.uc.mem_write(CONTEXT+74,bytes([config.get('busy',0)]))
        self.put32(CONTEXT+32,config.get('target',1000));self.put32(BUF,config.get('duration',10000))
        if '.btmem.bb.util' in sections and sections['.btmem.bb.util'][1]==24:
            base=sections['.btmem.bb.util'][0]
            for i in range(2):
                self.put32(base+i*12,config.get('active',(1,1))[i])
                addr=bytes(self.uc.mem_read(ARG,6)) if config.get('match',-1)==i else bytes([i+1]*6)
                self.uc.mem_write(base+i*12+4,addr)
        self.uc.mem_write(0x11000,bytes(rng.randrange(256) for _ in range(0x800)))
        self.uc.mem_write(DATA['mgr_cause_within_nobody'],bytes(rng.randrange(256) for _ in range(160)))
        if config.get('address_match',-1)>=0:
            self.uc.mem_write((0x1166a if config.get('bank') else 0x11658)+6*config['address_match'],bytes(self.uc.mem_read(ARG,6)))
        if 'bit_bytes' in config:self.uc.mem_write(ARG,bytes(config['bit_bytes']))
        self.put32(STATUS,CONTEXT)
        self.uc.mem_write(DATA['lm_proc_pass_to_knee']+80,bytes([config.get('enabled',0)]))
        self.uc.mem_write(0x52000,bytes(rng.randrange(256) for _ in range(0x1000)))
        if config.get('evt'):self.setup_evt(config)
        if config.get('con'):self.setup_con(config)
        if config.get('mgr_handlers'):self.setup_mgr_handlers(config)
        if config.get('link'):self.setup_ll(config)
        if config.get('control'):self.setup_control(config)
        if config.get('manager'):self.setup_manager(config)
        if config.get('link_handlers'):self.setup_link_handlers(config)
        self.trace=[];self.allocations=0;self.polls=0;self.blocked=False
        for a,payload in hooks.items():
            self.uc.mem_write(a,b'\x67\x80\x00\x00');self.uc.hook_add(UC_HOOK_CODE,self.hook,payload,begin=a,end=a)
        if config.get('reassembly_override'):
            a=self.api['ll_show_of_depression'];self.uc.hook_add(UC_HOOK_CODE,self.hook,('reassembly_enabled',0),begin=a,end=a)
        for low,high in ((0x540,0x547),(0x8000,0x80ff),(0xf000,0xf1ff)):
            self.uc.hook_add(UC_HOOK_MEM_READ,self.read,begin=low,end=high)
            self.uc.hook_add(UC_HOOK_MEM_WRITE,self.write,begin=low,end=high)
        for count in range(13):
            a=self.symbols.get(f'__riscv_restore_{count}')
            if a:
                self.uc.hook_add(UC_HOOK_CODE,self.restore,count,begin=a,end=a)

    def setup_link_handlers(self,c):
        self.setup_control(c)
        self.uc.mem_write(ARG,bytes(64));self.uc.mem_write(ARG+2,struct.pack('<HHHHHH',6,24,0,100,0,0))
        self.put32(ARG+8,OTHER+0x500);self.uc.mem_write(OTHER+0x504,b'\x00\x00')
        self.uc.mem_write(ARG+9,b'\x00');self.put32(ARG+8,OTHER+0x500)
        for off,default in ((14,1),(26,42),(27,1),(35,0),(36,0),(37,0),(70,0),(71,19),(73,1),(74,0),(77,136)):
            self.uc.mem_write(CONTEXT+off,bytes([c.get('p'+str(off),default)]))
        if c.get('channels_match'):self.uc.mem_write(CONTEXT+16,bytes(range(5)))
        self.uc.mem_write(ARG+2,bytes([c.get('value',0)]))
        if 'payload' in c:self.uc.mem_write(ARG,bytes(c['payload']))
        if c.get('receive'):
            self.uc.mem_write(ARG,bytes([c.get('slot',0),c.get('count',3),c.get('completed',1),c.get('control_completed',1)]))
            for i in range(8):
                base=OTHER+0xc00+46*i
                self.uc.mem_write(base+2,struct.pack('<HHH',c.get('rx_flags',0),c.get('header',0x1001),i*17))

    def setup_manager(self,c):
        m=self.symbols['mgr_cause_within_nobody'];self.manager=m
        self.uc.mem_write(m,bytes(156));self.put32(m+48,0 if c.get('null') else CONTEXT)
        self.put32(m+8,OTHER+0x200 if c.get('deferred') else 0)
        self.uc.mem_write(OTHER+0x210,bytes([c.get('size',7)]))
        for off,default in ((18,0),(19,7),(20,8),(21,1),(29,1),(30,1),(31,1),(32,31),(40,12),(41,2),(42,0),(44,2),(58,0)):
            self.uc.mem_write(m+off,bytes([c.get('m'+str(off),default)]))
        self.put32(DATA['ble_con_keep_inside_cell'],OTHER+0x700)
        self.put32(OTHER+0x700+8,OTHER+0x500);self.put32(OTHER+0x700+12,OTHER+0x800)
        self.uc.mem_write(DATA['mgr_read_for_boy'],bytes(range(16)));self.uc.mem_write(DATA['mgr_take_inside_band'],b'\x01\x02\x03')
        self.uc.mem_write(DATA['ble_con_show_of_beat']+20,b'\x05')
        self.uc.mem_write(ARG,bytes(range(64)))
        if 'payload' in c:self.uc.mem_write(ARG,bytes(c['payload']))
        self.uc.mem_write(ARG+30,struct.pack('<HHH',c.get('interval',24),c.get('latency',0),c.get('timeout',100)))
        if 'payload' not in c or 'header' in c:self.uc.mem_write(ARG+4,struct.pack('<H',c.get('header',0x1000)))
        self.uc.mem_write(CONTEXT,bytes(range(64)))
        if c.get('queued'):
            self.put32(m,CONTEXT);self.put32(CONTEXT,OTHER if c.get('chain') else 0);self.put32(OTHER,0)
            self.uc.mem_write(CONTEXT+12,struct.pack('<H',c.get('destination',2)))
        if c.get('records'):
            for i in range(c['records']):
                self.uc.mem_write(m+77+i*7,bytes([c.get('record_flags',1)])+bytes(self.uc.mem_read(ARG+8,6)))

    def setup_control(self,c):
        def half(a,n,v):self.uc.mem_write(a+n,struct.pack('<H',v&65535))
        self.put32(DATA['ll_give_about_aspect'],CONTEXT)
        self.put32(DATA['ble_con_keep_inside_cell'],OTHER+0x700)
        self.put32(OTHER+0x700+8,OTHER+0x500);self.put32(OTHER+0x700+12,OTHER+0x800)
        self.put32(OTHER+0x700+20,OTHER+0xc00)
        self.put32(CONTEXT,OTHER)
        for offset in (35,36,37,70,71,72):self.uc.mem_write(CONTEXT+offset,bytes([c.get('p'+str(offset),0)]))
        half(CONTEXT,10,c.get('timeout',12));half(OTHER,48,c.get('accuracy',7));half(OTHER,62,c.get('latency',2))
        half(OTHER,66,c.get('counter',0));self.put32(DATA['ble_con_make_on_name']+4,OTHER+0x600)
        for offset in (6,10):half(ARG,offset,c.get('instant',10))
        if 'size' in c:
            half(OTHER+0xc00,4,c['size']<<8);self.uc.mem_write(OTHER+0xc00+8,bytes([c.get('opcode',0)]))
        if c.get('data_length'):
            for offset in (1,5):half(ARG,offset,c['data_length'])
            for offset in (3,7):half(ARG,offset,c.get('data_time',328))

    def setup_ll(self,c):
        def half(a,n,v):self.uc.mem_write(a+n,struct.pack('<H',v&65535))
        self.put32(self.symbols['ll_give_about_aspect'],0 if c.get('null') else CONTEXT)
        self.put32(DATA['ble_con_keep_inside_cell'],OTHER+0x700)
        self.put32(OTHER+0x700+20,OTHER+0x800)
        self.put32(CONTEXT,OTHER)
        half(CONTEXT,10,c.get('timeout',12));half(CONTEXT,12,c.get('next_timeout',30))
        for offset in (35,37,75,76):self.uc.mem_write(CONTEXT+offset,bytes([c.get('p'+str(offset),0)]))
        half(OTHER,48,c.get('accuracy',7));half(OTHER,60,24);half(OTHER,62,c.get('latency',2));half(OTHER,64,0)
        self.put32(DATA['ble_con_make_on_name']+4,OTHER+0x600)
        for slot in range(8):
            packet=OTHER+0x800+46*slot
            half(packet,4,c.get('header',0x1002));half(packet,8,c.get('expected',28))
            self.uc.mem_write(packet+10,bytes([c.get('type',4)]));self.uc.mem_write(packet+12,bytes([c.get('opcode',82)]))
        for offset in (1,5):half(ARG,offset,c.get('length',27))

    def setup_mgr_handlers(self,c):
        base=DATA['mgr_cause_within_nobody']
        for offset in (18,43,52,54,55,58,76):self.uc.mem_write(base+offset,bytes([c.get('m'+str(offset),0)]))
        self.put32(base+48,CONTEXT)
        self.put32(DATA['ble_con_keep_inside_cell'],OTHER)
        self.put32(OTHER+12,OTHER+0x400);self.put32(OTHER+20,OTHER+0x600)
        for i in range(8):
            packet=OTHER+0x600+i*46
            self.uc.mem_write(packet+2,struct.pack('<H',c.get('rx_flags',0)))
            self.uc.mem_write(packet+4,bytes([c.get('rx_kind',i)]))
        self.uc.mem_write(ARG,bytes([c.get('payload0',0),c.get('count',0),0,c.get('payload3',0)]))
        if c.get('boundary'):
            prefix=192 if c.get('m76',0)==0 else 0 if c['m76']==1 else 64
            self.uc.mem_write(ARG,bytes([255]*5+[prefix|63]) if c['boundary']==1 else bytes([0]*5+[prefix]))

    def setup_con(self,c):
        def half(a,n,v):self.uc.mem_write(a+n,struct.pack('<H',v&65535))
        base=self.symbols['ble_con_make_on_name']
        for i in range(8):self.put32(base+4+4*i,OTHER+i*128)
        self.put32(DATA['ble_con_keep_inside_cell'],CONTEXT)
        self.put32(CONTEXT+12,OTHER+0x800);self.put32(CONTEXT+20,OTHER+0xa00)
        self.uc.mem_write(CONTEXT+24,b'\x02');half(BUF,4,0)
        event=OTHER+0x900;half(event,58,c.get('length',10));half(event,60,c.get('interval',24))
        half(ARG,0,c.get('input_interval',24));half(ARG,2,c.get('duration',10))
        half(ARG,13,c.get('input_interval',24));half(ARG,15,c.get('maximum',32));half(ARG,17,2);half(ARG,21,c.get('duration',10))
        self.uc.mem_write(ARG+10,bytes([c.get('kind',0)]))
        if c.get('update'):half(ARG,0,c.get('handle',0))
        self.uc.mem_write(OTHER+0x800,bytes(range(42)))

    def setup_evt(self,c):
        def half(a,n,v):self.uc.mem_write(a+n,struct.pack('<H',v&65535))
        def byte(a,n,v):self.uc.mem_write(a+n,bytes([v&255]))
        p=CONTEXT;q=OTHER
        for i in range(6):self.put32(DATA['ble_con_make_on_name']+4*i,q+i*128)
        self.put32(p,0);self.put32(p+4,0);self.put32(p+8,q)
        self.put32(p+32,c.get('target',1000));half(p,56,c.get('offset',0));half(p,58,c.get('duration',10))
        half(p,60,c.get('interval',24));half(p,62,c.get('latency',1));half(p,64,0)
        half(p,66,c.get('counter',0));half(p,70,c.get('count',0))
        for n in (73,74,75,76,77,79,83):byte(p,n,c.get('b'+str(n),0))
        self.put32(q,0);self.put32(q+4,p);self.put32(q+8,50);half(q,12,c.get('interval',24))
        for i in range(6):
            hw=q+i*128
            half(hw,10,c.get('channel',0x8025));half(hw,12,c.get('control',0x8000))
            half(hw,20,c.get('hw_interval',24));half(hw,68,1000);half(hw,70,0);half(hw,72,c.get('phase',0));half(hw,76,c.get('hw_flags',0))
        # Group and radio records occupy distinct slots for schedule tests.
        group=OTHER+0x800;first=CONTEXT+0x100;second=CONTEXT+0x200
        self.put32(group,0);self.put32(group+4,first if c.get('children') else 0)
        self.put32(group+8,c.get('available',20));half(group,12,c.get('interval',24))
        for i,child in enumerate((first,second)):
            self.put32(child,0);self.put32(child+4,second if i==0 and c.get('children',0)>1 else 0)
            half(child,56,c.get('child_start',2)+i*8);half(child,58,c.get('child_length',4))
            self.put32(child+32,c.get('target',1000));half(child,64,i)
        base=self.sections['.btmem.bb.ble'][0]
        self.put32(base,p if c.get('head') else 0);self.put32(base+8,group if c.get('group') else 0)
        self.put32(base+12,group if c.get('group') else 0)
        self.put32(p+8,group)
        half(ARG,0,12);half(ARG,2,2);half(ARG,4,12);half(ARG,6,2);half(ARG,10,20)
        byte(BUF,19,c.get('window',1));half(BUF,20,c.get('connect_offset',5));byte(BUF,33,c.get('accuracy',2)<<5)
        if c.get('chain'):
            self.put32(p,first);self.put32(first,second);self.put32(second,0)
        if c.get('first_as_head'):self.put32(base,first)

    def hook(self,uc,address,size,payload):
        name,count=payload;args=[uc.reg_read(r) for r in AREGS[:count]];result=0xdeadbeef
        observed=['stack' if STACK-1024<=a<STACK else a for a in args]
        observed=[next((n for n,a in self.api.items() if a==v),v) for v in observed]
        if name in ('mgr_find_at_writing','memcmp'):
            observed += [bytes(uc.mem_read(args[0],args[2] if name=='memcmp' else 6)).hex(),bytes(uc.mem_read(args[1],args[2] if name=='memcmp' else 6)).hex()]
        if name=='ll_cntl_set_of_reward':observed.append(bytes(uc.mem_read(args[1],12)).hex())
        if name=='ll_go_without_restaurant' and args[0]==60:observed.append(bytes(uc.mem_read(args[2]+6,6)).hex())
        if name in ('ll_do_under_regret','ble_con_take_inside_pace'):
            observed.append(bytes(uc.mem_read(args[0],15)).hex())
        if name=='my_printf':
            def string(a):return bytes(uc.mem_read(a,128)).split(b'\0')[0].decode('ascii')
            fmt=string(args[0])
            observed=[fmt,args[1]] if fmt.startswith('error:') else [fmt,string(args[1]),args[2],args[3],string(args[4]),args[5],string(args[6]),string(args[7])]
        self.trace.append(('call',name,observed))
        if name=='memcpy':
            a,b,n=args;uc.mem_write(a,bytes(uc.mem_read(b,n)));result=a
        elif name=='memset':
            a,b,n=args;uc.mem_write(a,bytes([b&255])*n);result=a
        elif name=='memcmp':
            a,b,n=args;left,right=bytes(uc.mem_read(a,n)),bytes(uc.mem_read(b,n))
            result=next((x-y for x,y in zip(left,right) if x!=y),0)
        elif name=='mgr_find_at_writing':result=int(bytes(uc.mem_read(args[0],6))==bytes(uc.mem_read(args[1],6)))
        elif name in ('ble_con_show_without_uncle','ble_con_cause_with_cold','ble_con_give_to_series','ble_con_leave_of_dance'):
            result=OTHER+0x900
            if name=='ble_con_give_to_series':uc.mem_write(args[5],b'\x12\x34\x56\x78\x9a')
        elif name in ('ble_con_do_without_notice','ble_con_give_at_black'):result=self.config.get('result',7)
        elif name=='heap_alloc':
            result=0x52000+self.allocations*128;self.allocations+=1
        elif name=='ble_con_find_inside_service':uc.mem_write(args[2],bytes(range(12)))
        elif name=='bb_rf_fetch_within_fix':result=(args[0]*3+args[1])&255
        elif name=='queue_pop':
            result=self.get32(args[0]);self.put32(args[0],self.get32(result) if result else 0)
        elif name=='ll_util_give_to_credit':uc.mem_write(args[0],b'\x00\x00');result=self.config.get('credit',0)
        elif name=='mgr_write_in_difference':
            uc.mem_write(args[2],struct.pack('<H',self.config.get('index',1)))
            result=self.config.get('empty_match',1) if bytes(uc.mem_read(args[0],6))==b'\xff'*6 else self.config.get('match',0)
        elif name=='mgr_take_on_diamond':result=self.config.get('diamond',0)
        elif name=='mgr_fetch_on_savings':result=self.config.get('address_mode',1)
        elif name in ('ble_con_make_on_street','ble_con_get_of_shoe','ble_con_cause_within_youth','ble_con_take_inside_pace','ble_con_reserve_on_option','ble_con_do_by_wall'):result=OTHER+0x900
        elif name=='init_list':self.put32(args[0],0);self.put32(args[0]+4,0)
        elif name=='uncertainty':result=(args[0]*7+args[1])&0xffff
        elif name=='scheduler_pending':result=self.config.get('pending',0)
        elif name=='allocate':
            result=0x52000+self.allocations*128;self.allocations+=1
        elif name=='get_buffers':self.put32(args[0],OTHER);self.put32(args[1],CONTEXT)
        elif name.startswith('irq'):
            self.regs[0xf010]&=~int(name[3:])
        elif name=='ble_get_local_bd_addr':uc.mem_write(args[0],bytes(self.config.get('address',range(6))))
        elif name in ('ext_util_read_without_raw','ext_util_save_of_ease'):
            uc.mem_write(args[0],bytes(range(8 if name=='ext_util_read_without_raw' else 6)));result=args[0]
        elif name=='ext_util_show_on_finger':
            uc.mem_write(args[0],b'\xab');uc.mem_write(args[1],b'\xcd')
        elif name in ('lm_proc_save_about_hearing','lm_proc_save_about_ruin'):result=self.config.get('result',0)
        elif name=='ll_cntl_leave_about_cup':
            uc.mem_write(args[0],bytes(x^y for x,y in zip(uc.mem_read(args[1],8),uc.mem_read(args[2],8))))
        elif name=='ll_get_on_spread':pass
        elif name in ('ll_read_of_yellow','ll_take_into_culture'):
            uc.mem_write(args[0],bytes(range(5 if name=='ll_read_of_yellow' else 8)));result=args[0]
        elif name in ('ll_reserve_without_bathroom','ble_con_keep_within_broad'):result=self.config.get('result',1)
        elif name=='reassembly_enabled':result=1
        elif name=='query':result=self.config.get('query2',self.config.get('query',0)) if args[0]==2 else self.config.get('query',0)
        elif name in MGR_CALLS or name=='bb_rf_set_on_inflation':result=self.config.get('result',0)
        elif name=='ll_save_under_decision':result=self.config.get('decision',2)
        elif name=='random32':result=0x12345678+len(self.trace)
        elif name=='sleep':
            self.put32(args[0],self.config.get('sleep_duration',123));result=self.config.get('sleep_result',0x101)
        elif name=='ble_evt_read_within_distribution':result=self.config.get('now',0)
        elif name=='bt_get_local_bd_addr':uc.mem_write(args[0],bytes((self.config.get('seed',0)+i)&255 for i in range(6)))
        elif self.config.get('changing') and name in ('set_state','ble_con_take_by_join'):
            for i in range(4):self.put32(DATA['ll_give_about_aspect']+4*i,OTHER+i*128)
        if self.config.get('changing'):self.regs[0x544]^=0x101
        for i,r in enumerate(AREGS):uc.reg_write(r,0xa5a50000+i)
        uc.reg_write(AREGS[0],result&0xffffffff);uc.reg_write(R.UC_RISCV_REG_PC,uc.reg_read(R.UC_RISCV_REG_RA))

    def read(self,uc,access,address,size,value,_):
        assert size==4 and address in self.regs,(address,size)
        if address==0xf0fc:self.regs[address]=0
        if address==0x544 and self.config.get('changing'):self.regs[address]^=0x41
        if address==0xf010:
            self.polls+=1
            if self.polls>=12:self.blocked=True;uc.emu_stop()
        self.trace.append(('read',address,self.regs[address]));self.put32(address,self.regs[address])
    def write(self,uc,access,address,size,value,_):
        assert size==4 and address in self.regs,(address,size)
        self.trace.append(('write',address,value));self.regs[address]=value
        if address==0xf018:self.regs[0xf010]&=~value
    def call(self,name,*args):
        uc=self.uc;uc.reg_write(R.UC_RISCV_REG_SP,STACK);uc.reg_write(R.UC_RISCV_REG_RA,STOP)
        for i,r in enumerate(SREGS):uc.reg_write(r,0x12345600+i)
        for i,r in enumerate(AREGS):uc.reg_write(r,args[i]&0xffffffff if i<len(args) else 0xa5a50000+i)
        uc.emu_start(self.api[name],STOP,count=100000)
        if self.blocked:
            self.trace.append(('blocked',name));return
        assert uc.reg_read(R.UC_RISCV_REG_PC)==STOP,name
        assert uc.reg_read(R.UC_RISCV_REG_SP)==STACK,name
        assert [uc.reg_read(r) for r in SREGS]==[0x12345600+i for i in range(12)],name
        if name in ('ll_show_of_depression','ll_fetch_with_brother') or (self.config.get('manager') and name in set(MGR_CALLS)|{'mgr_cause_in_go','mgr_write_by_being'}) or self.config.get('link_handlers') or self.config.get('mgr_handlers') or name in RETURNS or name in RETURNS_EVT or name in RETURNS_CON or name.startswith('lm_proc_'):self.trace.append(('return',name,uc.reg_read(AREGS[0])))
    def snapshot(self):
        own=[]
        for section,(a,n) in self.sections.items():
            if section=='.rodata' and (self.config.get('manager') or self.config.get('link_handlers')):continue # compiler-generated branch targets
            data=bytearray(self.uc.mem_read(a,n))
            if section in ('.rodata.bb.tbl','.rodata.ble.tbl'):
                canonical={address:0x700000+i*4 for i,(name,address) in enumerate(sorted(self.api.items()))}
                for i in range(0,n-3,4):
                    value=struct.unpack_from('<I',data,i)[0]
                    if value in canonical:struct.pack_into('<I',data,i,canonical[value])
            own.append(bytes(data))
        return self.trace,self.regs,own+[bytes(self.uc.mem_read(a,n)) for a,n in
                ((0x11000,0x800),(0x30000,0x10000),(CONTEXT,0x2000),(BUF,0x3000),(0x60000,0x2000))]


def cases(module):
    if module=='ext_util':
        for seed,threshold in itertools.product(range(9),(-2,-1,0,1,0x7fffffff,-0x80000000)):
            features=[0]*8
            if seed<8:features[seed]=255
            yield f'features-{seed}-{threshold}',dict(features=features,seed=seed),[
                ('ext_util_leave_for_variety',threshold,STATUS,BUF),('ext_init_cause_within_lift',ARG),
                ('ext_util_save_of_ease',BUF),('ext_util_read_without_raw',BUF),
                ('ext_util_set_to_menu',0xfedcba98+seed),('ext_util_leave_about_wait',BUF),
                ('ext_util_write_within_brain',seed&1,seed&2),('ext_util_show_on_finger',BUF,BUF+1)]
    elif module=='bb_utils':
        for i,(active,match) in enumerate(itertools.product(itertools.product((0,1,2),repeat=2),(-1,0,1))):
            for handle in (0,1,127,128,129,130,255,0x10080,0xffffffff):
                yield f'lookup-{i}-{handle}',dict(active=active,match=match,seed=i,query=i&1),[
                    ('bb_utils_pass_without_hole',ARG),('bb_utils_save_about_joke',ARG),
                    ('bb_utils_keep_of_age',handle),('bb_utils_go_by_promise',handle),
                    ('bb_utils_keep_on_pain',handle),('mgr_find_at_writing',ARG,ARG),
                    ('mgr_find_at_writing',ARG,BUF),('bt21_give_to_dust',),('bb_utils_pass_without_hole',ARG)]
    elif module=='ll_util':
        for seed in range(32):
            yield f'link-{seed}',dict(seed=seed,query=seed&1,changing=bool(seed&2)),[
                ('ll_util_give_to_credit',BUF),('ll_cntl_pick_without_bid',seed%4,seed*19),
                ('ll_cntl_leave_about_cup',BUF,ARG,ARG+8),('ll_util_fetch_to_staff',seed%4,ARG),
                ('ll_util_write_inside_god',seed%4,seed*23)]
    elif module=='lm_sleep':
        for i,value in enumerate((0,1,2,3,20,625,0xffff,0x7fffffff,0x80000000,0xffffffff)):
            for option in (0,1,0x100):
                yield f'sleep-{i}-{option}',dict(seed=i,ticks=value,sleep_result=value),[
                    ('ext_init_get_without_name',),('bb_send_to_mall',BUF,option),
                    ('bb_send_without_line',value,option),('bb_find_with_brush',),('bb_read_within_homework',)]
    elif module=='ble_sleep':
        for i,(now,delta,margin,duration) in enumerate(itertools.product(
                (0,1,0x7fffffe,0xffffffff),(-0x30bb01,-0x30bb00,-1,0,1,2,0x30bb00,0x30bb01),(0,1,1000),(0,1,10000))):
            yield f'window-{i}',dict(now=now,target=(now+delta+8)&0xffffffff,margin=margin,duration=duration),[('bb_get_at_attitude',BUF,margin)]
        for null,busy in ((True,0),(False,1),(False,255)):
            yield f'busy-{null}-{busy}',dict(null=null,busy=busy),[('bb_get_at_attitude',BUF,0)]
    elif module=='weak_symbol':
        for seed in range(256):
            yield f'address-{seed}',dict(seed=seed),[('bb_rf_get_about_importance',),('weak_symbol_give_to_preference',),
                ('ble_get_local_bd_addr',BUF),('mgr_fetch_on_savings',)]

    elif module=='bt21':
        for seed in range(32):
            pending=(seed&7)|((seed&8)<<7)|(seed&16)<<25
            yield f'irq-{seed}',dict(seed=seed,pending=pending,changing=bool(seed&1)),[
                ('bb_do_at_review',),('bb_tell_with_base',),('bb_pass_in_airport',)]
    elif module=='ble':
        for seed in range(32):
            pending=sum(bit for i,bit in enumerate((2,128,8,16,32)) if seed&(1<<i))
            yield f'irq-{seed}',dict(seed=seed,pending=pending,changing=bool(seed&1)),[
                ('ble_write_about_black',),('bb_go_on_valuable',),('ble_sell_about_sun',),
                ('bb_take_about_press',),('bb_reserve_without_volume',)]
        yield 'unhandled-irq',dict(pending=1),[('bb_reserve_without_volume',)]
    elif module=='lm':
        for i,value in enumerate((0,1,127,128,255,256,65535,0xffffffff)):
            yield f'message-{i}',dict(seed=i),[('bt21_keep_within_grocery',),
                ('lm_leave_in_treat',value,~value),('lm_send_with_consequence',~value,value)]
    elif module=='bb_buf':
        for seed in range(16):
            yield f'buffers-{seed}',dict(seed=seed),[('bt21_find_on_you',),
                *[('ll_sell_under_data',i) for i in range(8)],('bt21_find_on_you',)]
    elif module=='mgr_util':
        for seed,(bank,match) in enumerate(itertools.product((0,1,0x100),(-1,0,1,2))):
            yield f'address-{seed}',dict(seed=seed,bank=bank,address_match=match),[
                ('mgr_write_in_difference',ARG,bank,BUF),('mgr_take_on_diamond',ARG),
                ('mgr_take_on_diamond',DATA['mgr_util_fetch_into_grandfather']),
                ('mgr_make_within_entrance',bank),('mgr_util_pick_with_refrigerator',),
                ('mgr_write_at_other',),('mgr_proc_fetch_at_novel',seed*37),('ll_save_under_decision',),
                ('ll_read_of_yellow',BUF),('ll_take_into_culture',BUF),
                *[('ll_reserve_without_bathroom',bit) for bit in range(64)]]
        for value in range(256):
            for length in (0,1,4,5,8):
                yield f'bits-{value}-{length}',dict(bit_bytes=[value]*8),[('ble_con_do_without_notice',ARG,length)]
    elif module=='bb_debug':
        for kind in (0,1,2,3,4,0xffffffff):
            for value in (0,1,10,11,255,256,0xffff,0xffffffff):
                yield f'print-{kind}-{value}',dict(seed=kind+value),[
                    ('bb_debug_give_within_lie',kind,value,value,value),
                    ('bb_debug_keep_by_type',STATUS,kind),('bb_debug_reserve_for_unit',STATUS)]

    elif module=='ll_proc':
        names=('show_into_sing','give_for_satisfaction','reserve_of_property','send_in_fuel','go_on_event',
         'keep_under_diamond','pick_within_link','do_at_brave','show_without_audience','write_about_apartment',
         'find_under_truth','make_at_blame','fetch_within_celebration','fetch_within_conflict','reserve_in_painting',
         'show_of_present','cause_inside_hate','pass_in_change','tell_on_method','get_about_friendship',
         'make_inside_impression','pick_on_parent','pass_at_string','reserve_in_promise','set_for_honey',
         'leave_inside_motor','go_with_parking','make_on_sale','sell_by_give','cause_for_job','reserve_for_roll',
         'pick_about_earth','reserve_without_conflict','pick_for_good','cause_under_value','give_under_hole')
        for state,flags,role in itertools.product((*range(23),255),(0,4,8,16,31),(0,1)):
            config=dict(link_handlers=True,query=state,p37=flags,decision=role,p35=role,p36=role,p70=role,p74=role,
                p14=state,p77=136 if role else 0,value=flags%3,channels_match=role)
            for name in names:yield f'{name}-{state}-{flags}-{role}',config,[('ll_proc_'+name,0,ARG,2,9)]
        for flags,header,count in itertools.product((0,1,2,16,32,136),(0,0x1000,0x1001,0x1002,0x1003),(0,1,3)):
            yield f'receive-{flags}-{header}-{count}',dict(link_handlers=True,receive=True,rx_flags=flags,header=header,count=count,query=7),[
                ('ll_proc_go_on_event',0,ARG,2,9)]
        for minimum,maximum,latency,timeout,role in itertools.product((5,6,24),(24,3200,3201),(0,500,501),(9,10,3200,3201),(0,1)):
            payload=list(struct.pack('<7H',0,minimum,maximum,latency,timeout,0,0))
            yield f'update-{minimum}-{maximum}-{latency}-{timeout}-{role}',dict(link_handlers=True,query=1,decision=role,payload=payload),[
                ('ll_proc_reserve_for_roll',0,ARG,2,9)]
        for reason in (4,5,6,18,19,20,21,26,41,59,60):
            yield f'disconnect-{reason}',dict(link_handlers=True,query=1,value=reason),[('ll_proc_pick_about_earth',0,ARG,2,9)]

    elif module=='mgr':
        for seed in range(32):
            c=dict(manager=True,seed=seed,null=seed&1,credit=seed&1,queued=seed&1,chain=seed&2,
                address_mode=seed%4,address=[255 if seed&4 else 0]*6,destination=seed%4,deferred=seed&1,
                m18=seed%8,m29=seed%3,m44=seed%3,payload=[seed%3,seed%33,seed%9]+list(range(3,25)))
            actions=[('mgr_go_for_frame',seed%3,ARG),('mgr_make_about_pick',seed%3,ARG),
                ('mgr_show_within_nobody',100,9,1,seed),('ll_proc_go_for_department',101,9,1,seed),('mgr_do_into_item',),
                ('mgr_get_at_coffee',ARG),('mgr_save_under_green',ARG),('mgr_make_to_delivery',ARG+8,seed%3),('mgr_save_under_green',ARG),
                ('mgr_do_without_strength',ARG),('mgr_send_for_rate',ARG),('mgr_send_without_alternative',ARG),
                ('mgr_show_in_wife',ARG),('ble_con_do_by_fill',),('mgr_make_by_hand',ARG),('mgr_write_inside_pay',),
                ('ble_evt_find_without_school',),('ble_take_for_screw',seed&1),('mgr_set_inside_one',)]
            for action in actions:yield f'{action[0]}-{seed}',c,[action]
        for channel,length,pattern in itertools.product((0,11,12,13,39,40),(0,1,37,38),range(9)):
            c=dict(manager=True,payload=[channel,length,pattern])
            yield f'radio-{channel}-{length}-{pattern}',c,[('mgr_leave_by_roll',ARG),('mgr_keep_by_commission',ARG),('mgr_leave_inside_minute',ARG)]
        for kind,size,records,flags in itertools.product(range(8),(5,6,36,37),(0,1,10),(1,255)):
            yield f'report-{kind}-{size}-{records}-{flags}',dict(manager=True,header=(size<<8)|kind,records=records,record_flags=flags),[
                ('mgr_read_into_shock',BUF+16,ARG),('mgr_pick_of_answer',ARG),('mgr_pick_of_answer',ARG)]
        for bank,match,empty in itertools.product((0,1,2),(0,1),(0,1)):
            yield f'whitelist-{bank}-{match}-{empty}',dict(manager=True,payload=[bank],match=match,empty_match=empty),[
                ('mgr_cause_in_go',ARG),('mgr_write_by_being',ARG)]
        for interval,latency,timeout in itertools.product((5,6,24,3200,3201),(0,1,500,501),(9,10,100,3200,3201)):
            yield f'accept-{interval}-{latency}-{timeout}',dict(manager=True,interval=interval,latency=latency,timeout=timeout),[('mgr_save_under_green',ARG)]
        for kind,minimum,maximum,policy in itertools.product(range(5),(29,30,31,32),(32,0x4000,0x4001),(0,1,3,4)):
            payload=list(struct.pack('<HHBBB6sBB',minimum,maximum,kind,0,0,b'abcdef',7,policy))
            yield f'advertise-{kind}-{minimum}-{maximum}-{policy}',dict(manager=True,payload=payload),[('mgr_get_to_town',ARG)]
        for kind,interval,window in itertools.product(range(3),(3,4,16,0x4000,0x4001),(3,4,16,0x4000,0x4001)):
            payload=list(struct.pack('<BHHBB',kind,interval,window,1,0))
            yield f'scan-{kind}-{interval}-{window}',dict(manager=True,payload=payload),[('mgr_leave_on_night',ARG)]
        for minimum,maximum,latency,timeout in itertools.product((5,6,24),(24,3200,3201),(0,1,500,501),(9,10,3200,3201)):
            payload=list(struct.pack('<HHBB6sBHHHHHH',16,8,0,0,b'abcdef',0,minimum,maximum,latency,timeout,0,0))
            yield f'initiate-{minimum}-{maximum}-{latency}-{timeout}',dict(manager=True,payload=payload),[('mgr_pass_inside_top',ARG)]

    elif module=='ll_cntl':
        pointer_names=('send_on_meaning','leave_with_time','show_with_card','set_of_reward',
            'cause_within_recommendation','show_under_network','sell_into_beat','find_about_visual',
            'take_without_hook','write_by_event','find_at_crazy','read_within_record','fetch_at_football',
            'cause_with_high','reserve_of_football')
        single_names=('show_of_interview','pick_of_partner','go_about_stupid','set_to_farm',
            'save_under_outcome','pick_without_support','pick_in_peak','tell_on_spend')
        integer_names=('find_in_heat','tell_without_demand','sell_at_community','leave_in_objective','write_on_quiet')
        for seed in range(64):
            config=dict(control=True,seed=seed,p35=seed&1,p36=seed&2,p37=seed,p70=seed,p72=seed&1,
                        query=(seed//2)%6,decision=seed%3,result=seed&1,instant=(0,1,32766,32767,65535)[seed%5])
            actions=[('ll_cntl_'+n,0,ARG) for n in pointer_names]+[('ll_cntl_'+n,0) for n in single_names]
            actions += [('ll_cntl_'+n,0,(0,6,19,36,255)[seed%5]) for n in integer_names]
            actions += [('ll_leave_inside_instruction',0,seed),('ll_cntl_pass_about_spring',0,ARG,seed),
                ('ll_cntl_make_into_boat',0,seed),('ll_cntl_write_in_king',ARG,seed%32,BUF),
                ('ll_cntl_set_of_letter',),('ll_cntl_get_in_conflict',),('ll_cntl_reserve_without_display',)]
            for action in actions:yield f'{action[0]}-{seed}',config,[action]
        lengths=(12,8,2,23,13,1,1,2,9,9,1,1,6,2)
        for opcode in range(24):
            for encrypted in (0,2):
                for delta in (-1,0,1):
                    length=lengths[opcode] if opcode<14 else 8
                    size=(length+delta+(4 if encrypted else 0))&31
                    yield f'parse-{opcode}-{encrypted}-{delta}',dict(control=True,p37=encrypted,opcode=opcode,size=size),[
                        ('ll_cntl_show_for_phase',0,0)]
        yield 'blocked-receive',dict(control=True,p72=1),[('ll_cntl_show_for_phase',0,0)]
        for length,time in itertools.product((26,27,28),(327,328)):
            yield f'data-limits-{length}-{time}',dict(control=True,data_length=length,data_time=time),[
                ('ll_cntl_show_under_network',0,ARG)]

    elif module=='ll':
        for i,(value,result,length) in enumerate(itertools.product((0,1,34,255,65535),(0,1),(0,27,28,65535))):
            yield f'events-{i}',dict(link=True,seed=i,result=result,length=length,p35=i&1,p37=i&2,query=i%6,p75=i&1,p76=i&2),[
                ('ll_show_of_depression',),('ll_tell_under_slip',2,value,value,value),
                ('ll_go_without_restaurant',value,0,ARG),('ll_write_within_surprise',0,ARG),
                ('ll_read_under_responsibility',0,ARG),('ll_send_for_consideration',value,0,ARG),
                ('ll_send_under_supermarket',value,0),('ll_reserve_within_drawing',0x100,9,2,value,value),
                ('ll_make_to_display',0x100,9,2,value,value),('ll_leave_without_cry',0,value,value),
                ('ll_show_of_incident',value,0,OTHER),('ble_evt_reserve_by_conflict',0,value),
                ('ble_con_tell_by_advantage',0,OTHER),('ble_evt_go_by_elevator',0,OTHER),
                ('ble_evt_cause_for_impress',0),('ll_go_into_variation',0),('ll_get_into_contribution',0,i%8)]
        for i,(null,result,query) in enumerate(itertools.product((0,1),(0,1),(0,1,5))):
            yield f'lifecycle-{i}',dict(link=True,null=null,result=result,query=query),[
                ('ll_do_under_regret',ARG,OTHER),('ble_make_of_distribution',),('ll_tell_under_position',0),('ble_pick_to_teacher',)]
        for header,kind,opcode,expected in itertools.product((0x1002,0x1001),(3,4),(81,82),(23,28)):
            yield f'reassembly-{header}-{kind}-{opcode}-{expected}',dict(link=True,reassembly_override=True,header=header,type=kind,opcode=opcode,expected=expected),[
                ('ll_fetch_with_brother',0,0),('ll_fetch_with_brother',0,1),('ll_tell_under_slip',2,0,0,0)]
        yield 'connection-failure-event',dict(link=True),[('ll_go_without_restaurant',60,0,ARG)]

    elif module=='mgr_proc':
        names=('pick_inside_two','get_for_escape','save_by_holiday','reserve_with_stomach','cause_about_round',
               'make_in_dependent','show_on_limit','send_by_reply','read_without_habit','leave_within_feedback',
               'send_on_muscle','leave_to_result','go_without_purple','give_for_clothes','fetch_into_dream',
               'give_by_serve','fetch_into_influence','show_under_diamond','set_inside_cash','pick_within_side',
               'send_into_hearing','make_of_cat','make_on_standard','find_on_truth','tell_within_string')
        for seed,(state,value,result) in enumerate(itertools.product((0,1,2,3,4,5,255),(0,1,31,32),(0,1,18,37,38))):
            for name in names:
                yield f'handler-{name}-{seed}',dict(mgr_handlers=True,seed=seed,query=state,payload0=value,result=result,
                    m43=seed&1,m52=seed&1,m54=seed&1,m55=seed%4,m58=seed%3,m76=seed%3),[
                    ('mgr_proc_'+name,0x400,ARG,1,9)]
        for i,(state,count,kind,flags) in enumerate(itertools.product(range(6),(0,1,3),(0,3,5,6,7),(0,1))):
            yield f'receive-{i}',dict(mgr_handlers=True,query=state,count=count,rx_kind=kind,rx_flags=flags,
                m18=1,m54=1,m55=i%4,payload3=i&1),[('ll_proc_go_on_event',0,ARG,1,9),('ll_proc_keep_under_diamond',0,ARG,1,9)]
        for mode,boundary in itertools.product((0,1,2,255),(1,2)):
            yield f'address-{mode}-{boundary}',dict(mgr_handlers=True,m76=mode,boundary=boundary),[
                ('mgr_proc_set_inside_cash',0,ARG,1,9)]

    elif module=='ble_con':
        for seed in range(16):
            yield f'initialize-{seed}',dict(con=True,seed=seed),[
                ('ble_con_make_on_burn',ARG,seed),('ble_con_show_on_border',BUF),
                ('ble_leave_about_television',seed&1),('bb_pick_under_strike',)]
        for seed,(interval,duration,length,packet) in enumerate(itertools.product((0,6,7,24),(0,1,10,65535),(2,10),(0,BUF))):
            yield f'create-{seed}',dict(con=True,seed=seed,input_interval=interval,duration=duration,length=length),[
                ('ble_con_get_of_shoe',ARG,packet),('ble_con_cause_within_youth',ARG,BUF,2)]
        for seed in range(16):
            yield f'link-{seed}',dict(con=True,seed=seed,kind=seed%4,handle=seed%8,update=True),[
                ('ble_con_write_under_state',CONTEXT),('ble_con_find_of_blow',CONTEXT,seed),
                ('ble_con_find_inside_service',CONTEXT,ARG,STATUS),('ble_con_keep_within_broad',CONTEXT),
                ('ble_con_make_on_street',ARG,BUF,0 if seed&1 else BUF+32,seed),
                ('ble_con_find_inside_official',CONTEXT),('ble_con_cause_about_mud',CONTEXT,seed),
                ('ble_con_sell_by_champion',CONTEXT,ARG),('ble_con_take_inside_pace',BUF,ARG,CONTEXT,seed%8),
                ('ble_con_take_by_join',CONTEXT),('ble_con_go_with_pen',0 if seed&1 else CONTEXT)]
        for channel in (0,1,11,12,13,38,39,40,255,256,0xffffffff):
            yield f'test-channel-{channel}',dict(con=True),[
                ('ble_con_reserve_on_option',BUF,channel),('ble_con_do_by_wall',channel)]

    elif module=='ble_evt':
        for seed in range(32):
            yield f'isr-{seed}',dict(evt=True,seed=seed,pending=seed&1,b73=5 if seed&1 else 0,
                b75=seed&2,hw_flags=128 if seed&4 else 0,control=0x8000 if seed&8 else 0,
                b76=seed%4,b83=seed,b74=seed&1,head=bool(seed&2),now=(0x7fffff0+seed)&0xffffffff),[
                ('ble_evt_give_for_drawing',),('ble_evt_show_under_paint',CONTEXT),
                ('ble_con_keep_by_success',CONTEXT),('ble_con_send_at_age',CONTEXT,17,seed&1),
                ('ble_con_show_to_slip',CONTEXT),('ble_evt_read_within_distribution',),('ble_evt_find_for_stock',)]
        for count in (0,1,4):
            for active in (0,1):
                yield f'convert-{count}-{active}',dict(evt=True,count=count,b74=active),[
                    ('ble_con_keep_on_muscle',CONTEXT,0),('ble_con_give_at_black',CONTEXT),('ble_con_make_in_spirit',CONTEXT,123)]
        for reset,chain in itertools.product((0,1),(0,1)):
            yield f'reset-{reset}-{chain}',dict(evt=True,head=True,chain=chain,group=True),[
                ('ble_evt_sell_for_mortgage',0),('ble_evt_sell_for_mortgage',1),('ble_evt_sell_for_mortgage',8),
                ('ble_con_fetch_at_pressure',reset),('ble_evt_cause_for_soil',CONTEXT)]
        for i,(phase,offset,old) in enumerate(itertools.product((0,311,312,624,936),(0,3,4,65535),(0,OTHER))):
            yield f'create-{i}',dict(evt=True,phase=phase,connect_offset=offset),[
                ('ble_con_leave_of_dance',ARG,BUF,old,7),('ble_con_give_within_suit',ARG,CONTEXT),
                ('ble_con_cause_with_cold',3,0,24,5)]
        for i,(children,start,length,duration) in enumerate(itertools.product((0,1,2),(0,2,8),(0,2,6),(0,1,6,18,24))):
            yield f'schedule-{i}',dict(evt=True,group=True,children=children,child_start=start,child_length=length),[
                ('ble_evt_pass_within_time',CONTEXT,duration,12,48),
                ('ble_con_show_without_uncle',3,duration,12,48,2),
                ('ble_evt_give_without_worth',OTHER+0x800,start)]
        for i,(interval,available,minimum,maximum) in enumerate(((24,0,12,48),(24,20,30,48),(24,20,49,100),(24,20,200,220),(65535,20,65535,65535))):
            yield f'new-group-{i}',dict(evt=True,group=True,interval=interval,available=available),[
                ('ble_con_show_without_uncle',1,6,minimum,maximum,0)]
        yield 'empty-schedule',dict(evt=True),[('ble_con_show_without_uncle',1,6,12,24,0)]
        for now in (1000,1100,0x7fffff0):
            yield f'update-{now}',dict(evt=True,now=now,target=now,latency=0),[
                ('ble_con_give_to_series',CONTEXT,6,12,24,1,STATUS)]

    elif module=='lm_proc':
        names=('save_without_neck','reserve_to_boy','sell_at_space','sell_by_coffee','cause_under_cell',
               'take_within_bonus','go_of_chain','tell_in_film','sell_of_gas','go_about_presentation',
               'show_in_cause','send_without_aside')
        for seed,(enabled,result) in enumerate(itertools.product((0,1,255),(0,1,255,256,0xffffffff))):
            yield f'handlers-{seed}',dict(seed=seed,enabled=enabled,result=result),[
                ('lm_proc_'+name,seed,ARG,4,9) for name in names]

    elif module=='baseband_platform':
        for value in (0,1,255,0xffffffff):
            yield f'defaults-{value}',{},[('bb_rf_reserve_into_strategy',),('bb_save_about_song',value),
                ('ll_get_on_spread',),('register_isr',value,ARG),('bb_pass_within_library',BUF),
                ('bt21_save_in_design',),('bthw_thread_post',),('os_interrupt_enter',),
                ('os_interrupt_leave',),('bb_rf_take_without_start',)]


def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--linker',default=shutil.which('ld.lld'))
    parser.add_argument('--case');parser.add_argument('--gcc');parser.add_argument('--module',choices=MODULES,action='append');args=parser.parse_args()
    if not args.linker:parser.error('provide --linker /path/to/ld.lld')
    for module in args.module or MODULES:
        with tempfile.TemporaryDirectory(prefix='ab32-bt-support-') as directory:
            original,source,apis,sections,binaries,hooks=build(args,Path(directory),module)
        report=dict(original_object_sha256=hashlib.sha256(original).hexdigest(),
                    recovered_source_sha256=hashlib.sha256(source.read_bytes()).hexdigest(),
                    compilers=list(binaries)[1:],exports=sorted(n for n,(_,b) in apis.items() if b!='STB_LOCAL'),
                    local_functions=sorted(n for n,(_,b) in apis.items() if b=='STB_LOCAL'),unicorn_version=unicorn.__version__,
                    model='Original/recovered RV32 with external/ROM hooks and restore ABI shim; ordered MMIO, calls and memory.',cases=[])
        report['included_sources_sha256']={str(path.relative_to(ROOT)):hashlib.sha256(path.read_bytes()).hexdigest()
            for path in [ROOT/'bare/drivers/inc/bt_recovered_internal.h', *[ROOT/'bare/drivers/inc'/name for name in
            __import__('re').findall(r'#include "([^"\n]+\.inc)"',source.read_text())]]}
        covered=set()
        for name,config,actions in cases(module):
            if args.case and args.case not in name:continue
            machines={n:Machine(b,apis,sections,hooks,config) for n,b in binaries.items()}
            for action in actions:
                covered.add(action[0])
                for compiler,m in machines.items():
                    try:m.call(*action)
                    except Exception:
                        print(module,name,compiler,action,hex(m.uc.reg_read(R.UC_RISCV_REG_PC)),m.trace[-6:],flush=True);raise
                expected=machines['original'].snapshot()
                for compiler,m in machines.items():
                    actual=m.snapshot()
                    if actual!=expected:
                        for i,(a,b) in enumerate(zip(expected[0],actual[0])):
                            if a!=b:raise AssertionError((module,name,compiler,action,i,a,b))
                        for j,(left,right) in enumerate(zip(expected[2],actual[2])):
                            differences=[(k,a,b) for k,(a,b) in enumerate(zip(left,right)) if a!=b]
                            if differences:print('memory',j,differences[:24],flush=True)
                        raise AssertionError((module,name,compiler,action,'lengths',len(expected[0]),len(actual[0]),'state equal',expected[1:]==actual[1:]))
            report['cases'].append(dict(name=name,actions=len(actions),result='pass'))
            if len(report['cases'])%1000==0:print(f'{module}: {len(report["cases"])} groups checked',flush=True)
        expected=set(apis)
        if module=='baseband_platform':
            expected-={'baseband_platform_pass_by_hello','bthw_irq_init','bthw_isr_do_plus','baseband_platform_get_of_officer'}
        assert args.case or covered==expected,(module,expected-covered)
        (OUT/f'bt-{module}{"-subset" if args.case else ""}-differential.json').write_text(json.dumps(report,indent=2)+'\n')
        print(f'{module}: {len(report["cases"])} groups passed; {len(apis)} functions; original vs {report["compilers"]}',flush=True)


if __name__=='__main__':main()
