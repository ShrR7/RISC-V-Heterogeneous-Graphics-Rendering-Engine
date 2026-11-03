"""Read retained RTL19 failure evidence, or explicitly rearm before a NEW case."""
from pathlib import Path
import argparse,subprocess,json,hashlib,struct
V=Path(__file__).resolve().parents[1]
GDB=Path('D:/Efinity/efinity-riscv-ide-2026.1/toolchain/bin/riscv-none-elf-gdb.exe')
p=argparse.ArgumentParser();p.add_argument('--out',type=Path,required=True);p.add_argument('--rearm',action='store_true');p.add_argument('--execute',action='store_true');a=p.parse_args()
if not a.execute:print(json.dumps({'hardware_access':False,'rearm':a.rearm,'flash':False}));raise SystemExit()
if not (V/'logs/openocd.pid').read_text().strip().isdigit():raise ValueError('Owned OpenOCD record missing')
out=a.out.resolve();out.mkdir(parents=True,exist_ok=False)
commands=['set confirm off','set mem inaccessible-by-default off','set arch riscv:rv32','set remotetimeout 60',
 'target extended-remote 127.0.0.1:13333','monitor halt',
 'if *(unsigned int*)0xf8100100 != 0x42554c39 || *(unsigned int*)0xf8100150 != 0x10007 || *(unsigned int*)0xf8100180 != 0x464d4953',
 'monitor resume','disconnect','quit 1','end']
if a.rearm:
 commands += ['if (*(unsigned int*)0xf8100184 & 4) != 0','monitor resume','disconnect','quit 2','end',
   'set *(unsigned int*)0xf8100188=0x52454152',
   'printf "[R2_REARM] ctrl=0x%x status=0x%x\\n", *(unsigned int*)0xf8100104, *(unsigned int*)0xf8100184']
else:
 commands += ['dump binary memory stage9_regs.bin 0xf8100100 0xf810015c',
   'dump binary memory first_missing.bin 0xf8100180 0xf8100234',
   'dump binary memory history_header.bin 0xf8100280 0xf8100290',
   'dump binary memory history_context.bin 0xf8100300 0xf81003c0',
   'dump binary memory history.bin 0xf8101000 0xf8101800',
   'printf "[R2_FAULT] alpha_reg=0x%x s9_missing_px=%u missing_lines=%u freeze_status=0x%x\\n", *(unsigned int*)0xf8100124, *(unsigned int*)0xf810012c, *(unsigned int*)0xf8100130, *(unsigned int*)0xf8100184']
commands += ['monitor resume','disconnect','quit']
script=out/'capture.gdb';script.write_text('\n'.join(commands)+'\n',encoding='ascii')
with (out/'gdb.log').open('w',encoding='utf-8') as f:
 r=subprocess.run([str(GDB),'-batch','-x',str(script)],cwd=out,stdout=f,stderr=subprocess.STDOUT,timeout=60,
    creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0))
if r.returncode:raise RuntimeError('Capture/rearm failed; original GDB log retained')
files={p.name:{'bytes':p.stat().st_size,'sha256':hashlib.sha256(p.read_bytes()).hexdigest()} for p in out.glob('*.bin')}
if not a.rearm:
 words=struct.unpack('<23I',(out/'stage9_regs.bin').read_bytes());diag=struct.unpack('<45I',(out/'first_missing.bin').read_bytes())
 result={'mode':words[1],'status':words[3],'descriptor_count':words[6],'alpha_reg':words[9],
  'missing_px':words[11],'missing_lines':words[12],'shown':words[13],'engine':words[14],
  'skip':words[15],'max_line':words[17],'reject':words[18],'revision':words[20],
  'freeze_status':diag[1],'freeze_words':list(diag[4:44]),'files':files,'writes':False}
else:result={'rearm':True,'only_write':'0xf8100188=0x52454152','flash':False}
(out/'result.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result))
