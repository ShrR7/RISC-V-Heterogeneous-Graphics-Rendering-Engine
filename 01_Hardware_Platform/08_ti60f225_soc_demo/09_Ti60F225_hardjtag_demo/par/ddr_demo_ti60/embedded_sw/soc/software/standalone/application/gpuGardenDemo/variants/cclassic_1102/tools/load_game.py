"""Verified RAM-only game loader, COM11 / USB1-10 / private GDB14333.

The PowerShell entry establishes PnP binding and owns OpenOCD. This helper
validates and stages the release files, preserves existing fault evidence,
loads only RAM, and records fresh-start UART checks. It never programs Flash.
"""
from pathlib import Path
from datetime import datetime, timezone
import argparse
import ctypes
import ctypes.wintypes as wt
import hashlib
import json
import re
import shutil
import struct
import subprocess
import sys
import time
import zlib

INPUTS = {
    'build/gardenStage9Game.elf': ('game.elf', None),
    'assets/atlas.bin': ('atlas.bin', 0x005C4000),
    'assets/stage9_tiles.bin': ('stage9_tiles.bin', 0x00900000),
    'assets/pure_scatter16.bin': ('pure_scatter16.bin', 0x009B0000),
}
REQUIRED_INPUTS = set(INPUTS)
TARGET = dict(uart='COM11', physical_usb='1-10', gdb_port=14333,
              telnet_port=14444, tcl_port=14666, protected_uart='COM25',
              protected_physical_usb='1-7', flash_written=False)


def sha(data):
    return hashlib.sha256(data).hexdigest()


def read_json(path):
    return json.loads(path.read_text(encoding='utf-8-sig'))


def save(path, value):
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2), encoding='utf-8')


def repo_of(path):
    for item in [path, *path.parents]:
        if (item / '.git').exists():
            return item
    raise ValueError('Variant is not inside a Git checkout')


def elf_check(data):
    if data[:7] != b'\x7fELF\x01\x01\x01' or len(data) < 52:
        raise ValueError('Expected a 32-bit little-endian ELF')
    machine = struct.unpack_from('<H', data, 18)[0]
    entry, phoff = struct.unpack_from('<II', data, 24)
    phentsize, phnum = struct.unpack_from('<HH', data, 42)
    if machine != 243 or entry != 0x1000 or phentsize < 32:
        raise ValueError('Unexpected ELF architecture or entry point')
    loads = []
    for index in range(phnum):
        values = struct.unpack_from('<8I', data, phoff + index * phentsize)
        kind, offset, address, physical, filesz, memsz, flags, align = values
        if kind == 1:
            if not (0x1000 <= address < address + memsz <= 0x20000 and physical == address and filesz <= memsz and offset + filesz <= len(data)):
                raise ValueError('ELF load segment exceeds permitted program RAM')
            loads.append(dict(address=address, bytes=filesz, memory_bytes=memsz))
    if not loads:
        raise ValueError('No ELF RAM load segment')
    return dict(entry=entry, loads=loads)


def validate(variant):
    manifest_path = variant / 'runtime_inputs.json'
    if not manifest_path.is_file():
        raise FileNotFoundError('Missing runtime_inputs.json; use the complete matching release')
    manifest = read_json(manifest_path)
    if manifest.get('schema') != 1 or manifest.get('variant') != 'cclassic_1102':
        raise ValueError('Unexpected runtime manifest schema/variant')
    rows = manifest.get('files', [])
    listed = {r.get('path') for r in rows}
    if len(rows) != len(listed) or listed != REQUIRED_INPUTS:
        raise ValueError('Manifest must identify exactly the ELF and three approved runtime blobs')
    records = []
    for row in rows:
        relative = row['path']; path = (variant / relative).resolve()
        if not path.is_relative_to(variant):
            raise ValueError('Input escaped the variant')
        data = path.read_bytes()
        if not re.fullmatch(r'[0-9a-fA-F]{64}', row.get('sha256', '')) or sha(data) != row['sha256'].lower() or len(data) != row['bytes']:
            raise ValueError('Release hash/length mismatch: ' + relative)
        name, address = INPUTS[relative]
        records.append(dict(path=relative, name=name, address=address, bytes=len(data), sha256=sha(data), crc32=zlib.crc32(data)))
        if address is None:
            elf = elf_check(data)
    build = read_json(variant / 'build/build_manifest.json')
    if build.get('variant') != 'cclassic_1102' or build.get('elf_sha256', '').lower() != next(r['sha256'] for r in records if r['name'] == 'game.elf'):
        raise ValueError('Build manifest does not match the release ELF')
    cfg = (variant / 'tools/board_com11.cfg').read_text(encoding='ascii')
    if not re.search(r'^adapter usb location 1-10\s*$', cfg, re.M) or 'adapter usb location 1-7' in cfg:
        raise ValueError('Unexpected physical target configuration')
    return records, elf


