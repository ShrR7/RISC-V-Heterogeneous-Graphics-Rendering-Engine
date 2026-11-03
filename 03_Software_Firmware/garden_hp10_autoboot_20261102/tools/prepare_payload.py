"""Prepare an independent Joy-Con game boot bundle. Never opens hardware.

Default is a read-only plan. Execute only after root identifies the final build
and supplies its ELF, binary and source fingerprints. The accepted Garden
bundle, and 03_Software_Firmware/joycon2_dual_bridge, are never changed by this script.
"""
from __future__ import annotations
import argparse
from datetime import datetime, timezone
import hashlib
import importlib.util
import json
from pathlib import Path
import runpy
import shutil
import struct
import sys
import zipfile
import zlib

sys.dont_write_bytecode = True
ROOT = next(p for p in Path(__file__).resolve().parents if (p/'01_Hardware_Platform').is_dir())
PROJECT = ROOT/'01_Hardware_Platform/08_ti60f225_soc_demo/09_Ti60F225_hardjtag_demo/par/ddr_demo_ti60'
APP = PROJECT/'embedded_sw/soc/software/standalone/application/gpuGardenDemo'
BASE = ROOT/'03_Software_Firmware/garden_cboss_autoboot_20261102'
DEFAULT_VARIANT = APP/'variants/chp10_1102'
DEFAULT_OUT = ROOT/'03_Software_Firmware/garden_hp10_autoboot_20261102'
BIT_MD5 = 'd59183e81a58833b1d550a91ebbe5e23'
BOOT_SHA = 'daaaddab6b275694085571d63bf97531e42709a43a85236309f51937c7ec5bf6'
ASSETS = (('atlas.bin',0x5c4000),('stage9_tiles.bin',0x900000),('pure_scatter16.bin',0x9b0000))


def sha(data): return hashlib.sha256(data).hexdigest()
def record(data):
    return {'bytes':len(data),'sha256':sha(data),'crc32':f'{zlib.crc32(data)&0xffffffff:08x}'}
