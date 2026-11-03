"""Adversarial checks of the exact C parser/CRC used by the RV32 loader."""
import ctypes
import hashlib
import json
from pathlib import Path
import random
import shutil
import struct
import subprocess
import tempfile
import zlib
import pack

HERE=Path(__file__).resolve().parent
OUT=HERE/'build'/'host_checks'
OUT.mkdir(parents=True,exist_ok=True)
cc=shutil.which('gcc') or 'D:/mingw64/bin/gcc.exe'
subprocess.run([cc,'-std=c11','-O2','-Wall','-Wextra','-Werror','-shared',
    '-Wl,--export-all-symbols',str(HERE/'package.c'),'-o',str(OUT/'package.dll')],check=True)
dll=ctypes.CDLL(str(OUT/'package.dll'))
dll.boot_validate.argtypes=[ctypes.c_void_p,ctypes.c_uint32]
dll.boot_validate.restype=ctypes.c_int
dll.boot_crc32.argtypes=[ctypes.c_void_p,ctypes.c_uint32]
dll.boot_crc32.restype=ctypes.c_uint32
checks=0
def check(condition,label):
    global checks
    if not condition:raise AssertionError(label)
    checks+=1
def valid(blob,offset=0x400000):
    buf=ctypes.create_string_buffer(blob)
    return dll.boot_validate(buf,offset)
def crc(blob):
    return dll.boot_crc32(ctypes.create_string_buffer(bytes(blob)),len(blob))
def mutate(blob,index,value,recrc=True):
    data=bytearray(blob);struct.pack_into('<I',data,index*4,value)
    if recrc:
        struct.pack_into('<I',data,60,0)
        struct.pack_into('<I',data,60,zlib.crc32(data)&0xFFFFFFFF)
    return bytes(data)

rng=random.Random(0xC0B01031)
for n in [0,1,2,3,4,7,8,31,64,127,255,256,4096,65537]:
    b=bytes(rng.randrange(256) for _ in range(n))
    check(crc(b)==zlib.crc32(b)&0xFFFFFFFF,f'CRC n={n}')
check(crc(b'123456789')==0xCBF43926,'standard CRC vector')
with tempfile.TemporaryDirectory(dir=OUT) as tmp:
    tmp=Path(tmp);inputs=[]
    for i,(name,_,_) in enumerate(pack.KINDS):
        path=tmp/name;path.write_bytes(bytes(rng.randrange(256) for _ in range(137+i*251)))
        inputs.append(path)
    m=pack.pack(inputs,tmp/'package')
    blob=(tmp/'package/header.bin').read_bytes()
    image=(tmp/'package/payload.bin').read_bytes()
    check(valid(blob)==0,'valid generated package')
    check(blob[:4]==b'CGB1','little endian magic')
    check(len(blob)==192,'exact C ABI192')
    for e,src in zip(m['entries'],inputs):
        begin=e['flash_offset']-m['manifest_flash_offset']
        payload=image[begin:begin+e['bytes']]
        check(payload==src.read_bytes(),'payload copied exactly')
        check(crc(payload)==e['crc32'],'actual C loader CRC validates copied payload')
        corrupt=bytearray(payload);corrupt[-1]^=1
        check(crc(corrupt)!=e['crc32'],'payload corruption detected by C readback CRC')
    # Every single-bit corruption of the complete header is rejected.
    for pos in range(len(blob)):
        for bit in range(8):
            corrupt=bytearray(blob);corrupt[pos]^=1<<bit
            check(valid(bytes(corrupt))!=0,f'header bit {pos}:{bit}')
    bad=[(0,0xFFFFFFFF),(1,2),(2,128),(3,0),(3,5),(4,0x1004),
         (5,0x1000000),(6,0x401000),(7,1),(8,0),(9,0),(10,0x10004),
         (11,0),(12,0x20000),(13,1),(14,1)]
    for index,value in bad:
        check(valid(mutate(blob,index,value))!=0,f'bad header word{index}')
    check(valid(blob,0x401000)!=0,'compiled manifest offset bound')
    for i,(_,dst,maxbytes) in enumerate(pack.KINDS):
        word=16+i*8
        for index,value in [(word,0),(word,5),(word+1,0x380000),(word+1,0x400000),
                            (word+1,0x401001),(word+1,0x800000),(word+1,0xFFFFF000),
                            (word+2,dst+4),(word+2,0xC00000),(word+2,0xF8014000),
                            (word+3,0),(word+3,maxbytes+1),(word+3,0xFFFFFFFF),
                            (word+5,1),(word+6,1),(word+7,1)]:
            check(valid(mutate(blob,index,value))!=0,f'bad section{i} word{index}')
    for i in range(1,4):
        check(valid(mutate(blob,16+i*8+1,m['entries'][0]['flash_offset']))!=0,'Flash sections overlap')
    # Alternate aligned manifest offset supports a layout selected after backup.
    alt=pack.pack(inputs,tmp/'alternate',0x500000)
    check(valid((tmp/'alternate/header.bin').read_bytes(),0x500000)==0,'alternate physical layout')
    for offset in [0x380000,0x39E000,0x400001,0x800000,0xFFFFFFFF]:
        try:pack.pack(inputs,tmp/'invalid',offset)
        except ValueError:check(True,'packer rejects bad offset')
        else:check(False,'packer accepted bad offset')
    huge=tmp/'huge.bin';huge.write_bytes(b'X'*(pack.KINDS[0][2]+1))
    try:pack.pack([huge]+inputs[1:],tmp/'too_big')
    except ValueError:check(True,'oversized game rejected')
    else:check(False,'oversized game accepted')
    # A complete simulation loads four real payloads only into whitelisted RAM.
    ram=bytearray(b'\xA5'*(16*1024*1024))
    for e in m['entries']:
        begin=e['flash_offset']-m['manifest_flash_offset'];dst=e['ddr_address']
        ram[dst:dst+e['bytes']]=image[begin:begin+e['bytes']]
        check(crc(ram[dst:dst+e['bytes']])==e['crc32'],'simulated DDR readback CRC')
        check(ram[dst-1]==0xA5 and ram[dst+e['bytes']]==0xA5,'DDR adjacent sentinels preserved')
    check(ram[0xC00000:0xC20000]==b'\xA5'*0x20000,'loader/stack/probe area untouched')

result={'status':'PASS','checks':checks,'parser':'actual package.c compiled as host DLL',
    'crc':'actual boot_crc32 against independent zlib vectors',
    'single_bit_header_corruptions':192*8,'source_sha256':{
        n:hashlib.sha256((HERE/n).read_bytes()).hexdigest() for n in ('package.h','package.c','pack.py','host_checks.py')}}
(HERE/'host_results.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
print(json.dumps(result))
