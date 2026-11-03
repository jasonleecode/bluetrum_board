#!/usr/bin/env python3
"""Regenerate known MP3 AUDEC inputs by executing the vendor CPU front end
on the host only. No vendor code is linked into the board probe.
Requires Unicorn 2.1.4, pyelftools, FFmpeg and the repository RISC-V linker.
"""
import argparse, hashlib, zlib
from pathlib import Path
import subprocess, struct, io, json
import unicorn
from unicorn import *
from unicorn.riscv_const import *
from elftools.elf.elffile import ELFFile
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--archive', required=True, type=Path)
parser.add_argument('--output', required=True, type=Path)
args=parser.parse_args()
archive=args.archive.read_bytes()
assert hashlib.sha256(archive).hexdigest() == '962e5a8ea3ba7cbae87c7902ad8cb5d97448f3fea9857a3f56a1e3ed1d8e818a', 'Unexpected SDK archive'
P=args.output.resolve(); P.mkdir(parents=True, exist_ok=True)
ROOT=Path(__file__).resolve().parents[2]
subprocess.run(['ffmpeg','-hide_banner','-loglevel','error','-f','lavfi','-i',
 'sine=frequency=1000:sample_rate=44100:duration=0.16','-ac','1','-c:a','libmp3lame',
 '-b:a','128k','-reservoir','0','-write_xing','0','-id3v2_version','0','-y',str(P/'tone.mp3')],check=True)
TC=str(ROOT/'.toolchains/riscv64-unknown-elf-gcc-10.1.0-2020.08.2-x86_64-linux-ubuntu14/bin/riscv64-unknown-elf-')
# Read first matching archive members (the SDK has a second encoder huffman.o).
def member(name):
 b=archive; i=8
 while i<len(b):
  h=b[i:i+60]; n=int(h[48:58]); s=b[i+60:i+60+n]
  if h[:16].decode().strip().rstrip('/')==name: return s
  i+=60+n+(n%2)
 raise ValueError(name)
for n in ('layer3.o','bit.o','huffman.o'): (P/n).write_bytes(member(n))
objs=[str(P/n) for n in ('layer3.o','bit.o','huffman.o')]
syms=subprocess.check_output([TC+'nm','-u',*objs],text=True)
externs=set(x.split()[-1] for x in syms.splitlines() if ' U ' in x)
defs=subprocess.check_output([TC+'nm','--defined-only',*objs],text=True)
externs-=set(x.split()[-1] for x in defs.splitlines() if len(x.split())==3)
hooks={n:0x80000+i*16 for i,n in enumerate(sorted(externs))}
(P/'host.ld').write_text('SECTIONS { . = 0x10000; .text : { *(.text*) *(.mp3dec*) } . = 0x30000; .data : { *(.data*) *(.mp3buf*) } . = 0x54000; .mp3 : { *(.mp3_decram_buf) } }')
subprocess.run([TC+'ld','-m','elf32lriscv','--no-relax','-T',str(P/'host.ld'),'-e','III_decode','-o',str(P/'host.elf'),*objs,*[f'--defsym={n}={a}' for n,a in hooks.items()]],check=True)
u=Uc(UC_ARCH_RISCV,UC_MODE_RISCV32);u.mem_map(0,0x100000)
e=ELFFile(io.BytesIO((P/'host.elf').read_bytes()))
for s in e.iter_sections():
 if s['sh_flags']&2 and s['sh_type']!='SHT_NOBITS':u.mem_write(s['sh_addr'],s.data())
sym={s.name:s['st_value'] for s in e.get_section_by_name('.symtab').iter_symbols()}
regs={0x180:0x08000000}; vectors=[]
def hook(uc,addr,size,unused):
 name=next(n for n,a in hooks.items() if a==addr)
 a,b,c=[uc.reg_read(r) for r in (UC_RISCV_REG_A0,UC_RISCV_REG_A1,UC_RISCV_REG_A2)]
 if name=='memcpy': uc.mem_write(a,bytes(uc.mem_read(b,c)))
 elif name=='memset': uc.mem_write(a,bytes([b&255])*c)
 elif name in ('os_msc_sem_pend','mpeg_pcm_output'):pass
 else:raise RuntimeError((name,hex(a),b,c))
 uc.reg_write(UC_RISCV_REG_PC,uc.reg_read(UC_RISCV_REG_RA))
for a in hooks.values():
 u.mem_write(a,b'\x67\x80\x00\x00');u.hook_add(UC_HOOK_CODE,hook,begin=a,end=a)