def read_json(path): return json.loads(path.read_text(encoding='utf-8-sig'))
def write_json(path,value): path.write_text(json.dumps(value,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
def load_module(name,path):
    spec=importlib.util.spec_from_file_location(name,path)
    module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
    return module


def inspect_game(elf, binary):
    if len(elf)<52 or elf[:7]!=b'\x7fELF\x01\x01\x01':
        raise ValueError('Expected ELF32 little-endian firmware')
    h=struct.unpack_from('<HHIIIIIHHHHHH',elf,16)
    if h[1]!=243 or h[3]!=0x1000 or h[8]!=32:
        raise ValueError('Expected RV32 game with entry 0x1000 and standard program headers')
    loads=[]
    for index in range(h[9]):
        offset=h[4]+index*h[8]
        if offset+32>len(elf): raise ValueError('Truncated ELF program table')
        q=struct.unpack_from('<8I',elf,offset)
        if q[0]!=1: continue
        _,file_offset,vaddr,paddr,filesz,memsz,flags,align=q
        if (vaddr!=paddr or filesz>memsz or file_offset+filesz>len(elf)
                or not 0x1000<=vaddr<0x20000 or vaddr+memsz>0x20000):
            raise ValueError('Game LOAD/BSS/stack must remain in DDR 0x1000..0x20000')
        loads.append(dict(file_offset=file_offset,address=vaddr,filesz=filesz,memsz=memsz,flags=flags))
    if not loads or not 0<len(binary)<=0x1f000:
        raise ValueError('Game does not fit the original 124KiB reserved window')
    file_loads=[q for q in loads if q['filesz']]
    first=min(q['address'] for q in file_loads)
    end=max(q['address']+q['filesz'] for q in file_loads)
    reconstructed=bytearray(end-first)
    for q in file_loads:
        begin=q['address']-first
        reconstructed[begin:begin+q['filesz']]=elf[q['file_offset']:q['file_offset']+q['filesz']]
    if first!=0x1000 or bytes(reconstructed)!=binary:
        raise ValueError('game.bin is not the exact address-ordered file image of game.elf')
    return {'entry':0x1000,'load_ranges':loads,'memory_end':max(q['address']+q['memsz'] for q in loads),
            'binary_bytes':len(binary),'stack_and_BSS_envelope_checked':True}


def source_snapshot(variant,build):
    paths=set()
    paths.update(p.resolve() for p in (variant/'src').rglob('*') if p.is_file() and '__pycache__' not in p.parts)
    paths.update(p.resolve() for p in (variant/'tools').iterdir()
                 if p.is_file() and (p.suffix.lower() in ('.py','.ps1') or p.name.startswith('Makefile')))
    for relative in build['source_files']:
        p=(APP/relative).resolve()
        if not p.is_file() or not p.is_relative_to(ROOT): raise ValueError('Source missing or outside workspace: '+str(p))
        paths.add(p)
    paths.add((variant/'build/build_manifest.json').resolve())
    for p in (APP/'vendor/garden-1.0.9/COPYING',):
        if p.is_file(): paths.add(p.resolve())
    blobs={p.relative_to(ROOT).as_posix():p.read_bytes() for p in sorted(paths)}
    tree={name:record(data) for name,data in blobs.items()}
    fingerprint=sha(json.dumps(tree,sort_keys=True,separators=(',',':')).encode('utf-8'))
    return blobs,tree,fingerprint


def copy_exact(source,destination):
    destination.parent.mkdir(parents=True,exist_ok=True)
    shutil.copyfile(source,destination)
    if source.read_bytes()!=destination.read_bytes(): raise ValueError('Copy differs: '+str(source))


def predict_package(base, binary, assets):
    """Use the existing pure header encoder, then compare complete sectors."""
    packer=load_module('planning_packer',base/'loader/pack.py')
    cursor=0x401000;entries=[];data=[binary]+[assets[n] for n,_ in ASSETS]
    destinations=[0x1000]+[address for _,address in ASSETS]
    names=['game.bin']+[n for n,_ in ASSETS]
    for kind,(name,address,blob) in enumerate(zip(names,destinations,data),1):
        cursor=(cursor+4095)&~4095
        entries.append(dict(kind=kind,name=name,flash_offset=cursor,ddr_address=address,
                            bytes=len(blob),crc32=zlib.crc32(blob)&0xffffffff,sha256=sha(blob)))
        cursor+=len(blob)
    payload=bytearray(b'\xff'*(((cursor+4095)&~4095)-0x400000))
    header=packer.make_header(entries,0x400000);payload[:len(header)]=header
    for entry,blob in zip(entries,data):
        begin=entry['flash_offset']-0x400000;payload[begin:begin+len(blob)]=blob
    old=(base/'package/payload.bin').read_bytes()
    # Optional delta is valid only when the current Flash package equals this
    # accepted old payload. A fresh board read must establish that separately.
    new=bytes(payload)
    same_extent=len(old)==len(new)
    changed=[offset for offset in range(0,len(new),4096)
             if not same_extent or old[offset:offset+4096]!=new[offset:offset+4096]]
    ranges=[]
    for offset in changed:
        if ranges and ranges[-1][1]==offset:ranges[-1][1]+=4096
        else:ranges.append([offset,offset+4096])
    return bytes(payload),dict(manifest_flash_offset=0x400000,bytes=len(payload),sha256=sha(payload),
        header_sha256=sha(header),entries=entries,
        delta_requires_current_flash_payload_sha256=sha(old),
        delta_requires_fresh_board_read=True,
        delta_mode='changed_sectors_same_extent' if same_extent else 'full_new_payload_extent_changed',
        changed_sector_count=len(changed),changed_sector_bytes=4096*len(changed),
        changed_ranges=[dict(address=0x400000+s,bytes=e-s,sha256=sha(new[s:e])) for s,e in ranges])


def main(argv=None):
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--variant',type=Path,default=DEFAULT_VARIANT)
    p.add_argument('--base-bundle',type=Path,default=BASE)
    p.add_argument('--out',type=Path,default=DEFAULT_OUT)
    p.add_argument('--final-build-id')
    p.add_argument('--expected-elf-sha256')
    p.add_argument('--expected-bin-sha256')
    p.add_argument('--expected-source-fingerprint')
    p.add_argument('--execute',action='store_true')
    a=p.parse_args(argv);variant=a.variant.resolve();base=a.base_bundle.resolve();out=a.out.resolve()
    forbidden=ROOT/'03_Software_Firmware/joycon2_dual_bridge'
    if (not out.is_relative_to(ROOT/'program') or out==base or out in base.parents or base in out.parents
            or out==forbidden or out in forbidden.parents or forbidden in out.parents):
        raise ValueError('Output must be a fresh separate directory in 03_Software_Firmware/, outside both protected bundles')
    if out.exists(): raise ValueError('Output already exists; refusing to overwrite it')
    if not variant.is_relative_to(APP/'variants'): raise ValueError('Expected isolated game variant')
    sys.path.insert(0,str(base/'tools'))
    installer=load_module('base_install_autoboot',base/'tools/install_autoboot.py')
    base_validation=installer.validate_bundle(base)
    elf=(variant/'build/gardenStage9Game.elf').read_bytes()
    binary=(variant/'build/gardenStage9Game.bin').read_bytes()
    build=read_json(variant/'build/build_manifest.json')
    if build['variant']!=variant.name or sha(elf)!=build['elf_sha256'].lower():
        raise ValueError('ELF does not match the current variant build manifest')
    memory=inspect_game(elf,binary)
    blobs,tree,source_fp=source_snapshot(variant,build)
    for expected,actual,label in ((a.expected_elf_sha256,sha(elf),'ELF'),
                                  (a.expected_bin_sha256,sha(binary),'BIN'),
                                  (a.expected_source_fingerprint,source_fp,'source tree')):
        if expected and expected.lower()!=actual: raise ValueError('Expected final '+label+' identity changed')
    boot=(base/'package/boot_slot.bin').read_bytes()
    if sha(boot)!=BOOT_SHA or hashlib.md5((base/'inputs/fpga.bit').read_bytes()).hexdigest()!=BIT_MD5:
        raise ValueError('Expected the accepted unchanged FPGA and relock-enabled bootstrap')
    assets={}
    for name,address in ASSETS:
        data=(variant/'assets'/name).read_bytes()
        if name=='atlas.bin':
            if (len(data)!=755700 or sha(data)!='48e6a628e02d68f1bbd86be37c85dcccd3df800d6c79aa3e58ccd5571a56b867'
                    or address+len(data)>0x67D000):
                raise ValueError('Expected the pinned CBoss atlas below runtime batch-list boundary')
        elif data!=(base/'inputs'/name).read_bytes():
            raise ValueError('Stage9/scatter asset changed relative to accepted package: '+name)
        assets[name]=data
    predicted_payload,predicted=predict_package(base,binary,assets)
    identity=dict(final_build_id=a.final_build_id,variant=variant.name,
        elf_sha256=sha(elf),bin_sha256=sha(binary),source_fingerprint=source_fp,
        memory=memory,log_runtime_region={'address':0xbb0000,'bytes':3712,'guard_bytes':16,
          'additional_flash_segment':False,'rationale':'low-DDR BSS initializes indices/stats; queue/format data are written before used'},
        hardware_access=False,flash_write=False,unchanged_fpga_md5=BIT_MD5,unchanged_boot_slot_sha256=BOOT_SHA)
    if not a.execute:
        print(json.dumps(dict(status='PLAN_ONLY_NOT_FROZEN',out=str(out),identity=identity,
            base_guard_files=len(base_validation['files']),
            predicted_package=predicted,
            required_for_execute=['final-build-id','expected-elf-sha256','expected-bin-sha256','expected-source-fingerprint'],
            pending_hardware_acceptance=True),ensure_ascii=True,indent=2))
        return 0
    if (not a.final_build_id or len(a.final_build_id)>128
            or not all((a.expected_elf_sha256,a.expected_bin_sha256,a.expected_source_fingerprint))):
        raise ValueError('Execute requires an explicit final-build-id and all three exact final fingerprints')
    # Nothing above this point writes a file or opens a device.
    out.mkdir(parents=True)
    for folder in ('loader','tools'):
        shutil.copytree(base/folder,out/folder,ignore=shutil.ignore_patterns('__pycache__','*.pyc','logs'))
    inputs=out/'inputs';inputs.mkdir()
    replace={'game.bin','game.elf','game_build_manifest.json','manifest.json','game_source_snapshot.zip'}
    for source in (base/'inputs').iterdir():
        if source.is_file() and source.name not in replace: copy_exact(source,inputs/source.name)
    for name in ('.gitignore','.gitattributes','固化Garden.ps1'):
        if (base/name).is_file(): copy_exact(base/name,out/name)
    runtime_inputs=[('game.bin',binary,0x1000,str((variant/'build/gardenStage9Game.bin').relative_to(ROOT))),
                    ('game.elf',elf,None,str((variant/'build/gardenStage9Game.elf').relative_to(ROOT)))]
    runtime_inputs += [(name,data,address,str((variant/'assets'/name).relative_to(ROOT))) for (name,address),data in zip(ASSETS,assets.values())]
    for name,data,address,source in runtime_inputs: (inputs/name).write_bytes(data)
    (inputs/'game_build_manifest.json').write_bytes((variant/'build/build_manifest.json').read_bytes())
    with zipfile.ZipFile(inputs/'game_source_snapshot.zip','w',compression=zipfile.ZIP_DEFLATED) as z:
        for name,data in sorted(blobs.items()):
            item=zipfile.ZipInfo(name,date_time=(2026,10,31,0,0,0));item.compress_type=zipfile.ZIP_DEFLATED
            item.external_attr=0o100644<<16;z.writestr(item,data)
    entries=[]
    for name,data,address,source in runtime_inputs: entries.append(dict(name=name,source=source,ddr_address=address,**record(data)))
    for name in ('fpga.bit','fpga.hex','game_build_manifest.json'):
        entries.append(dict(name=name,source='frozen matching configuration' if name.startswith('fpga') else str((variant/'build/build_manifest.json').relative_to(ROOT)),
                            ddr_address=None,**record((inputs/name).read_bytes())))
    write_json(inputs/'manifest.json',dict(schema=1,kind='accepted_game_autoboot_inputs',source_variant=variant.name,
        default_mode='AUTO_game',retain_T_test=True,source_game_elf_sha256=sha(elf),fpga_bit_md5=BIT_MD5,
        entries=entries,source_archive=record((inputs/'game_source_snapshot.zip').read_bytes()),
        final_build_identity=identity,final_source_files=tree,original_sources_modified=False,
        flash_offsets=None,flash_programmed=False,hardware_acceptance='PENDING_FOR_THIS_NEW_BUILD'))
    packer=load_module('new_bundle_packer',out/'loader/pack.py')
    paths=[inputs/'game.bin']+[inputs/name for name,_ in ASSETS]
    package=packer.pack(paths,out/'package',0x400000,out/'loader/build/boot_slot.bin')
    if (out/'package/payload.bin').read_bytes()!=predicted_payload:
        raise ValueError('Actual packed bytes differ from the reviewed read-only package plan')
    # Freeze a NEW package generation; never claim its header/payload equal the old game package.
    delivery=read_json(out/'loader/delivery_manifest.json')
    delivery['loader_firmware_origin']=delivery.get('origin')
    delivery['runtime_package_origin']=dict(final_build_identity=identity,
        method='CGB1 regenerated from newly frozen final runtime inputs; payload bytes independently compared below',
        original_package_payload_sha256=sha((base/'package/payload.bin').read_bytes()))
    for name in ('boot_slot.bin','payload.bin','header.bin'):
        data=(out/'package'/name).read_bytes()
        delivery['repacked_segments'][name]={'record':record(data),
            'identical_to_frozen_origin':True,'frozen_origin_kind':'this_final_build_package_generation',
            'matches_previously_accepted_package':data==(base/'package'/name).read_bytes()}
    # Re-run the unchanged loader/ELF/package artifact checks against THIS new
    # runtime generation instead of copying an old game's artifact PASS report.
    sys.path.insert(0,str(out/'loader'))
    try:
        runpy.run_path(str(out/'loader/audit_artifacts.py'),run_name='__main__')
    finally:
        sys.path.pop(0)
    delivery['artifact_review']=record((out/'loader/artifact_review.json').read_bytes())
    write_json(out/'loader/delivery_manifest.json',delivery)
    new_installer=load_module('new_bundle_installer',out/'tools/install_autoboot.py')
    checked=new_installer.validate_bundle(out)
    for name,address in ASSETS:
        if name=='atlas.bin':
            data=(inputs/name).read_bytes()
            assert len(data)==755700 and sha(data)=='48e6a628e02d68f1bbd86be37c85dcccd3df800d6c79aa3e58ccd5571a56b867'
            assert address+len(data)<=0x67D000
        else:
            assert (inputs/name).read_bytes()==(base/'inputs'/name).read_bytes()
    assert (out/'package/boot_slot.bin').read_bytes()==boot
    payload=(out/'package/payload.bin').read_bytes()
    header=bytearray((out/'package/header.bin').read_bytes());stored=struct.unpack_from('<I',header,60)[0]
    struct.pack_into('<I',header,60,0)
    assert zlib.crc32(header)&0xffffffff==stored
    for entry in package['entries']:
        data=(inputs/entry['name']).read_bytes();begin=entry['flash_offset']-0x400000
        assert payload[begin:begin+len(data)]==data and zlib.crc32(data)&0xffffffff==entry['crc32']
    # Base and all supplied final sources must still be byte-identical after preparation.
    assert installer.validate_bundle(base)==base_validation
    assert (variant/'build/gardenStage9Game.elf').read_bytes()==elf
    assert (variant/'build/gardenStage9Game.bin').read_bytes()==binary
    assert source_snapshot(variant,build)[2]==source_fp
    records=out/'records';records.mkdir()
    delta=out/'package/delta';delta.mkdir()
    for index,region in enumerate(predicted['changed_ranges']):
        begin=region['address']-0x400000
        data=predicted_payload[begin:begin+region['bytes']]
        if len(data)!=region['bytes']:
            raise ValueError('Invalid complete-sector delta span')
        filename=f'payload_{index+1:02d}_{region["address"]:06X}.bin'
        (delta/filename).write_bytes(data);region['file']='package/delta/'+filename
    write_json(delta/'manifest.json',dict(status='OFFLINE_DELTA_NOT_PROGRAMMED',**predicted,
        warning='Before writing any delta, fresh board read must equal the exact accepted old payload; this script has not checked a board.'))
    write_json(records/'offline_bundle_validation.json',dict(status='PASS',hardware_access=False,
        final_build_identity=identity,validated_files=checked['files'],
        hardware_acceptance='PENDING_FOR_THIS_NEW_BUILD'))
    result=dict(status='PREPARED_NOT_FLASH_PROGRAMMED_NOT_ACCEPTED',created_utc=datetime.now(timezone.utc).isoformat(),
        identity=identity,installer_guard_files=len(checked['files']),hardware_access=False,
        flash_programmed=False,cold_start_accepted=False,performance_accepted=False,
        old_acceptance_records_copied=False,base_bundle_unchanged=True,
        package_sha256=sha(payload),package_bytes=len(payload),
        payload_delta_plan='package/delta/manifest.json',
        minimal_persistent_change='CGB1 payload/header region only; FPGA and boot_slot unchanged',
        inherited_host_tests='unchanged loader/tool tests are historical; no new board claim')
    write_json(records/'preparation.json',result)
    (out/'README.txt').write_text('Joy-Con UI game autoboot candidate\n\n'+
        'Prepared from final build '+a.final_build_id+'. Not programmed or board-accepted by this script.\n'+
        'Original 03_Software_Firmware/garden_autoboot and 03_Software_Firmware/joycon2_dual_bridge are unchanged.\n'+
        'FPGA D591 and relock bootstrap are unchanged; AUTO game default and T entry remain.\n'+
        'New game, CGB1 section sizes/offsets/CRCs, source snapshot and input guards were regenerated.\n'+
        'Do not reuse the old game\'s cold-start/performance acceptance for this new build.\n'+
        'records/preparation.json contains exact final identities. Hardware acceptance is a separate step.\n',encoding='utf-8')
    copy_exact(Path(__file__).resolve(),out/'tools/prepare_payload.py')
    files=[]
    for path in sorted(out.rglob('*')):
        if path.is_file() and not {'__pycache__','logs'}&set(path.relative_to(out).parts):
            files.append(dict(path=path.relative_to(out).as_posix(),**record(path.read_bytes())))
    write_json(records/'bundle_manifest.json',dict(status=result['status'],hardware_goal_complete=False,files=files,file_count=len(files)))
    print(json.dumps(result,ensure_ascii=True,indent=2))
    return 0


if __name__=='__main__':
    raise SystemExit(main())
