"""Ti60/Winbond W25Q64 Flash operations using the local Efinity API.

Dry run is the default. The parent owns exclusive hardware access. No vendor
source or bridge image is copied into this delivery. Efinity HEX is one byte
per line, not Intel HEX. Loading the bridge changes volatile FPGA RAM only.
"""
from __future__ import annotations
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys
from types import SimpleNamespace
from datetime import datetime, timezone

CAPACITY = 8 * 1024 * 1024
# Exact device measured by the parent's read-only board probe. The manual's
# GD25LQ64 is a BOM variant; do not whitelist an arbitrary density suffix.
EXPECTED_JEDEC = 'ef6017'
EXPECTED_FLASH_MODEL = 'Winbond W25Q64'
BRIDGE_REL = Path('pgm/fli/titanium/u10660A79.bit')


def sha(data):
    return hashlib.sha256(data).hexdigest()


def read_hex(path):
    lines = Path(path).read_text(encoding='ascii').splitlines()
    if not lines or any(not re.fullmatch(r'[0-9a-fA-F]{2}', line) for line in lines):
        raise ValueError('Expected Efinity one-byte-per-line HEX (not Intel HEX)')
    return bytes(int(line, 16) for line in lines)


def load_input(path):
    path = Path(path)
    return path.read_bytes() if path.suffix.lower() == '.bin' else read_hex(path)


def write_hex(path, data):
    with Path(path).open('w', encoding='ascii', newline='\n') as stream:
        stream.writelines(f'{byte:02X}\n' for byte in data)


def vendor_environment(home):
    env = dict(os.environ)
    env.update(EFINITY_HOME=str(home), EFXPGM_HOME=str(home / 'pgm'),
               EFXDBG_HOME=str(home / 'debugger'), PYTHONHOME=str(home / 'python311'),
               PYTHONPATH=os.pathsep.join((str(home/'pgm/bin'), str(home/'debugger/bin'))))
    return env


def record(path, data):
    Path(path).write_text(json.dumps(data, ensure_ascii=False, indent=2)+'\n', encoding='utf-8')


