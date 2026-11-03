"""Merge reviewed regions into an intact backup; never opens a device."""
from pathlib import Path
import argparse
import hashlib
import json

CAPACITY = 0x800000
SECTOR = 4096


def sha(blob):
    return hashlib.sha256(blob).hexdigest()


def compose(original, patches):
    if len(original) != CAPACITY:
        raise ValueError('Expected complete 8MiB original image')
    image = bytearray(original)
    spans = []
    for name, address, data in patches:
        if not data or address % SECTOR or address < 0 or address+len(data) > CAPACITY:
            raise ValueError('Invalid patch address/length: '+name)
        if any(address < end and start < address+len(data) for start,end in spans):
            raise ValueError('Overlapping requested patches')
        spans.append((address,address+len(data)))
        image[address:address+len(data)] = data
    # Each output region includes the untouched tail of its last erased sector.
    regions = []
    region_spans = []
    for name,address,data in patches:
        end = (address+len(data)+SECTOR-1)//SECTOR*SECTOR
        if any(address < other_end and other_start < end for other_start,other_end in region_spans):
            raise ValueError('Erase-sector overlap between patches')
        region_spans.append((address,end))
        region = bytes(image[address:end])
        if region != original[address:end]:
            regions.append((name,address,region))
    changed = [i for i,(a,b) in enumerate(zip(original,image)) if a != b]
    if any(not any(start <= i < end for start,end in spans) for i in changed):
        raise AssertionError('Byte outside requested patch changed')
    return bytes(image), regions, len(changed)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--backup-manifest',type=Path,required=True)
    p.add_argument('--package',type=Path,required=True)
    p.add_argument('--configuration-bin',type=Path)
    p.add_argument('--resources-only',action='store_true')
    p.add_argument('--out',type=Path,required=True)
    a = p.parse_args()
    backup = json.loads(a.backup_manifest.read_text(encoding='utf-8-sig'))
    original_path = Path(backup['backup_binary'])
    original = original_path.read_bytes()
    if (backup.get('status') != 'BACKUP_COMPLETE' or backup.get('jedec') != 'ef6017'
        or backup.get('passes_matching',0) < 2 or backup.get('capacity') != CAPACITY
        or sha(original) != backup.get('backup_sha256')):
        raise ValueError('Two matching complete reads of this board are required')
    manifest = json.loads((a.package/'manifest.json').read_text(encoding='utf-8-sig'))
    if manifest.get('format') != 'CGB1' or manifest.get('manifest_flash_offset') != 0x400000:
        raise ValueError('Unexpected reviewed package layout')
    if ({s['name'] for s in manifest['segments']} != {'boot_slot.bin','payload.bin'}
        or len(manifest['segments']) != 2):
        raise ValueError('Expected exactly the reviewed bootstrap and resource segments')
    patches = []
    if not a.resources_only:
        if not a.configuration_bin:
            raise ValueError('Full image requires reviewed active SPI configuration')
        configuration = a.configuration_bin.read_bytes()
        if (len(configuration) != 925393
            or sha(configuration) != 'e588e410c2b0c4068d9a867cbe9445a8ba89d7209c5a363efc2930226277ade8'):
            raise ValueError('Expected matched D591 active SPI configuration')
        patches.append(('configuration',0,configuration))
    for segment in manifest['segments']:
        if a.resources_only and segment['name'] != 'payload.bin':
            continue
        data = (a.package/segment['name']).read_bytes()
        if len(data) != segment['bytes'] or sha(data) != segment['sha256']:
            raise ValueError('Package segment changed after packing')
        address = segment['flash_offset']
        if ((segment['name'] == 'boot_slot.bin' and (address != 0x380000 or len(data) != 0x1f000))
            or (segment['name'] == 'payload.bin' and address != 0x400000)):
            raise ValueError('Unexpected segment address')
        patches.append((segment['name'].removesuffix('.bin'),address,data))
    expected,image_regions,changed = compose(original,patches)
    a.out.mkdir(parents=True,exist_ok=False)
    (a.out/'expected_flash_8MiB.bin').write_bytes(expected)
    program=[]
    for name,address,data in image_regions:
        filename = name+'_sector_image.bin'
        (a.out/filename).write_bytes(data)
        program.append(dict(name=name,file=filename,address=address,bytes=len(data),sha256=sha(data)))
    result=dict(schema=1,status='OFFLINE_ASSEMBLED_NOT_PROGRAMMED',
        resources_only=a.resources_only,expected_jedec='ef6017',capacity=CAPACITY,
        original_backup_manifest=str(a.backup_manifest.resolve()),original_sha256=sha(original),
        expected_flash_sha256=sha(expected),changed_bytes=changed,
        all_other_bytes_preserved=True,flash_chip_erase=False,
        program_regions=program,
        patches=[dict(name=n,address=addr,bytes=len(data),sha256=sha(data)) for n,addr,data in patches])
    (a.out/'manifest.json').write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    print(json.dumps(result,ensure_ascii=True))


if __name__ == '__main__':
    main()
