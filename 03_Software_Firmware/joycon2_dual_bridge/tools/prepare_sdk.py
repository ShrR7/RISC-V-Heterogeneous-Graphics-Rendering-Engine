"""Prepare the pinned local ESP32 SDK without network or hardware access.

Default: validate inputs and print a plan. --execute extracts only checked
local archives into the existing isolated SDK and installs the pinned local
NimBLE source. Vendor SDKs and private Flash/NVS data are never bundled here.
"""
from __future__ import annotations
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import zipfile

HERE = Path(__file__).resolve().parent


def workspace():
    for path in HERE.parents:
        if (path / '01_Hardware_Platform').is_dir():
            return path
    raise ValueError('Workspace containing 01_Hardware_Platform was not found')


def sha(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def nimble_fingerprint(path):
    files = [p for p in (path / 'src').rglob('*') if p.is_file()]
    files += [path / name for name in ('library.properties', 'LICENSE', 'NOTICE')]
    files.sort(key=lambda p: p.relative_to(path).as_posix())
    if not files or any(not p.is_file() for p in files):
        raise ValueError('Complete local NimBLE-Arduino 2.5.2 source is required')
    records = [{'path': p.relative_to(path).as_posix(), 'bytes': p.stat().st_size,
                'sha256': sha(p)} for p in files]
    encoded = json.dumps(records, sort_keys=True, separators=(',', ':')).encode()
    return hashlib.sha256(encoded).hexdigest(), files


def checked_extract(source, destination):
    # Each pinned upstream zip has one wrapper directory. Check every target
    # after stripping it; do not follow symlinks supplied by an archive.
    with zipfile.ZipFile(source) as archive:
        for entry in archive.infolist():
            parts = entry.filename.replace('\\', '/').split('/')[1:]
            if not parts or not any(parts):
                continue
            target = destination.joinpath(*parts).resolve()
            if not target.is_relative_to(destination.resolve()):
                raise ValueError('Unsafe archive member: ' + entry.filename)
            if (entry.external_attr >> 16) & 0o170000 == 0o120000:
                raise ValueError('Archive symlinks are not accepted')
            if entry.is_dir():
                target.mkdir(parents=True, exist_ok=True)
            else:
                target.parent.mkdir(parents=True, exist_ok=True)
                output = '\\\\?\\' + str(target) if os.name == 'nt' else str(target)
                with archive.open(entry) as inp, open(output, 'wb') as out:
                    shutil.copyfileobj(inp, out)


def main():
    root = workspace()
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--offline', type=Path, default=root / 'tools/tools/esp32_offline')
    parser.add_argument('--arduino-data', type=Path,
                        help='Existing Arduino data with packages/builtin and its two indexes; required on first preparation')
    parser.add_argument('--nimble-source', type=Path,
                        help='Local pinned NimBLE-Arduino 2.5.2 source; never downloaded by this script')
    parser.add_argument('--check-build-inputs', action='store_true',
                        help='Only verify existing SDK configuration and pinned NimBLE compile inputs')
    parser.add_argument('--execute', action='store_true')
    args = parser.parse_args()
    metadata = json.loads((HERE / 'sdk_archives.json').read_text(encoding='utf-8'))
    sdk = root / '06_Controller_Dependencies/j2sdk'
    dependency = root / '06_Controller_Dependencies/joycon2_dual_20261031/deps/NimBLE-Arduino'
    nimble = args.nimble_source.resolve() if args.nimble_source else dependency
    fingerprint, nimble_files = nimble_fingerprint(nimble)
    if fingerprint != metadata['nimble']['compile_tree_sha256']:
        raise ValueError('NimBLE source differs from the pinned 2.5.2 compile tree')
    install_nimble = nimble.resolve() != dependency.resolve() and not dependency.exists()
    if nimble.resolve() != dependency.resolve() and dependency.exists() and nimble_fingerprint(dependency)[0] != fingerprint:
        raise ValueError('Existing isolated NimBLE differs from the pinned source; retain it separately before preparing')
    if args.check_build_inputs:
        if not (sdk / 'arduino-cli.yaml').is_file():
            raise ValueError('Run this prepare_sdk.py with local prerequisites and --execute first')
        print(json.dumps({'status': 'BUILD_INPUTS_VALID', 'sdk': str(sdk),
                          'nimble_compile_sha256': fingerprint, 'hardware_access': False}))
        return 0
    offline = args.offline.resolve()
    index_path = offline / 'package_esp32_index.json'
    index = json.loads(index_path.read_text(encoding='utf-8-sig'))
    package = next(p for p in index['packages'] if p['name'] == 'esp32')
    platform = next(p for p in package['platforms'] if p['version'] == '3.3.12')
    entries = {platform['archiveFileName']: platform}
    for tool in package['tools']:
        for system in tool.get('systems', []):
            entries[system['archiveFileName']] = system
    plan = []
    for item in metadata['archives']:
        source = offline / item['archive']
        destination = (root / item['destination']).resolve()
        if not destination.is_relative_to((sdk / 'data/packages').resolve()):
            raise ValueError('SDK destination escapes the isolated package directory')
        expected = entries[item['archive']]['checksum'].split(':', 1)[1].lower()
        if expected != item['sha256'] or sha(source) != item['sha256']:
            raise ValueError('Local archive/index hash mismatch: ' + item['archive'])
        if not zipfile.is_zipfile(source):
            raise ValueError('Expected ZIP archive: ' + str(source))
        plan.append((source, destination, item['sha256']))
    if args.arduino_data:
        candidates = [args.arduino_data.resolve()]
    else:
        candidates = [sdk / 'data', Path(os.environ.get('LOCALAPPDATA', str(root))) / 'Arduino15']
    arduino_data = next((p for p in candidates if (p / 'packages/builtin').is_dir()
                         and all((p / n).is_file() for n in ('package_index.json', 'library_index.json'))), None)
    if arduino_data is None:
        raise ValueError('Provide --arduino-data from an existing Arduino IDE/CLI data directory (packages/builtin, package_index.json, library_index.json); no network setup is performed')
    result = {'status': 'PREPARED' if args.execute else 'PLAN_ONLY_NO_WRITES',
              'hardware_access': False, 'network_access': False,
              'sdk': str(sdk), 'arduino_data': str(arduino_data),
              'nimble_source': str(nimble), 'nimble_destination': str(dependency),
              'nimble_compile_sha256': fingerprint,
              'archives': [{'path': str(s), 'destination': str(d), 'sha256': h} for s, d, h in plan]}
    if not args.execute:
        print(json.dumps(result, indent=2))
        return 0
    for source, destination, digest in plan:
        marker = destination / '.j2sdk_checked_archive'
        if not marker.is_file() or marker.read_text() != digest:
            destination.mkdir(parents=True, exist_ok=True)
            checked_extract(source, destination)
            marker.write_text(digest)
    builtin = sdk / 'data/packages/builtin'
    if not builtin.exists():
        shutil.copytree(arduino_data / 'packages/builtin', builtin)
    for name in ('package_index.json', 'library_index.json'):
        target = sdk / 'data' / name
        if (arduino_data / name).resolve() != target.resolve():
            shutil.copy2(arduino_data / name, target)
    if install_nimble:
        for source in nimble_files:
            target = dependency / source.relative_to(nimble)
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(source, target)
        if nimble_fingerprint(dependency)[0] != fingerprint:
            raise ValueError('NimBLE copy verification failed')
    (sdk / 'data/package_esp32_index.json').write_text(json.dumps(index), encoding='utf-8')
    for name in ('downloads', 'user', 'build', 'output', 'cache', 'temp', 'logs'):
        (sdk / name).mkdir(parents=True, exist_ok=True)
    base = sdk.as_posix()
    (sdk / 'arduino-cli.yaml').write_text(
        'board_manager:\n  additional_urls: []\ndirectories:\n'
        f"  data: '{base}/data'\n  downloads: '{base}/downloads'\n  user: '{base}/user'\n"
        'logging:\n  level: warn\nmetrics:\n  enabled: false\n', encoding='utf-8')
    (sdk / 'sdk_preparation.json').write_text(json.dumps(result, indent=2), encoding='utf-8')
    print(json.dumps(result, indent=2))
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
