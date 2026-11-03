from pathlib import Path
import hashlib
import json
import shutil
import subprocess

repo = Path('/home/codex/efinix-rtl7-relock')
work = Path(__file__).resolve().parent
target = work.parent / 'fpga-gpio6-uart'
prefix = '01_Hardware_Platform/08_ti60f225_soc_demo/09_Ti60F225_hardjtag_demo/'
previous = json.loads((work.parent / 'rtl19/source-manifest.json').read_text())
assert not target.exists()
entries = previous['files'] + [{
    'path': prefix + 'par/ddr_demo_ti60/src/platform_uart_rx.v',
    'build_path': 'par/ddr_demo_ti60/src/platform_uart_rx.v'
}]
rows = []
for entry in entries:
    source = repo / entry['path']
    destination = target / entry['build_path']
    destination.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(source, destination)
    rows.append(dict(path=entry['path'], build_path=entry['build_path'],
                     bytes=source.stat().st_size,
                     sha256=hashlib.sha256(source.read_bytes()).hexdigest()))
manifest = dict(base=subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=repo, text=True).strip(),
                files=rows, profile='RTL19 production + independent GPIO6 UART RX; no Stage10 swap')
(work / 'source-manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
print('Prepared', len(rows), 'production inputs', flush=True)
