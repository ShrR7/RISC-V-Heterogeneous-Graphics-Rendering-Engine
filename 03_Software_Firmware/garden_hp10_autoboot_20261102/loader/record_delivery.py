"""Seal the relocated offline delivery, without invoking a compiler or hardware."""
import argparse
import hashlib
import json
from pathlib import Path

HERE=Path(__file__).resolve().parent
def record(path):
    data=path.read_bytes()
    return {'bytes':len(data),'sha256':hashlib.sha256(data).hexdigest()}
def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--origin',type=Path,required=True)
    a=p.parse_args();origin=a.origin.resolve()
    source_names=['package.h','package.c','stage1.S','stage1.ld','stage2_start.S',
        'stage2.c','stage2.ld','build.py','pack.py','host_checks.py','README.txt',
        'audit_artifacts.py','record_delivery.py','display_host_checks.py']
    binary_names=['stage1.elf','stage1.bin','stage2.elf','stage2.bin','boot_slot.bin']
    copied={}
    for n in binary_names:
        actual=record(HERE/'build'/n);old=record(origin/'build'/n)
        if actual!=old:raise ValueError('Copied frozen build differs: '+n)
        copied[n]={'record':actual,'identical_to_frozen_origin':True}
    pkg=HERE.parent/'package';segments={}
    for n in ['boot_slot.bin','payload.bin','header.bin']:
        actual=record(pkg/n);old=record(origin/'package_candidate'/n)
        if actual!=old:raise ValueError('Repacked programming segment differs: '+n)
        segments[n]={'record':actual,'identical_to_frozen_origin':True}
    manifest={'schema':1,'purpose':'relocated CGB1 autonomous game boot delivery',
        'no_firmware_recompiled_during_copy':True,'origin':str(origin),
        'source_files':{n:record(HERE/n) for n in source_names},
        'copied_frozen_build':copied,'repacked_segments':segments,
        'host_results':record(HERE/'host_results.json'),
        'display_host_results':record(HERE/'display_host_results.json'),
        'artifact_review':record(HERE/'artifact_review.json'),
        'flash_programmed_by_this_script':False}
    (HERE/'delivery_manifest.json').write_text(json.dumps(manifest,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    print(json.dumps({'status':'PASS','copied_frozen_files':len(copied),'identical_repacked_files':len(segments),
                      'manifest':str(HERE/'delivery_manifest.json')}))
if __name__=='__main__':main()
