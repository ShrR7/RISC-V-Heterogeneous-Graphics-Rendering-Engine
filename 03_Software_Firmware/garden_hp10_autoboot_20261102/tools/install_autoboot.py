"""Install the frozen Garden autoboot image. Default: offline dry run only.

Hardware execution first saves two matching complete Flash reads, overlays only
the reviewed ranges, programs whole sectors, verifies all 8 MiB, then loads the
unchanged FPGA bit over JTAG. It never downloads a game ELF or DDR assets.
"""
from __future__ import annotations
import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import socket
import subprocess
import sys
import zlib

import flash_ops

EXPECTED_CFG_SHA='e588e410c2b0c4068d9a867cbe9445a8ba89d7209c5a363efc2930226277ade8'
EXPECTED_BIT_MD5='d59183e81a58833b1d550a91ebbe5e23'
CAPACITY=0x800000


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def read_json(path):
    return json.loads(Path(path).read_text(encoding='utf-8-sig'))


def inside(root,filename):
    path=(Path(root)/filename).resolve()
    if not path.is_relative_to(Path(root).resolve()):
        raise ValueError('Manifest path escapes bundle: '+str(filename))
    return path


def validate_bundle(bundle):
    bundle=Path(bundle).resolve()
    inputs=bundle/'inputs'
    source=read_json(inputs/'manifest.json')
    if (source.get('kind')!='accepted_game_autoboot_inputs'
            or source.get('default_mode')!='AUTO_game' or not source.get('retain_T_test')):
        raise ValueError('Expected frozen complete-game inputs with AUTO default and T retained')
    files={}
    for entry in source['entries']:
        path=inside(inputs,entry['name'])
        data=path.read_bytes()
        if (len(data)!=entry['bytes'] or hashlib.sha256(data).hexdigest()!=entry['sha256']
                or f'{zlib.crc32(data)&0xffffffff:08x}'!=entry['crc32'].lower().removeprefix('0x')):
            raise ValueError('Frozen input changed: '+entry['name'])
        files[str(path.relative_to(bundle))]=digest(path)
    archive=inputs/'game_source_snapshot.zip'
    if source.get('source_archive') and (archive.stat().st_size!=source['source_archive']['bytes']
            or digest(archive)!=source['source_archive']['sha256']):
        raise ValueError('Frozen game source archive changed')
    if hashlib.md5((inputs/'fpga.bit').read_bytes()).hexdigest()!=EXPECTED_BIT_MD5:
        raise ValueError('Expected accepted D591 FPGA image')
    config=(inputs/'fpga_spi_active.bin').read_bytes()
    if len(config)!=925393 or hashlib.sha256(config).hexdigest()!=EXPECTED_CFG_SHA:
        raise ValueError('Expected officially exported matched active SPI configuration')
    if flash_ops.read_hex(inputs/'fpga.hex')!=config:
        raise ValueError('SPI raw image and paired active HEX disagree')
    loader=bundle/'loader'
    delivery=read_json(loader/'delivery_manifest.json')
    frozen=[]
    for name,entry in delivery['source_files'].items():
        frozen.append((inside(loader,name),entry))
    for name,entry in delivery['copied_frozen_build'].items():
        if not entry.get('identical_to_frozen_origin'):
            raise ValueError('Loader binary copy was not verified against its frozen build')
        frozen.append((inside(loader/'build',name),entry['record']))
    for name,entry in delivery['repacked_segments'].items():
        if not entry.get('identical_to_frozen_origin'):
            raise ValueError('Repacked runtime segment was not verified against its frozen package')
        frozen.append((inside(bundle/'package',name),entry['record']))
    for path,entry in frozen:
        if path.stat().st_size!=entry['bytes'] or digest(path)!=entry['sha256']:
            raise ValueError('Frozen loader/package artifact changed: '+str(path))
        files[str(path.relative_to(bundle))]=digest(path)
    package=bundle/'package'
    packed=read_json(package/'manifest.json')
    if (packed.get('format')!='CGB1' or packed.get('manifest_flash_offset')!=0x400000
            or packed.get('entry')!=0x1000 or packed.get('jedec_id')!=0xef6017
            or packed.get('flash_bytes')!=CAPACITY or packed.get('bul9_revision')!=0x10007):
        raise ValueError('Unexpected package architecture, board or capability')
    spans=[]
    segments=packed['segments']
    if len(segments)!=2 or {s['name'] for s in segments}!={'boot_slot.bin','payload.bin'}:
        raise ValueError('Expected exactly boot_slot.bin and payload.bin')
    for segment in segments:
        path=inside(package,segment['name'])
        data=path.read_bytes()
        address=segment['flash_offset']
        if len(data)!=segment['bytes'] or hashlib.sha256(data).hexdigest()!=segment['sha256']:
            raise ValueError('Packed segment changed: '+segment['name'])
        if address%4096 or not data or address+len(data)>CAPACITY:
            raise ValueError('Invalid package address or capacity')
        if segment['name']=='boot_slot.bin' and (address!=0x380000 or len(data)!=0x1f000):
            raise ValueError('Bootstrap must exactly fill the immutable ROM load slot')
        if segment['name']=='payload.bin' and address!=0x400000:
            raise ValueError('Payload must start at 0x400000')
        if any(address<end and start<address+len(data) for start,end in spans):
            raise ValueError('Overlapping package segments')
        spans.append((address,address+len(data)))
        files[str(path.relative_to(bundle))]=digest(path)
    payload=(package/'payload.bin').read_bytes()
    if payload[:192]!=(package/'header.bin').read_bytes():
        raise ValueError('Resource package header differs from frozen header')
    expected_ddr={'game.bin':0x1000,'atlas.bin':0x5c4000,
                  'stage9_tiles.bin':0x900000,'pure_scatter16.bin':0x9b0000}
    if len(packed['entries'])!=4 or {e['name'] for e in packed['entries']}!=set(expected_ddr):
        raise ValueError('Package must carry all four runtime payloads')
    for entry in packed['entries']:
        data=(inputs/entry['name']).read_bytes()
        offset=entry['flash_offset']-0x400000
        if (entry['ddr_address']!=expected_ddr[entry['name']] or offset<0
                or len(data)!=entry['bytes'] or digest(inputs/entry['name'])!=entry['sha256']
                or payload[offset:offset+len(data)]!=data):
            raise ValueError('Package does not match frozen runtime input: '+entry['name'])
    for path in (inputs/'manifest.json',inputs/'fpga_spi_active.bin',archive,package/'manifest.json',loader/'delivery_manifest.json',
                 bundle/'tools/flash_ops.py',bundle/'tools/assemble_flash.py',Path(__file__).resolve()):
        files[str(path.relative_to(bundle))]=digest(path)
    return dict(bundle=str(bundle),source_variant=source['source_variant'],
                default_mode='AUTO_game',retain_T_test=True,files=files,
                fpga_bit_md5=EXPECTED_BIT_MD5,spi_raw_sha256=EXPECTED_CFG_SHA,
                expected_jedec='ef6017',capacity=CAPACITY)


