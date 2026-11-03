
import argparse,hashlib,json,shutil,subprocess,tempfile
from pathlib import Path
here=Path(__file__).resolve().parent.parent
root=next(p for p in here.parents if (p/'06_Controller_Dependencies/batch_paths.json').exists())
work=root/'06_Controller_Dependencies/batch_driver_host'
work.mkdir(exist_ok=True)
for name in ('gpu_driver.c','gpu_driver.h','gpu_regs.h'): shutil.copyfile(here/'src'/name,work/name)
shutil.copyfile(here/'tools/test_batch_driver_host.c',work/'test_batch_driver_host.c')
(work/'vexriscv.h').write_text('static inline void data_cache_invalidate_all(void) {}\n')
cmd=['D:/mingw64/bin/gcc.exe','-std=c11','-O2','-Wall','-Wextra','-Werror','-I.', 'test_batch_driver_host.c','-o','test_batch_driver_host.exe']
compile=subprocess.run(cmd,cwd=work,text=True,capture_output=True)
if compile.returncode: print(compile.stdout+compile.stderr);raise SystemExit(compile.returncode)
run=subprocess.run([str(work/'test_batch_driver_host.exe')],cwd=work,text=True,capture_output=True)
print(run.stdout+run.stderr,end='')
files=['src/gpu_driver.c','src/gpu_driver.h','src/gpu_regs.h','tools/test_batch_driver_host.c','tools/verify_batch_driver_host.py']
parent=Path(json.loads((root/'06_Controller_Dependencies/batch_paths.json').read_text(encoding='utf-8-sig'))['coherent'])
unchanged={name:(here/'src'/name).read_text().startswith((parent/'src'/name).read_text()) for name in ('gpu_driver.c','gpu_driver.h','gpu_regs.h')}
report={'status':'PASS' if run.returncode==0 and all(unchanged.values()) else 'FAIL','stdout':run.stdout,'stderr':run.stderr,'legacy_single_prefix_unchanged':unchanged,'source_sha256':{x:hashlib.sha256((here/x).read_bytes()).hexdigest().upper() for x in files},'test_scope':'host MMIO protocol/validation/cache ordering; no hardware execution or AXI fault injection','limits':'caller allocation/tail readability and external DDR writers remain caller responsibility'}
out=here/'build/host/batch_driver';out.mkdir(parents=True,exist_ok=True)
(out/'verification.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
(out/'host.log').write_text(run.stdout+run.stderr,encoding='utf-8')
raise SystemExit(0 if report['status']=='PASS' else 1)
