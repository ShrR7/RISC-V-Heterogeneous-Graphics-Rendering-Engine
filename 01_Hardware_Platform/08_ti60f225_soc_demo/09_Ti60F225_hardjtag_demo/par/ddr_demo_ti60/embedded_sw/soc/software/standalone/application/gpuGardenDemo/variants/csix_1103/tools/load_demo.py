"""Verified RAM-only independent six-case loader, COM11 / USB1-10 / GDB14333.

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
    manifest_path = variant / 'tools/runtime_inputs.json'
    if not manifest_path.is_file():
        raise FileNotFoundError('Missing runtime_inputs.json; use the complete matching release')
    manifest = read_json(manifest_path)
    if manifest.get('schema') != 1 or manifest.get('variant') != 'csix_1103' or manifest.get('status') != 'SEALED':
        raise ValueError('Unexpected runtime manifest schema/variant')
    if manifest.get('target') != TARGET or manifest.get('expected_hardware_md5') != 'D59183E81A58833B1D550A91EBBE5E23':
        raise ValueError('Runtime manifest target/hardware identity mismatch')
    if manifest.get('ram_only') is not True or manifest.get('flash_written') is not False:
        raise ValueError('Only the RAM-only manifest is permitted')
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
    if build.get('variant') != 'csix_1103' or build.get('elf_sha256', '').lower() != next(r['sha256'] for r in records if r['name'] == 'game.elf'):
        raise ValueError('Build manifest does not match the release ELF')
    if manifest.get('build_manifest_sha256') != sha((variant / 'build/build_manifest.json').read_bytes()):
        raise ValueError('Build manifest changed after sealing')
    cfg = (variant / 'tools/board_com11.cfg').read_text(encoding='ascii')
    if not re.search(r'^adapter usb location 1-10\s*$', cfg, re.M) or 'adapter usb location 1-7' in cfg:
        raise ValueError('Unexpected physical target configuration')
    return records, elf


def prepare(variant, stage, records, elf):
    root = repo_of(variant)
    if not stage.is_relative_to(root / '06_Controller_Dependencies') or not stage.name.startswith('six_launch_') or not str(stage).isascii():
        raise ValueError('Stage must be a new ASCII repository 06_Controller_Dependencies/six_launch_UUID directory')
    if (stage / 'launch.json').exists():
        raise FileExistsError('Refuse to overwrite a prior launch')
    stage.mkdir(parents=True, exist_ok=True)
    inputs = stage / 'inputs'; inputs.mkdir()
    for row in records:
        shutil.copy2(variant / row['path'], inputs / row['name'])
    shutil.copy2(variant / 'tools/runtime_inputs.json', stage / 'runtime_inputs.json')
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
    fresh = [i for i, line in enumerate(lines) if re.search(r'^\[READY\] SIX_1103\b', line)]
    if not fresh:
        raise ValueError('Missing fresh standalone SIX_1103 READY')
    previous = max((i for i in range(fresh[-1]) if lines[i].startswith('[READY]')), default=-1)
    # A preceding run's probes are not evidence for this boot.
    lines = lines[previous + 1:]
    if any(line.startswith(('[FAIL]', '[TRAP]', '[FATAL]', '[SIX_FATAL]', '[SIX_STOP]', '[SIX_FAILED]')) for line in lines):
        raise ValueError('Standalone demo reported a fatal/stop; READY is not a running acceptance')
    ready = [line for line in lines if re.search(r'^\[READY\] SIX_1103\b', line)]
    identity = fields(ready[-1])
    if any(identity.get(key) != value for key, value in dict(cases=6, duration_ms=10000, count=2093, independent=1).items()):
        raise ValueError('SIX READY must describe six independent10-second2093-object scenes')
    verified = []
    for tag in ('[ALPHA_PROBE]', '[PRECISION_PROBE]', '[MODE_PIXEL_PROBE]'):
        found = [line for line in lines if line.startswith(tag)]
        for line in found:
            actual = fields(line)
            if tag != '[MODE_PIXEL_PROBE]':
                good = actual.get('rc') == 0 and actual.get('cases', 0) > 0 and all(actual.get(k) == 0 for k in ('pixel_errors', 'source_errors', 'guard_errors'))
            else:
                good = (actual.get('errors') == 0 and actual.get('modes') == 3 and actual.get('pixels') == 96
                        and actual.get('source_pixels') == 6912 and actual.get('no_key_matches') == 0
                        and actual.get('original_key_pixels', 0) > 0)
            if not good:
                raise ValueError('Reported readback probe failed: ' + line)
            verified.append(tag)
    if set(verified) != {'[ALPHA_PROBE]', '[PRECISION_PROBE]', '[MODE_PIXEL_PROBE]'}:
        raise ValueError('All three fresh ALPHA/PRECISION/96-pixel MODE probes are required')
    # If the program supplies atlas checks, they must match the staged blob.
    atlas = next(r for r in records if r['name'] == 'atlas.bin')
    for line in lines:
        if line.startswith('[ATLAS]') or line.startswith('[ATLAS_POST_PROBE]'):
            f = fields(line)
            if f.get('bytes') != atlas['bytes'] or f.get('crc32') != atlas['crc32'] or f.get('expected') != atlas['crc32']:
                raise ValueError('Reported atlas checksum mismatches frozen runtime input')
    return dict(fresh_six_ready=True, cases=6, duration_ms=10000,
                count=2093, independent=1, successful_probes=sorted(set(verified)),
                visual_acceptance=False)


def write_manifest(variant):
    build_path = variant / 'build/build_manifest.json'
    build = read_json(build_path)
    if build.get('variant') != 'csix_1103':
        raise ValueError('Build this standalone variant before sealing runtime inputs')
    rows = []
    for relative in INPUTS:
        data = (variant / relative).read_bytes()
        rows.append(dict(path=relative, bytes=len(data), sha256=sha(data)))
        if relative.endswith('.elf'):
            elf_check(data)
            if build.get('elf_sha256', '').lower() != sha(data):
                raise ValueError('Build manifest ELF hash mismatch')
    atlas = next(row for row in rows if row['path'] == 'assets/atlas.bin')
    if build.get('atlas_sha256', '').lower() != atlas['sha256'] or build.get('atlas_bytes') != atlas['bytes']:
        raise ValueError('Build manifest atlas identity mismatch')
    result = dict(schema=1, variant='csix_1103', status='SEALED',
                  created_utc=datetime.now(timezone.utc).isoformat(), files=rows,
                  expected_hardware_md5='D59183E81A58833B1D550A91EBBE5E23',
                  target=TARGET, build_manifest_sha256=sha(build_path.read_bytes()),
                  boot_signature='[READY] SIX_1103 cases=6 duration_ms=10000 count=2093 independent=1',
                  ram_only=True, flash_written=False)
    save(variant / 'tools/runtime_inputs.json', result)
    return result


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
    try:
        import serial
    except ImportError as exc:
        raise RuntimeError('pyserial unavailable in this interpreter and local Efinity site-packages; set -PythonExecutable or PYTHONPATH to an installed local copy. No pip/network installation was attempted.') from exc
    script = stage / 'load.gdb'; script.write_text(gdb_commands(stage, records), encoding='ascii')
    state = dict(**TARGET, status='LOADING', startup_checks=None, capture_seconds_requested=seconds,
                 stage=str(stage), started_utc=datetime.now(timezone.utc).isoformat())
    proc = None; raw = bytearray(); ready = None; capture_end = None
    uart = serial.Serial(port=None, baudrate=115200, timeout=.1, write_timeout=2)
    uart.dtr = False; uart.rts = False; uart.port = 'COM11'
    try:
        uart.open()
        with (stage / 'gdb.log').open('w', encoding='utf-8') as log, (stage / 'serial.bin').open('wb') as binary, (stage / 'serial_live.log').open('wb') as live:
            proc = subprocess.Popen([str(gdb), '-q', '-batch', '-x', str(script)], cwd=stage,
                                    stdout=log, stderr=subprocess.STDOUT,
                                    creationflags=subprocess.CREATE_NO_WINDOW)
            started = time.monotonic()
            while True:
                now = time.monotonic()
                chunk = uart.read(max(1, uart.in_waiting))
                if chunk:
                    raw.extend(chunk); binary.write(chunk); binary.flush()
                    live.write(chunk); live.flush()
                if any(marker in raw for marker in (b'[FAIL]', b'[TRAP]', b'[FATAL]', b'[SIX_FATAL]', b'[SIX_STOP]', b'[SIX_FAILED]')):
                    raise RuntimeError('Demo reported failure; preserve logs and hardware evidence')
                if ready is None and re.search(rb'(?m)^\[READY\] SIX_1103\b', raw):
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
        (stage / 'serial.log').write_text(raw.decode('ascii', errors='replace'), encoding='utf-8', newline='')
        save(stage / 'result.json', state)
        print(json.dumps(state, ensure_ascii=False, indent=2), flush=True)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--variant', type=Path, required=True)
    parser.add_argument('--stage', type=Path)
    parser.add_argument('--port', choices=['COM11'], default='COM11')
    parser.add_argument('--seconds', '--capture-seconds', dest='seconds', type=int, default=140)
    parser.add_argument('--write-manifest', action='store_true')
    parser.add_argument('--load-only', action='store_true')
    parser.add_argument('--gdb', type=Path, default=Path('D:/Efinity/efinity-riscv-ide-2026.1/toolchain/bin/riscv-none-elf-gdb.exe'))
    parser.add_argument('--dry-run', action='store_true')
    parser.add_argument('--prepare', action='store_true')
    args = parser.parse_args()
    if not 0 <= args.seconds <= 3600:
        parser.error('--seconds must be0..3600')
    if args.load_only:
        args.seconds = 0
    variant = args.variant.resolve()
    if args.write_manifest:
        print(json.dumps(write_manifest(variant), indent=2))
        return
    records, elf = validate(variant)
    if args.dry_run:
        print(json.dumps(dict(**TARGET, status='DRY_RUN', hardware_access=False,
                              variant=str(variant), inputs=records, elf=elf,
                              recover_fpga_supported=True), indent=2))
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
