"""Audit linked ELF ranges, relocation payload, package CRCs, and exact inputs."""
import hashlib
import json
from pathlib import Path
import struct
import zlib
import pack

HERE=Path(__file__).resolve().parent
BUILD=HERE/'build'
PKG=(HERE.parent/'package') if (HERE.parent/'package').is_dir() else (HERE/'package_candidate')
checks=[]
def check(ok,label):
    if not ok:raise AssertionError(label)
    checks.append(label)
def loads(path):
    b=path.read_bytes();check(b[:7]==b'\x7fELF\x01\x01\x01','ELF32LE '+path.name)
    h=struct.unpack_from('<HHIIIIIHHHHHH',b,16)
    check(h[1]==243,'RISC-V machine '+path.name)
    result=[]
    for i in range(h[9]):
        q=struct.unpack_from('<8I',b,h[4]+i*h[8])
        if q[0]==1:result.append({'offset':q[1],'vaddr':q[2],'paddr':q[3],
                                 'filesz':q[4],'memsz':q[5],'flags':q[6]})
    return h[3],result
m=json.loads((BUILD/'build_manifest.json').read_text())
one=(BUILD/'stage1.bin').read_bytes();two=(BUILD/'stage2.bin').read_bytes()
check(one.endswith(two),'stage1 embeds exact linked stage2')
check(zlib.crc32(two)&0xFFFFFFFF==m['stage2_crc32'],'stage2 CRC matches embedded compile constant')
check(len(one)<=0x1F000,'stage1 fits immutable ROM copy slot')
slot=(BUILD/'boot_slot.bin').read_bytes()
check(slot==one+b'\xFF'*(0x1F000-len(one)),'ROM slot padding exact')
e,ranges1=loads(BUILD/'stage1.elf');check(e==0x1000,'stage1 entry1000')
for r in ranges1:
    check(r['vaddr']>=0x1000 and r['vaddr']+r['memsz']<=0x20000,'stage1 VMA low DDR')
e,ranges2=loads(BUILD/'stage2.elf');check(e==0xC00000,'stage2 high DDR entry')
for r in ranges2:
    check(r['vaddr']>=0xC00000 and r['vaddr']+r['memsz']<=0xC0F000,'stage2 VMA incl bss below stack')
    check(r['paddr']==r['vaddr'],'stage2 all LOAD VMA equals LMA')
manifest=json.loads((PKG/'manifest.json').read_text())
blob=(PKG/'header.bin').read_bytes();header=bytearray(blob);struct.pack_into('<I',header,60,0)
check(zlib.crc32(header)&0xFFFFFFFF==manifest['header_crc32'],'header independent CRC')
for s in manifest['segments']:
    b=(PKG/s['name']).read_bytes()
    check(len(b)==s['bytes'] and hashlib.sha256(b).hexdigest()==s['sha256'],'segment exact '+s['name'])
payload=(PKG/'payload.bin').read_bytes()
for e in manifest['entries']:
    start=e['flash_offset']-manifest['manifest_flash_offset']
    data=payload[start:start+e['bytes']]
    check(data==Path(e['source']).read_bytes(),'frozen source exact '+e['name'])
    check(zlib.crc32(data)&0xFFFFFFFF==e['crc32'],'section CRC '+e['name'])
    check(hashlib.sha256(data).hexdigest()==e['sha256'],'section SHA '+e['name'])
result={'status':'PASS','checks':len(checks),'details':checks,
        'stage1_loads':ranges1,'stage2_loads':ranges2,'hardware_access':False,
        'sources':{n:hashlib.sha256((HERE/n).read_bytes()).hexdigest() for n in
             ('stage1.S','stage1.ld','stage2_start.S','stage2.ld','stage2.c','package.c','package.h','build.py','pack.py')}}
(HERE/'artifact_review.json').write_text(json.dumps(result,indent=2)+'\n')
print(json.dumps({'status':'PASS','checks':len(checks),'stage1_bytes':len(one),'stage2_bytes':len(two)}))