def refuse_debugger():
    # Do not kill processes based on a port number or an untrusted PID file.
    try:
        with socket.create_connection(('127.0.0.1',13333),timeout=0.25):
            raise RuntimeError('GDB/OpenOCD port 13333 is occupied. Close the known game debugger before Flash installation; no process was killed.')
    except (ConnectionRefusedError,TimeoutError):
        return
    except OSError as error:
        if getattr(error,'winerror',None) in (10061,10060):
            return
        raise


def run_step(state,out,name,argv,env=None):
    log=out/(name+'.log')
    step=dict(name=name,argv=[str(a) for a in argv],log=str(log),status='RUNNING')
    state['steps'].append(step)
    flash_ops.record(out/'install_manifest.json',state)
    print('STEP '+name,flush=True)
    child_env=dict(os.environ if env is None else env)
    child_env.update(PYTHONIOENCODING='utf-8',PYTHONUTF8='1')
    with log.open('w',encoding='utf-8') as stream:
        child=subprocess.Popen(step['argv'],stdout=subprocess.PIPE,stderr=subprocess.STDOUT,
            text=True,encoding='utf-8',errors='replace',env=child_env,shell=False)
        for line in child.stdout:
            stream.write(line);stream.flush()
            print(line,end='',flush=True)
        child.wait()
    step['returncode']=child.returncode
    step['status']='COMPLETE' if child.returncode==0 else 'FAILED'
    flash_ops.record(out/'install_manifest.json',state)
    if child.returncode:
        raise RuntimeError(f'{name} failed with exit {child.returncode}; see {log}')


