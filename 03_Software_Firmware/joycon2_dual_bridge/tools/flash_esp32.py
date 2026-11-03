"""Program only the identified ESP32, preserving NVS and the rollback backup."""
import argparse
from datetime import datetime
import hashlib
import json
from pathlib import Path
import subprocess

ROOT=Path(__file__).resolve().parents[3]
DELIVERY=Path(__file__).resolve().parents[1]/'delivery'

def sha(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream,'sha256').hexdigest()

def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--port',default='COM22')
    parser.add_argument('--dry-run',action='store_true')
    args=parser.parse_args()
    if args.port.upper()=='COM11':
        raise SystemExit('COM11 is the FPGA port: operation refused')
    manifest=json.loads((DELIVERY/'manifest.json').read_text(encoding='utf-8'))
    backup=ROOT/manifest['rollback']['path']
    if not backup.is_file() or backup.stat().st_size!=0x400000 or sha(backup)!=manifest['rollback']['sha256']:
        raise SystemExit('Verified private rollback backup is required')
    tool=ROOT/manifest['esptool']
    images=[]
    for item in manifest['images']:
        path=DELIVERY/item['name'];offset=int(item['offset'],16)
        if path.stat().st_size!=item['bytes'] or sha(path)!=item['sha256']:
            raise SystemExit('Image hash mismatch: '+item['name'])
        end=offset+item['bytes']
        if offset<0xe000 and end>0x9000:
            raise SystemExit('Image would overlap preserved NVS')
        images.extend([item['offset'],str(path)])
    common=[str(tool),'--chip','esp32','--port',args.port,'--baud','460800']
    command=common+['write-flash','--flash-mode','dio','--flash-freq','40m','--flash-size','4MB']+images
    if args.dry_run:
        print(json.dumps({'port':args.port,'expected_mac':manifest['expected_mac'],
              'command':command,'preserved_nvs':['0x9000','0xe000'],
              'whole_chip_erase':False,'hardware_access':False},indent=2))
        return 0
    logs=ROOT/'06_Controller_Dependencies/joycon2_dual_20261031'
    stamp=datetime.now().strftime('%Y%m%d_%H%M%S')
    identity=subprocess.run(common+['read-mac'],capture_output=True,text=True,errors='replace')
    (logs/('identity_'+stamp+'.log')).write_text(identity.stdout+identity.stderr,encoding='utf-8')
    if identity.returncode or manifest['expected_mac'].lower() not in identity.stdout.lower():
        raise SystemExit('ESP32 identity check failed; no Flash write attempted')
    print('Confirmed ESP32 '+manifest['expected_mac']+' on '+args.port,flush=True)
    log=logs/('flash_'+stamp+'.log')
    with log.open('w',encoding='utf-8') as out:
        result=subprocess.run(command,stdout=out,stderr=subprocess.STDOUT)
    print('Flash log:',log,flush=True)
    text=log.read_text(encoding='utf-8')
    print('\n'.join(text.splitlines()[-13:]),flush=True)
    if result.returncode:
        raise SystemExit(result.returncode)
    # Explicit Flash comparison supplements the programmer's transfer hashes.
    verify=common+['verify-flash']+images
    check=subprocess.run(verify,capture_output=True,text=True,errors='replace')
    (logs/('verify_'+stamp+'.log')).write_text(check.stdout+check.stderr,encoding='utf-8')
    print('\n'.join((check.stdout+check.stderr).splitlines()[-10:]),flush=True)
    return check.returncode

if __name__=='__main__':
    raise SystemExit(main())