def prepare(variant, stage, records, elf):
    root = repo_of(variant)
    if not stage.is_relative_to(root / '06_Controller_Dependencies') or not stage.name.startswith('classic_launch_') or not str(stage).isascii():
        raise ValueError('Stage must be a new ASCII repository 06_Controller_Dependencies/classic_launch_UUID directory')
    if (stage / 'launch.json').exists():
        raise FileExistsError('Refuse to overwrite a prior launch')
    stage.mkdir(parents=True, exist_ok=True)
    inputs = stage / 'inputs'; inputs.mkdir()
    for row in records:
        shutil.copy2(variant / row['path'], inputs / row['name'])
    shutil.copy2(variant / 'runtime_inputs.json', stage / 'runtime_inputs.json')
    shutil.copy2(variant / 'build/build_manifest.json', stage / 'build_manifest.json')
    shutil.copy2(variant / 'tools/board_com11.cfg', stage / 'board_com11.cfg')
    save(stage / 'launch.json', dict(**TARGET, variant=str(variant), prepared_utc=datetime.now(timezone.utc).isoformat(),
                                   inputs=records, elf=elf, runtime_manifest_sha256=sha((stage / 'runtime_inputs.json').read_bytes())))


def verify_owned_openocd(stage):
    meta = read_json(stage / 'openocd_identity.json')
    for key in ['uart', 'physical_usb', 'gdb_port', 'telnet_port', 'tcl_port']:
        if meta.get(key) != TARGET[key]:
            raise ValueError('Unexpected owned debugger identity: ' + key)
    config_args = re.findall(r'-f\s+"([^"]+)"', meta['arguments'])
    expected_cfg = (stage / 'board_com11.cfg').resolve()
    if not any(Path(item).resolve() == expected_cfg for item in config_args):
        raise ValueError('Owned debugger was not launched with this staged physical target')
    kernel = ctypes.WinDLL('kernel32', use_last_error=True)
    kernel.OpenProcess.argtypes = [wt.DWORD, wt.BOOL, wt.DWORD]; kernel.OpenProcess.restype = wt.HANDLE
    kernel.CloseHandle.argtypes = [wt.HANDLE]
    kernel.QueryFullProcessImageNameW.argtypes = [wt.HANDLE, wt.DWORD, wt.LPWSTR, ctypes.POINTER(wt.DWORD)]
    kernel.GetProcessTimes.argtypes = [wt.HANDLE, *([ctypes.POINTER(wt.FILETIME)] * 4)]
    handle = kernel.OpenProcess(0x1000, False, int(meta['pid']))
    if not handle:
        raise OSError('Cannot inspect owned OpenOCD process')
    try:
        name = ctypes.create_unicode_buffer(32768); size = wt.DWORD(len(name))
        if not kernel.QueryFullProcessImageNameW(handle, 0, name, ctypes.byref(size)) or Path(name.value).resolve() != Path(meta['exe']).resolve():
            raise ValueError('Owned debugger process image mismatch')
        times = [wt.FILETIME() for _ in range(4)]
        if not kernel.GetProcessTimes(handle, *[ctypes.byref(item) for item in times]):
            raise OSError('Cannot inspect owned debugger creation time')
        actual_started = ((times[0].dwHighDateTime << 32) | times[0].dwLowDateTime) / 10000000 - 11644473600
        expected_started = datetime.fromisoformat(meta['started_at'].replace('Z', '+00:00')).timestamp()
        if abs(actual_started - expected_started) > .001:
            raise ValueError('Owned debugger PID creation time changed')
    finally:
        kernel.CloseHandle(handle)


