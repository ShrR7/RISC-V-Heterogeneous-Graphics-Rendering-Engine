"""Create a CGB1 game/resource package, without reading or writing hardware."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import zlib

MAGIC=0x31424743
HEADER_BYTES=192
KINDS=(('game.bin',0x1000,0x1F000),('atlas.bin',0x5C4000,0xBA000),
       ('stage9_tiles.bin',0x900000,0xB0000),('pure_scatter16.bin',0x9B0000,0x10000))
def align(n):return (n+4095)&~4095
def make_header(entries,offset):
    h=[MAGIC,1,HEADER_BYTES,4,0x1000,0x800000,offset,0,
       0x47505531,0x42554C39,0x10007,0x380000,0x1F000,0xEF6017,0,0]
    words=h[:]
    for i,e in enumerate(entries):words.extend([i+1,e['flash_offset'],e['ddr_address'],e['bytes'],e['crc32'],0,0,0])
    blob=bytearray(struct.pack('<48I',*words))
    struct.pack_into('<I',blob,60,zlib.crc32(blob)&0xFFFFFFFF)
    return bytes(blob)
def pack(inputs,out,offset=0x400000,bootstrap=None):
    if offset&4095 or not 0x39F000<=offset<0x800000:raise ValueError('Invalid manifest offset')
    entries=[];data=[];cursor=offset+4096
    for index,((name,dst,maximum),source) in enumerate(zip(KINDS,inputs)):
        source=Path(source).resolve();payload=source.read_bytes()
        if not 0<len(payload)<=maximum:raise ValueError(f'{name} size {len(payload)} exceeds {maximum}')
        cursor=align(cursor)
        if cursor+len(payload)>0x800000:raise ValueError('Package exceeds 8MB Flash')
        e={'kind':index+1,'name':name,'source':str(source),'flash_offset':cursor,
           'ddr_address':dst,'bytes':len(payload),'crc32':zlib.crc32(payload)&0xFFFFFFFF,
           'sha256':hashlib.sha256(payload).hexdigest()}
        entries.append(e);data.append(payload);cursor+=len(payload)
    image=bytearray(b'\xFF'*(align(cursor)-offset));header=make_header(entries,offset)
    image[:len(header)]=header
    for e,payload in zip(entries,data):
        begin=e['flash_offset']-offset;image[begin:begin+len(payload)]=payload
    out=Path(out).resolve();out.mkdir(parents=True,exist_ok=True)
    (out/'payload.bin').write_bytes(image)
    (out/'header.bin').write_bytes(header)
    segments=[{'name':'payload.bin','flash_offset':offset,'bytes':len(image),
               'sha256':hashlib.sha256(image).hexdigest()}]
    if bootstrap:
        boot=Path(bootstrap).read_bytes()
        if len(boot)!=0x1F000:raise ValueError('boot_slot.bin must be exactly the immutable ROM copy length')
        if offset<0x39F000:raise ValueError('Manifest overlaps ROM slot')
        (out/'boot_slot.bin').write_bytes(boot)
        segments.insert(0,{'name':'boot_slot.bin','flash_offset':0x380000,'bytes':len(boot),
                           'sha256':hashlib.sha256(boot).hexdigest()})
    manifest={'schema':1,'format':'CGB1','version':1,'flash_bytes':0x800000,
        'manifest_flash_offset':offset,'header_bytes':len(header),
        'header_crc32':struct.unpack_from('<I',header,60)[0],
        'entry':0x1000,'gpu_id':0x47505531,'bul9_id':0x42554C39,'bul9_revision':0x10007,'jedec_id':0xEF6017,
        'entries':entries,'segments':segments,'flash_programmed':False}
    (out/'manifest.json').write_text(json.dumps(manifest,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    return manifest
def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--variant',type=Path)
    p.add_argument('--inputs',type=Path,help='directory containing game.bin and three named assets')
    p.add_argument('--manifest-offset',type=lambda x:int(x,0),default=0x400000)
    p.add_argument('--bootstrap',type=Path)
    p.add_argument('--out',type=Path,required=True)
    a=p.parse_args()
    if bool(a.variant)==bool(a.inputs):p.error('Choose exactly one of --variant or --inputs')
    if a.variant:
        paths=[a.variant/'build/gardenStage9Game.bin']+[a.variant/'assets'/n for n,_,_ in KINDS[1:]]
    else:paths=[a.inputs/n for n,_,_ in KINDS]
    m=pack(paths,a.out,a.manifest_offset,a.bootstrap)
    print(json.dumps({'out':str(a.out.resolve()),'segments':m['segments'],'entries':len(m['entries'])}))
if __name__=='__main__':main()
