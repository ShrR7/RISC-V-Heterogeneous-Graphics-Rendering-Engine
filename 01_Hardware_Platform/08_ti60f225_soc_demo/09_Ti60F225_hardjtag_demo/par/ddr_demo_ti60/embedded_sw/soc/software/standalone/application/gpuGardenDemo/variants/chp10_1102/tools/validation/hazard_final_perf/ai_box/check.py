"""Read-only W performance experiment: isolated outputs, never hardware.
SPDX-License-Identifier: GPL-3.0-or-later
"""
import hashlib,json,shutil,subprocess,re,time
from pathlib import Path
HERE=Path(__file__).resolve().parent
ROOT=HERE.parents[2]
W=Path(json.loads((ROOT/'06_Controller_Dependencies/hazard_paths.json').read_text(encoding='utf-8-sig'))['hazard'])
APP=W.parents[1]
def sha(p): return hashlib.sha256(p.read_bytes()).hexdigest().upper()
def call(cmd,name,cwd=HERE):
    p=subprocess.run([str(x) for x in cmd],cwd=cwd,capture_output=True,text=True)
    (HERE/name).write_text(p.stdout+p.stderr,encoding='utf-8')
    if p.returncode: raise RuntimeError(name+'\n'+p.stdout+p.stderr)
    return p.stdout+p.stderr
def main():
    assert sha(W/'src/garden_core.c')=='2C4759A4D745D34E9766691ABF095A6011AA59D7CF44C98120A31B3150B9762F'
    source=(W/'src/garden_core.c').read_text(encoding='utf-8-sig')
    marker='static void auto_avoid(const GardenGame *g,int32_t *vx,int32_t *vy)\n{'
    assert source.count(marker)==1
    candidate=source.replace(marker,'static void __attribute__((optimize("O2"))) auto_avoid(const GardenGame *g,int32_t *vx,int32_t *vy)\n{',1)
    helper=(HERE.parent/'ai_box_patch.txt').read_text()
    candidate=candidate.replace('static void __attribute__((optimize("O2"))) auto_avoid(',helper+'\nstatic void __attribute__((optimize("O2"))) auto_avoid(',1)
    old='segment_box(ax,ay,bx,by,-16000,-16000,16000,16000)'
    # The helper's fallback must remain general; replace only auto_avoid.
    start=candidate.index('static void __attribute__((optimize("O2"))) auto_avoid(')
    candidate=candidate[:start]+candidate[start:].replace(old,'ai_bullet_segment_box(ax,ay,bx,by)',1)
    (HERE/'reference_core.c').write_text(source,encoding='utf-8')
    (HERE/'optimized_core.c').write_text(candidate,encoding='utf-8')
    shutil.copy2(W/'src/garden_core.h',HERE/'garden_core.h')
    shutil.copy2(ROOT/'06_Controller_Dependencies/hazard_perf_check/compare_core.c',HERE/'compare_core.c')
    gcc='D:/mingw64/bin/gcc.exe'; rv='D:/Efinity/efinity-riscv-ide-2026.1/toolchain/bin/riscv-none-elf-'
    names={'garden_init':'init','garden_step':'step','garden_boss_fire_countdown':'countdown'}
    objs=[]
    for label,text in [('reference',source),('optimized',candidate)]:
        probe=f'\nunsigned long long {label}_probe(const GardenGame*g,int32_t*x,int32_t*y){{auto_avoid(g,x,y);return 0;}}\n'
        (HERE/(label+'_build.c')).write_text(text+probe,encoding='utf-8')
        obj=HERE/(label+'.o');objs.append(obj)
        call([gcc,'-std=c11','-Os','-Wall','-Wextra','-Werror',*['-D'+n+'='+label+'_'+a for n,a in names.items()],'-c',HERE/(label+'_build.c'),'-o',obj],label+'_compile.log')
    exe=HERE/'compare.exe'
    call([gcc,'-std=c11','-Os','-Wall','-Wextra','-Werror',HERE/'compare_core.c',*objs,'-o',exe],'compare_link.log')
    output=call([exe],'compare.log');print(output,end='')
    flags=['-I../../../../bsp/efinix/EfxSapphireSoc/include','-I../../../../bsp/efinix/EfxSapphireSoc/app',
       '-DSYSTEM_UART_A_APB=SYSTEM_UART_0_IO_APB','-DSYSTEM_GPIO_A_APB=SYSTEM_GPIO_0_IO_APB','-DSYSTEM_I2C_A_APB=SYSTEM_I2C_0_IO_APB',
       '-Os','-march=rv32im_zicsr_zifencei','-mabi=ilp32','-DUSE_GP','-fcommon','-std=gnu99','-Wall','-Wextra','-Werror',
       '-Wno-unused-function','-Wno-unused-parameter','-Wno-comment','-Wno-unused-but-set-variable',
       '-I'+str(W/'src'),'-I../../include','-I../../driver','-ffunction-sections','-fdata-sections','-flto']
    refobjs=sorted((W/'build/obj_files').glob('*.o'))
    before={str(p):sha(p) for p in refobjs}
    linkinfo={}
    for label in ['reference','optimized']:
        obj=HERE/(label+'_rv32.o')
        call([rv+'gcc.exe','-c',*flags,'-fstack-usage','-o',obj,HERE/(label+'_core.c')],label+'_rv32_compile.log',APP)
        objects=[obj if p.name=='garden_core.o' else p for p in refobjs]
        elf=HERE/(label+'.elf');mapfile=HERE/(label+'.map')
        output=call([rv+'gcc.exe',*flags,'-o',elf,*objects,'-march=rv32im_zicsr_zifencei','-mabi=ilp32','-lc','-L../../common',
            '-specs=nosys.specs','-lgcc','-nostartfiles','-ffreestanding',
            '-Wl,-Bstatic,-T,../../../../bsp/efinix/EfxSapphireSoc/linker/default.ld,-Map,'+str(mapfile)+',--print-memory-usage,--no-warn-rwx-segment,--gc-sections','-lm'],label+'_link.log',APP)
        size=call([rv+'size.exe',elf],label+'_size.log')
        call([rv+'objdump.exe','-d',elf],label+'.asm')
        headers=call([rv+'readelf.exe','-l','--wide',elf],label+'_headers.log')
        load=re.search(r'^\s*LOAD\s+0x\w+\s+0x(\w+)\s+0x\w+\s+0x\w+\s+0x(\w+)',headers,re.M)
        assert load
        used=int(load[2],16); free=0x20000-int(load[1],16)-used
        linkinfo[label]={'ELF_SHA256':sha(elf),'load_mem_bytes':used,'free_bytes':free,'size':size,'map':str(mapfile)}
        print(label,output,size)
    assert all(sha(Path(p))==s for p,s in before.items())
    assert sha(W/'src/garden_core.c')=='2C4759A4D745D34E9766691ABF095A6011AA59D7CF44C98120A31B3150B9762F'
    report={'status':'PASS','runtime_sources_modified':False,'hardware_access':False,'reference_source_SHA256':sha(HERE/'reference_core.c'),
       'candidate_source_SHA256':sha(HERE/'optimized_core.c'),'change':'auto_avoid O2 plus exact fixed-square AI-only SAT with guarded general fallback',
       'full_Game_memcmp_steps':140000,'synthetic_decision_scenes':6000,'links':linkinfo,
       'limits':['Host timings are not RV32 or board FPS. Candidate is not board-accepted.','Full SDK LTO relink reuses and verifies unchanged current W non-core objects.']}
    (HERE/'verification.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    print(json.dumps(report,indent=2))
if __name__=='__main__':main()