def gdb_commands(stage, records):
    def filename(name):
        value = (stage / name).as_posix()
        if re.search(r'\s|["\\]', value):
            raise ValueError('GDB restore requires ASCII staging without whitespace')
        return value
    commands = ['set confirm off', 'set pagination off', 'set mem inaccessible-by-default off',
                'set arch riscv:rv32', 'set remotetimeout 60', 'file ' + filename('inputs/game.elf'),
                'target extended-remote 127.0.0.1:14333', 'monitor halt',
                'set $gpu=*(unsigned int*)0xf8100024', 'set $dbg=*(unsigned int*)0xf810006c',
                'set $bul=*(unsigned int*)0xf8100100', 'set $brev=*(unsigned int*)0xf8100150',
                'set $fmis=*(unsigned int*)0xf8100180', 'set $frev=*(unsigned int*)0xf810018c',
                'set $hst=*(unsigned int*)0xf8100280', 'set $shape=*(unsigned int*)0xf8100288',
                'printf "[RAM_LOADER_ID] GPU=%08x DBG=%08x BUL=%08x REV=%08x FMIS=%08x FREV=%08x HST=%08x SHAPE=%08x\\n",$gpu,$dbg,$bul,$brev,$fmis,$frev,$hst,$shape',
                'if $gpu!=0x47505531 || $dbg!=0x44424731 || $bul!=0x42554c39 || $brev!=0x10007 || $fmis!=0x464d4953 || $frev!=0x10002 || $hst!=0x48535432 || $shape!=0x00400008',
                'echo Hardware identity mismatch; prior CPU resumed, RAM untouched\\n', 'monitor resume', 'disconnect', 'quit 41', 'end',
                'if *(unsigned int*)0xf8100184!=0 || (*(unsigned int*)0xf8100284 & 1)!=0']
    for name, start, end in [('stage9_regs.bin', 0xf8100100, 0xf810015c), ('first_missing.bin', 0xf8100180, 0xf8100234),
                             ('history_header.bin', 0xf8100280, 0xf8100290), ('history_context.bin', 0xf8100300, 0xf81003c0),
                             ('history.bin', 0xf8101000, 0xf8101800)]:
        commands.append('dump binary memory %s 0x%x 0x%x' % (filename(name), start, end))
    commands += ['echo Existing fault saved; prior CPU resumed, no new load or clear\\n',
                 'monitor resume', 'disconnect', 'quit 42', 'end', 'monitor reset init', 'monitor reset halt', 'load']
    for record in records:
        name, address = record['name'], record['address']
        if address is not None:
            commands.append('restore %s binary 0x%08x' % (filename('inputs/' + name), address))
    commands += ['set {unsigned int}0xf810005c=4', 'monitor sleep 40',
                 'if *(unsigned int*)0xf810005c!=4 || (*(unsigned int*)0xf8100074 & 0x400)==0',
                 'echo Relock readback failed; CPU remains halted\\n', 'disconnect', 'quit 43', 'end',
                 'echo [RAM_LOADER_RESUME] entry=0x1000\\n', 'monitor resume 0x1000', 'disconnect', 'quit']
    return '\n'.join(commands) + '\n'


def fields(line):
    result = {}
    for key, value in re.findall(r'(\w+)=([^\s]+)', line):
        try:
            result[key] = int(value, 16) if value.startswith('0x') else int(value)
        except ValueError:
            result[key] = value
    return result


def startup_check(text, records):
    lines = text.splitlines()
    def tagged(tag):
        found = [line for line in lines if line.startswith(tag)]
        if not found:
            raise ValueError('Missing startup evidence ' + tag)
        return fields(found[-1])
    crc_tags = [('[ATLAS]', 'atlas.bin'), ('[ATLAS_POST_PROBE]', 'atlas.bin')]
    for tag, name in crc_tags:
        actual = tagged(tag); row = next(r for r in records if r['name'] == name)
        if actual.get('bytes') != row['bytes'] or actual.get('crc32') != row['crc32'] or actual.get('expected') != row['crc32']:
            raise ValueError('Startup CRC/extent mismatch ' + tag)
    for tag in ['[ALPHA_PROBE]', '[PRECISION_PROBE]']:
        actual = tagged(tag)
        if actual.get('rc') != 0 or not actual.get('cases') or any(actual.get(k) != 0 for k in ['pixel_errors', 'source_errors', 'guard_errors']):
            raise ValueError('Startup hardware readback failed ' + tag)
    if not any(line.startswith('[COLOR_KEY]') and 'PASS' in line for line in lines):
        raise ValueError('Color Key readback not confirmed')
    if not any(line.startswith('[READY]') for line in lines):
        raise ValueError('No fresh READY')
    return dict(atlas_before_after_crc=True, alpha_pixels=True,
                precision_pixels=True, color_key_pixels=True, fresh_ready=True,
                visual_acceptance=False)