def make_plan(args):
    home = Path(args.efinity).resolve()
    bridge = home / BRIDGE_REL
    if not bridge.is_file():
        raise ValueError('Matching bundled Ti60 bridge missing: '+str(bridge))
    plan = dict(operation=args.operation, hardware_access=args.execute,
                volatile_fpga_becomes_bridge=args.execute, expected_jtag_id='10660A79',
                bridge_path=str(bridge), bridge_sha256=sha(bridge.read_bytes()),
                expected_jedec=EXPECTED_JEDEC, expected_capacity=CAPACITY,
                expected_flash_model=EXPECTED_FLASH_MODEL,
                url=args.url, address=args.address, length=args.length,
                flash_persistent_write=args.operation == 'program' and args.execute,
                flash_erase_entire=False, output_dir=str(Path(args.out).resolve()),
                backup_passes=args.backup_passes, chunk_bytes=args.chunk_bytes,
                jtag_frequency=args.frequency)
    if args.chunk_bytes < 4096 or args.chunk_bytes > 65536 or args.chunk_bytes % 4096:
        raise ValueError('Chunk size must be a 4 KiB multiple within 4096..65536')
    if args.operation in ('read','verify') and not (0 <= args.address < CAPACITY and 0 < args.length <= CAPACITY-args.address):
        raise ValueError('Read range outside 8 MiB Flash')
    if args.operation in ('program','verify'):
        if not args.input:
            raise ValueError('--input is required')
        data = load_input(args.input)
        if not data or not (0 <= args.address < CAPACITY) or len(data) > CAPACITY-args.address:
            raise ValueError('Input outside 8 MiB Flash')
        if args.operation == 'program' and args.address % 4096:
            raise ValueError('Program address must be 4 KiB aligned')
        if args.operation == 'program' and len(data) % 4096:
            raise ValueError('Program input must contain complete 4 KiB sectors; merge tail bytes from backup first')
        if args.operation == 'program' and Path(args.input).suffix.lower() not in ('.bin','.hex'):
            raise ValueError('Program accepts .bin or SPI .hex, never a JTAG .bit input')
        plan.update(input_path=str(Path(args.input).resolve()), input_bytes=len(data),
                    input_sha256=sha(data), erase_start=args.address,
                    erase_end=(args.address+len(data)+4095)//4096*4096)
        if args.operation == 'verify':
            plan['length'] = len(data)
        if args.operation == 'program':
            if not args.backup_manifest:
                raise ValueError('Program requires --backup-manifest from a completed backup')
            backup = json.loads(Path(args.backup_manifest).read_text(encoding='utf-8-sig'))
            original = Path(backup['backup_binary'])
            if (backup.get('status') != 'BACKUP_COMPLETE' or backup.get('jedec') != EXPECTED_JEDEC
                    or backup.get('capacity') != CAPACITY or not original.is_file()
                    or original.stat().st_size != CAPACITY or sha(original.read_bytes()) != backup['backup_sha256']):
                raise ValueError('Original full Flash backup missing, incomplete, or hash mismatch')
            plan['backup_manifest'] = str(Path(args.backup_manifest).resolve())
    return plan


def get_vendor_program(args):
    # These imports occur only inside the explicitly executed vendor child.
    from efx_pgm.ftdi_program import FtdiProgram, configure_jtag_session, set_up_urls
    from efx_pgm.usb_resolver import UsbResolver
    config = SimpleNamespace(input_file=args.input or '', mode='jtag_bridge',
        output_file='', urls=[], num='1', address=0, num_bytes=256,
        jtag_bridge_mode='read', jtag_clock_freq=args.frequency, spi_active_freq=args.frequency)
    prog = FtdiProgram(config)
    connections = UsbResolver().get_usb_connections()
    if not connections:
        raise RuntimeError('No FTDI target enumerated')
    prog.urls = set_up_urls(prog, connections, args.url, '')
    prog.jtag_session_url = configure_jtag_session(prog)
    return prog


def read_range(prog, address, length, out, name, chunk_bytes=65536):
    """Read bounded transactions; save and identify every completed chunk."""
    folder = Path(out)/name
    folder.mkdir()
    parts, entries = [], []
    for offset in range(0,length,chunk_bytes):
        count = min(chunk_bytes,length-offset)
        position = address+offset
        stem = f'{position:06X}_{count:05X}'
        encoded = folder/(stem+'.hex')
        print(f'{name}: reading 0x{position:06X} + {count} bytes ({offset+count}/{length})',flush=True)
        jedec = bytes(prog.jtag2spi_read_flash(position,count,str(encoded),True,'lower')).hex()
        if jedec != EXPECTED_JEDEC:
            raise RuntimeError('Flash ID changed during chunk read')
        data = read_hex(encoded)
        if len(data) != count:
            raise RuntimeError('Chunk read length mismatch')
        binary = folder/(stem+'.bin')
        binary.write_bytes(data)
        parts.append(data)
        entries.append(dict(address=position,bytes=count,sha256=sha(data),
                            jedec=jedec,hex=str(encoded),binary=str(binary)))
        record(folder/'progress.json',dict(status='READING',expected_bytes=length,
            completed_bytes=offset+count,chunks=entries))
    merged = b''.join(parts)
    record(folder/'progress.json',dict(status='READ_COMPLETE',expected_bytes=length,
        completed_bytes=len(merged),sha256=sha(merged),chunks=entries))
    return merged,entries


def vendor_worker(args, plan):
    out = Path(args.out).resolve()
    out.mkdir(parents=True, exist_ok=False)
    # The official CLI and its JTAG daemon require these settings. Keep them
    # entirely inside this new evidence directory and this vendor child.
    user_dir = out/'efinity_user'
    user_dir.mkdir()
    user_ini = out/'efinity_user_dir.ini'
    user_ini.write_text('[General]\nEFINITY_USER_DIR = '+user_dir.as_posix()+'\n',encoding='utf-8')
    os.environ['EFINITY_USER_DIR_INI'] = str(user_ini)
    os.environ['EFINITY_USER_DIR'] = str(user_dir)
    plan['isolated_efinity_user_dir'] = str(user_dir)
    plan['isolated_efinity_user_dir_ini'] = str(user_ini)
    record(out/'plan.json', plan)
    result = dict(started_utc=datetime.now(timezone.utc).isoformat(), **plan)
    try:
        # Official CLI configures a bundled bridge first. API setup() is a no-op.
        home = Path(args.efinity).resolve()
        command = [sys.executable, str(home/'pgm/bin/efx_pgm/ftdi_program.py'),
                   str(home/BRIDGE_REL), '-m','jtag','-u',args.url,
                   '--jtag_clock_freq',str(args.frequency)]
        print('Loading volatile JTAG Bridge for read/program access', flush=True)
        with (out/'bridge_program.log').open('w',encoding='utf-8') as log:
            child = subprocess.run(command,stdout=log,stderr=subprocess.STDOUT,timeout=300)
        if child.returncode:
            raise RuntimeError('Bridge configuration failed; see bridge_program.log')
        prog = get_vendor_program(args)
        probe = out/'probe.hex'
        jedec = bytes(prog.jtag2spi_read_flash(0,256,str(probe),True,'lower')).hex()
        result['jedec'] = jedec
        print('JEDEC '+jedec, flush=True)
        if jedec != EXPECTED_JEDEC:
            raise RuntimeError('Unexpected Flash ID; abort before full read or persistent write')
        result['capacity'] = CAPACITY
        result['flash_model'] = EXPECTED_FLASH_MODEL
        if args.operation == 'probe':
            result['status'] = 'PROBE_COMPLETE'
        elif args.operation == 'backup':
            image = None
            passes = []
            for index in range(args.backup_passes):
                print(f'Full read pass {index+1}/{args.backup_passes}: {CAPACITY} bytes', flush=True)
                data,chunks = read_range(prog,0,CAPACITY,out,f'backup_pass_{index+1}',args.chunk_bytes)
                if len(data) != CAPACITY or data[:256] != read_hex(probe):
                    raise RuntimeError('Backup length/probe mismatch')
                if image is not None and image != data:
                    raise RuntimeError('Full backup passes differ')
                image = data
                pass_binary = out/f'flash_read_{index+1}.bin'
                pass_binary.write_bytes(data)
                passes.append(dict(pass_index=index+1,binary=str(pass_binary),sha256=sha(data),chunks=chunks))
            target = out/'original_flash_8MiB.bin'
            target.write_bytes(image)
            result.update(status='BACKUP_COMPLETE', backup_binary=str(target),
                          backup_sha256=sha(image), passes_matching=args.backup_passes,
                          read_passes=passes)
        elif args.operation in ('read','verify'):
            length = plan['length']
            expected = load_input(args.input) if args.operation == 'verify' else None
            if expected is not None and sha(expected) != plan['input_sha256']:
                raise RuntimeError('Verification input changed after planning')
            data,chunks = read_range(prog,args.address,length,out,'readback_chunks',args.chunk_bytes)
            if len(data) != length:
                raise RuntimeError('Readback identity or length mismatch')
            (out/'readback.bin').write_bytes(data)
            result.update(readback_bytes=len(data),readback_sha256=sha(data),readback_chunks=chunks)
            if expected is not None and data != expected:
                raise RuntimeError('Readback differs from input')
            result['status'] = 'VERIFY_COMPLETE' if args.operation == 'verify' else 'READ_COMPLETE'
        elif args.operation == 'program':
            # Exact binary snapshot prevents mutation of input between plan/write.
            data = load_input(args.input)
            if sha(data) != plan['input_sha256']:
                raise RuntimeError('Input changed after planning')
            programmed = []
            for offset in range(0,len(data),args.chunk_bytes):
                part = data[offset:offset+args.chunk_bytes]
                position = args.address+offset
                encoded = out/f'program_{position:06X}.hex'
                write_hex(encoded,part)
                prog.input_file = str(encoded)
                print(f'Program {len(part)} bytes at 0x{position:06X}; per-range erase; host readback verify',flush=True)
                again = bytes(prog.jtag2spi_program(True,position,False,True,'lower','hostx1')).hex()
                if again != jedec:
                    raise RuntimeError('Flash ID changed during programming')
                programmed.append(dict(address=position,bytes=len(part),sha256=sha(part)))
                record(out/'program_progress.json',dict(status='PROGRAMMING',chunks=programmed))
            copied,chunks = read_range(prog,args.address,len(data),out,'readback_chunks',args.chunk_bytes)
            if copied != data:
                raise RuntimeError('Independent final byte comparison failed')
            (out/'readback.bin').write_bytes(copied)
            result.update(status='PROGRAM_VERIFIED',readback_sha256=sha(copied),
                          program_chunks=programmed,readback_chunks=chunks)
        result['completed_utc'] = datetime.now(timezone.utc).isoformat()
        record(out/'manifest.json',result)
        print(json.dumps({'status':result['status'],'manifest':str(out/'manifest.json')},ensure_ascii=True),flush=True)
        return 0
    except Exception as error:
        result.update(status='FAILED', error=repr(error))
        record(out/'manifest.json',result)
        raise


def parser():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--operation',choices=('probe','backup','read','verify','program'),required=True)
    p.add_argument('--url',required=True,help='Explicit FTDI target URL; no default target')
    p.add_argument('--out',required=True,help='A new evidence directory')
    p.add_argument('--efinity',default='D:/Efinity/2026.1')
    p.add_argument('--frequency',type=int,default=6000000)
    p.add_argument('--address',type=lambda x:int(x,0),default=0)
    p.add_argument('--length',type=lambda x:int(x,0),default=256)
    p.add_argument('--backup-passes',type=int,choices=(1,2),default=2)
    p.add_argument('--chunk-bytes',type=lambda x:int(x,0),default=65536)
    p.add_argument('--input')
    p.add_argument('--backup-manifest')
    p.add_argument('--execute',action='store_true')
    p.add_argument('--vendor-worker',action='store_true',help=argparse.SUPPRESS)
    return p


def main(argv=None):
    args = parser().parse_args(argv)
    plan = make_plan(args)
    if not args.execute:
        print(json.dumps(plan,ensure_ascii=True,indent=2))
        return 0
    if Path(args.out).exists():
        raise ValueError('Output already exists; use a fresh evidence directory')
    if args.vendor_worker:
        return vendor_worker(args,plan)
    home = Path(args.efinity).resolve()
    python = home/'python311/bin/python.exe'
    arguments = list(sys.argv[1:] if argv is None else argv)
    child = subprocess.run([str(python),str(Path(__file__).resolve()),*arguments,'--vendor-worker'],
                           env=vendor_environment(home))
    return child.returncode


if __name__ == '__main__':
    raise SystemExit(main())
