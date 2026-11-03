"""Record the portable installer sources and host-only verification evidence."""
import hashlib
import json
from pathlib import Path

root=Path(__file__).resolve().parent.parent
names=('tools/flash_ops.py','tools/inspect_config.py','tools/install_autoboot.py',
       'tools/test_flash_ops.py','tools/test_install_autoboot.py','tools/record_tools.py',
       'tools/flash_tools_README.txt','tools/installer_README.txt','固化Garden.ps1',
       'tools/evidence/flash_host_checks.log','tools/evidence/installer_host_checks.log',
       'tools/evidence/installer_ps51_dryrun.json')
entries={}
for name in names:
    path=root/name
    data=path.read_bytes()
    entries[name]={'bytes':len(data),'sha256':hashlib.sha256(data).hexdigest()}
manifest=dict(schema=1,purpose='portable dryrun-first Garden Flash installer',
    hardware_accessed_by_this_subtask=False,flash_programmed_by_this_subtask=False,
    installer_host_checks_passed=11,flash_wrapper_host_checks_passed=27,
    powershell51_dryrun='PASS_NO_HARDWARE_NO_INSTALL_LOG_DIRECTORY',files=entries,
    dependency_assemble_flash_sha256=hashlib.sha256((root/'tools/assemble_flash.py').read_bytes()).hexdigest(),
    notes='Actual board backup/03_Software_Firmware/readback/cold boot are recorded by root separately.')
(root/'tools/tool_delivery_manifest.json').write_text(json.dumps(manifest,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
print(json.dumps({'files':len(entries),'installer_host_checks':11,'flash_host_checks':27,'hardware_access':False}))