def execute(stage, records, seconds, gdb):
    verify_owned_openocd(stage)
    config = read_json(stage / 'launch.json')
    if any(config.get(k) != v for k, v in TARGET.items()):
        raise ValueError('Staged target identity changed')
    for row in records:
        data = (stage / 'inputs' / row['name']).read_bytes()
        if len(data) != row['bytes'] or sha(data) != row['sha256']:
            raise ValueError('Staged bytes changed: ' + row['name'])
    sys.path.insert(0, 'D:/Efinity/2026.1/python311/Lib/site-packages')
    import serial
    script = stage / 'load.gdb'; script.write_text(gdb_commands(stage, records), encoding='ascii')
    state = dict(**TARGET, status='LOADING', startup_checks=None, capture_seconds_requested=seconds,
                 stage=str(stage), started_utc=datetime.now(timezone.utc).isoformat())
    proc = None; raw = bytearray(); ready = None; capture_end = None
    uart = serial.Serial(port=None, baudrate=115200, timeout=.1, write_timeout=2)
    uart.dtr = False; uart.rts = False; uart.port = 'COM11'
    try:
        uart.open()
        with (stage / 'gdb.log').open('w', encoding='utf-8') as log, (stage / 'serial.bin').open('wb') as binary:
            proc = subprocess.Popen([str(gdb), '-q', '-batch', '-x', str(script)], cwd=stage,
                                    stdout=log, stderr=subprocess.STDOUT,
                                    creationflags=subprocess.CREATE_NO_WINDOW)
            started = time.monotonic()
            while True:
                now = time.monotonic()
                chunk = uart.read(max(1, uart.in_waiting))
                if chunk:
                    raw.extend(chunk); binary.write(chunk); binary.flush()
                if b'[FAIL]' in raw or b'[TRAP]' in raw:
                    raise RuntimeError('Game reported failure; preserve logs and hardware evidence')
                if ready is None and b'[READY]' in raw:
                    ready = time.monotonic(); capture_end = ready + seconds
                code = proc.poll()
                if code is not None and code != 0:
                    raise RuntimeError('GDB failed with code %s; see gdb.log (41=wrong hardware,42=preserved prior fault,43=relock)' % code)
                if ready is not None and now >= capture_end and code == 0:
                    break
                if ready is None and now - started > 300:
                    raise TimeoutError('No fresh READY within 300 seconds')
                if ready is not None and now > capture_end + 30:
                    raise TimeoutError('GDB did not finish after READY')
        text = raw.decode('ascii', errors='replace')
        state['startup_checks'] = startup_check(text, records)
        state.update(status='READY', gdb_returncode=proc.returncode,
                     captured_after_ready_seconds=time.monotonic() - ready,
                     serial_bytes=len(raw), serial_sha256=sha(raw))
    except Exception as exc:
        state.update(status='FAILED', error=repr(exc), cpu_state='See gdb.log; only identity/fault rejection resumes the prior program. Partial loads are not blindly resumed.')
        raise
    finally:
        uart.close()
        if proc is not None and proc.poll() is None:
            proc.terminate()
            try:
                proc.wait(timeout=5)
            except subprocess.TimeoutExpired:
                proc.kill(); proc.wait(timeout=5)
        state.update(serial_bytes=len(raw), serial_sha256=sha(raw))
        (stage / 'serial.log').write_text(raw.decode('ascii', errors='replace'), encoding='utf-8')
        save(stage / 'result.json', state)
        print(json.dumps(state, ensure_ascii=False, indent=2), flush=True)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--variant', type=Path, required=True)
    parser.add_argument('--stage', type=Path)
    parser.add_argument('--port', choices=['COM11'], default='COM11')
    parser.add_argument('--seconds', type=int, default=30)
    parser.add_argument('--gdb', type=Path, default=Path('D:/Efinity/efinity-riscv-ide-2026.1/toolchain/bin/riscv-none-elf-gdb.exe'))
    parser.add_argument('--dry-run', action='store_true')
    parser.add_argument('--prepare', action='store_true')
    args = parser.parse_args()
    if not 1 <= args.seconds <= 3600:
        parser.error('--seconds must be 1..3600')
    variant = args.variant.resolve(); records, elf = validate(variant)
    if args.dry_run:
        print(json.dumps(dict(**TARGET, status='DRY_RUN', hardware_access=False,
                              variant=str(variant), inputs=records, elf=elf,
                              recover_fpga_supported=False), indent=2))
        return
    if args.stage is None:
        parser.error('--stage is required outside --dry-run')
    stage = args.stage.resolve()
    if args.prepare:
        prepare(variant, stage, records, elf)
        print(json.dumps(dict(status='PREPARED', stage=str(stage), hardware_access=False), indent=2))
    else:
        execute(stage, records, args.seconds, args.gdb.resolve())


if __name__ == '__main__':
    main()