def rd(uc,access,addr,size,value,unused): uc.mem_write(addr,struct.pack('<I',regs.get(addr,0)))
def wr(uc,access,addr,size,value,unused):
 regs[addr]=value
 if addr==0x184:
  data=bytes(uc.mem_read(0x55000,0x1c38))
  vectors.append((regs[0x180],value,data))
u.hook_add(UC_HOOK_MEM_READ,rd,begin=0,end=0xfff)
u.hook_add(UC_HOOK_MEM_WRITE,wr,begin=0,end=0xfff)
def call(name,*args):
 u.reg_write(UC_RISCV_REG_RA,0x90000);u.reg_write(UC_RISCV_REG_SP,0x9f000)
 for r,a in zip([UC_RISCV_REG_A0,UC_RISCV_REG_A1,UC_RISCV_REG_A2,UC_RISCV_REG_A3],args):u.reg_write(r,a)
 try: u.emu_start(sym[name],0x90000,count=1000000)
 except UcError:
  pc=u.reg_read(UC_RISCV_REG_PC);print('exception PC',hex(pc),bytes(u.mem_read(pc,8)).hex());raise
 assert u.reg_read(UC_RISCV_REG_PC)==0x90000,(name,'limit')
 return u.reg_read(UC_RISCV_REG_A0)
def w32(a,n):u.mem_write(a,struct.pack('<I',n))
def bits(state,address,data):
 u.mem_write(address,data+b'\0'*8)
 call('mad_bit_init',state,address,0,len(data))
 w32(state+16,len(data))
mp3=(P/'tone.mp3').read_bytes();pos=0;frame=0;ctx=0x60000
while pos<len(mp3):
 h=int.from_bytes(mp3[pos:pos+4],'big'); assert h>>21==0x7ff
 assert ((h>>19)&3)==3 and ((h>>17)&3)==1 and ((h>>16)&1)==1
 assert ((h>>10)&3)==0 and ((h>>6)&3)==3 and ((h>>12)&15)==9
 length=144*128000//44100+((h>>9)&1); data=mp3[pos:pos+length]; assert len(data)==length
 w32(ctx+64,3);w32(ctx+68,0);w32(ctx+72,0);w32(ctx+88,44100);w32(ctx+92,0)
 bits(ctx+24,0x61000,data[4:21])
 r=call('III_sideinfo',ctx);assert r==0,(frame,'sideinfo',r)
 assert int.from_bytes(u.mem_read(ctx+104,4),'little')==0,'reservoir is disabled'
 bits(ctx+348,0x62000,data[21:])
 r=call('mad_layer_III',ctx);assert r==0,(frame,'layer3',r)
 print('frame',frame,'vectors',len(vectors),'con',hex(regs[0x180]))
 pos+=length;frame+=1
(P/'vectors.bin').write_bytes(b''.join(struct.pack('<II',con,kick)+data for con,kick,data in vectors))
(P/'vectors.json').write_text(json.dumps({'frames':frame,'granules':len(vectors),'stride':0x1c40,'sample_rate':44100,'channels':1},indent=2))
print('OK',frame,len(vectors))
words=[]; rows=[]
for con,kick,data in vectors:
 first=len(words)
 # Mono software front-end writes the left quantized spectrum and 39 band descriptors.
 for offset in list(range(0x1000,0x1900,4))+list(range(0x2b00,0x2b9c,4)):
  value=struct.unpack_from('<I',data,offset-0x1000)[0]
  if value:words.append((offset,value))
 rows.append((con,kick,first,len(words)-first))
head='#include <stdint.h>\nstruct mp3_word {uint32_t offset,value;};\nstruct mp3_vector {uint32_t control,kick; unsigned first,count;};\n'
head+=f'#define MP3_VECTOR_COUNT {len(rows)}\nstatic const struct mp3_word mp3_words[]={{\n'
head+=''.join(f'{{0x{o:x},0x{v:08x}}},\n' for o,v in words)+'};\nstatic const struct mp3_vector mp3_vectors[]={\n'
head+=''.join('{0x%08x,0x%08x,%d,%d},\n'%r for r in rows)+'};\n'
(P/'mp3_vectors.h').write_text(head)
print('sparse words',len(words),'bytes',len(words)*8)

assert len(vectors)==16
packed=b''.join(struct.pack('<II',con,kick)+data[:0x900]+data[0x1b00:0x1b9c]
                for con,kick,data in vectors)
(P/'vectors.zlib').write_bytes(zlib.compress(packed,9))
print('sample sha256',hashlib.sha256((P/'tone.mp3').read_bytes()).hexdigest())
print('vector sha256',hashlib.sha256((P/'vectors.zlib').read_bytes()).hexdigest())