def execute(args,validation,out):
    bundle=Path(args.bundle).resolve()
    home=Path(args.efinity).resolve()
    refuse_debugger()
    out.mkdir(parents=True,exist_ok=False)
    state=dict(status='RUNNING',started_utc=datetime.now(timezone.utc).isoformat(),
        validation=validation,steps=[],flash_verified=False,
        true_cold_boot_verified=False,game_ELF_downloaded=False,DDR_assets_downloaded=False)
    flash=Path(__file__).resolve().parent/'flash_ops.py'
    common=['--url',args.url,'--efinity',str(home),'--frequency',str(args.frequency),'--execute']
    try:
        backup=out/'backup'
        run_step(state,out,'01_backup',[sys.executable,str(flash),'--operation','backup',
            '--backup-passes','2','--out',str(backup),*common])
        if validate_bundle(bundle)!=validation:
            raise RuntimeError('Bundle changed while backing up; no persistent write attempted')
        assembled=out/'assembled'
        run_step(state,out,'02_assemble',[sys.executable,str(bundle/'tools/assemble_flash.py'),
            '--backup-manifest',str(backup/'manifest.json'),'--package',str(bundle/'package'),
            '--configuration-bin',str(bundle/'inputs/fpga_spi_active.bin'),'--out',str(assembled)])
        merge=read_json(assembled/'manifest.json')
        if (merge.get('capacity')!=CAPACITY or not merge.get('all_other_bytes_preserved')
                or merge.get('flash_chip_erase') or merge.get('expected_jedec')!='ef6017'):
            raise RuntimeError('Assembler did not confirm preserved full image')
        for index,region in enumerate(merge['program_regions']):
            if validate_bundle(bundle)!=validation:
                raise RuntimeError('Frozen bundle changed before programming')
            source=inside(assembled,region['file'])
            if (source.stat().st_size!=region['bytes'] or digest(source)!=region['sha256']
                    or region['address']%4096 or region['bytes']%4096
                    or region['address']<0 or region['address']+region['bytes']>CAPACITY):
                raise RuntimeError('Invalid assembled whole-sector region')
            run_step(state,out,f'03_program_{index+1}',[sys.executable,str(flash),
                '--operation','program','--address',hex(region['address']),'--input',str(source),
                '--backup-manifest',str(backup/'manifest.json'),
                '--out',str(out/f'program_{index+1}'),*common])
        expected=assembled/'expected_flash_8MiB.bin'
        if expected.stat().st_size!=CAPACITY or digest(expected)!=merge['expected_flash_sha256']:
            raise RuntimeError('Expected final image changed before verification')
        run_step(state,out,'04_verify_all',[sys.executable,str(flash),'--operation','verify',
            '--address','0','--input',str(expected),'--out',str(out/'verify_all'),*common])
        state['flash_verified']=True
        if validate_bundle(bundle)!=validation:
            raise RuntimeError('Bundle changed before FPGA restart')
        restart=out/'restart_vendor';restart.mkdir()
        user=restart/'efinity_user';user.mkdir()
        ini=restart/'efinity_user_dir.ini'
        ini.write_text('[General]\nEFINITY_USER_DIR = '+user.as_posix()+'\n',encoding='utf-8')
        env=flash_ops.vendor_environment(home)
        env.update(EFINITY_USER_DIR=str(user),EFINITY_USER_DIR_INI=str(ini))
        run_step(state,out,'05_restart_FPGA',[str(home/'python311/bin/python.exe'),
            str(home/'pgm/bin/efx_pgm/ftdi_program.py'),str(bundle/'inputs/fpga.bit'),
            '-m','jtag','-u',args.url,'--jtag_clock_freq',str(args.frequency)],env)
        state['status']='FLASH_VERIFIED_FPGA_RESTARTED'
        state['note']='No game/asset download occurred; verify UART/HDMI and a true power cycle separately.'
        state['completed_utc']=datetime.now(timezone.utc).isoformat()
        flash_ops.record(out/'install_manifest.json',state)
        print(json.dumps(dict(status=state['status'],manifest=str(out/'install_manifest.json'))),flush=True)
        return 0
    except Exception as error:
        state['status']='FLASH_VERIFIED_RESTART_FAILED' if state['flash_verified'] else 'FAILED'
        state['error']=repr(error)
        flash_ops.record(out/'install_manifest.json',state)
        raise


def main(argv=None):
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--bundle',default=str(Path(__file__).resolve().parent.parent))
    p.add_argument('--url',default='ftdi://0x0403:0x6011:0:ff/1')
    p.add_argument('--efinity',default='D:/Efinity/2026.1')
    p.add_argument('--frequency',type=int,default=3000000)
    p.add_argument('--out',help='New install log directory; default bundle/logs/timestamp')
    p.add_argument('--execute',action='store_true')
    args=p.parse_args(argv)
    if not 100000<=args.frequency<=6000000:
        raise ValueError('JTAG frequency must be within 100000..6000000 Hz')
    validation=validate_bundle(args.bundle)
    home=Path(args.efinity).resolve()
    if not (home/'python311/bin/python.exe').is_file() or not (home/flash_ops.BRIDGE_REL).is_file():
        raise ValueError('Installed Efinity Python and matching Ti60 bridge are required')
    out=Path(args.out).resolve() if args.out else Path(args.bundle).resolve()/'logs'/datetime.now().strftime('%Y%m%d_%H%M%S_%f')
    if out.exists():
        raise ValueError('Install output already exists; select a fresh directory')
    if not args.execute:
        print(json.dumps(dict(status='DRY_RUN_NO_HARDWARE',hardware_access=False,
            persistent_write=False,validation=validation,planned_output=str(out),
            sequence=['two matching 8MiB backups','offline preserving overlay',
                'program changed whole-sector regions','compare entire 8MiB',
                'volatile FPGA JTAG restart without game/asset download'],
            true_cold_boot_verified=False),ensure_ascii=True,indent=2))
        return 0
    return execute(args,validation,out)


if __name__=='__main__':
    if hasattr(sys.stdout,'reconfigure'):
        sys.stdout.reconfigure(errors='replace')
        sys.stderr.reconfigure(errors='replace')
    raise SystemExit(main())
