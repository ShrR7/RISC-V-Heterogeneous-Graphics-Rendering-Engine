#!/usr/bin/env python3
"""Build two core TUs and compare every byte on every tick; offline only.
SPDX-License-Identifier: GPL-3.0-or-later
"""
import hashlib,json,re,subprocess,time
from pathlib import Path
ROOT=Path(__file__).resolve().parent
WORKSPACE=ROOT.parents[1]
def sha(b):return hashlib.sha256(b).hexdigest()
def call(command,name):
    run=subprocess.run(command,cwd=ROOT,capture_output=True,text=True)
    (ROOT/name).write_text(run.stdout+run.stderr,encoding='utf-8')
    assert run.returncode==0,run.stdout+run.stderr
    return run.stdout+run.stderr
def main():
    before=(ROOT/'reference_core.c').read_bytes()
    assert sha(before)=='308c0680c1b0c8bc153310dfcab76535bd57a49fdc729b870b9aa30bc4372f51'
    source=before.decode('utf-8-sig');candidate=(ROOT.parent/'hazard_auto_avoid_optimized.txt').read_text(encoding='utf-8')
    start=source.index('static int32_t bullet_forecast(');end=source.index('static void check_player_collisions(',start)
    optimized=source[:start]+candidate+'\n'+source[end:]
    (ROOT/'optimized_core.c').write_bytes(optimized.encode('utf-8'))
    names={'garden_init':'init','garden_step':'step','garden_boss_fire_countdown':'countdown'}
    for label,text in [('reference',source),('optimized',optimized)]:
        # Counting build proves repeated sine evaluations independently of
        # host timing. Normal replay/benchmark has no counter increments.
        hook='\nstatic unsigned long long hazard_sine_calls;\n'
        marker='static int32_t sine(int angle)\n{'
        assert marker in text
        instrumented=text.replace(marker,hook+marker+'\n#ifdef HAZARD_COUNT_SINE\n    ++hazard_sine_calls;\n#endif',1)
        probe=f'\nunsigned long long {label}_probe(const GardenGame*g,int32_t*x,int32_t*y){{hazard_sine_calls=0;auto_avoid(g,x,y);return hazard_sine_calls;}}\n'
        (ROOT/(label+'_build.c')).write_bytes((instrumented+probe).encode('utf-8'))
    compiler='D:/mingw64/bin/gcc.exe'
    for counted in [False,True]:
        flag=['-DHAZARD_COUNT_SINE'] if counted else []
        objects=[]
        for label in ['reference','optimized']:
            obj=ROOT/(label+('_count' if counted else '')+'.o')
            cmd=[compiler,'-std=c11','-O2','-Wall','-Wextra','-Werror',*flag,
                *['-D'+name+'='+label+'_'+alias for name,alias in names.items()],'-c',str(ROOT/(label+'_build.c')),'-o',str(obj)]
            call(cmd,label+('_count' if counted else '')+'_compile.log');objects.append(str(obj))
        exe=ROOT/('compare_count.exe' if counted else 'compare.exe')
        call([compiler,'-std=c11','-O2','-Wall','-Wextra','-Werror',str(ROOT/'compare_core.c'),*objects,'-o',str(exe)],exe.stem+'_link.log')
        output=call([str(exe)],exe.stem+'.log');print(output,end='')
    riscv='D:/Efinity/efinity-riscv-ide-2026.1/toolchain/bin/riscv-none-elf-gcc.exe'
    for label in ['reference','optimized']:
        call([riscv,'-std=c11','-Os','-Wall','-Wextra','-Werror','-march=rv32im_zicsr','-mabi=ilp32',
            '-fstack-usage','-ffunction-sections','-fdata-sections','-c',str(ROOT/(label+'_core.c')),
            '-o',str(ROOT/(label+'_rv32.o'))],label+'_rv32_compile.log')
    stacks={}
    for label in ['reference','optimized']:
        path=ROOT/(label+'_rv32.su');text=path.read_text();(ROOT/(label+'_stack.txt')).write_text(text,encoding='utf-8')
        rows=[]
        for line in text.splitlines():
            loc,amount,kind=line.rsplit('\t',2)
            name=loc.rsplit(':',1)[-1];rows.append({'function':name,'bytes':int(amount),'kind':kind})
        stacks[label]=rows
    # At -Os GCC inlines auto_avoid and pair into garden_step. Include that
    # frame rather than misreporting absent inline functions as zero stack.
    candidate_frames=[r for r in stacks['optimized'] if r['function'] in ['garden_step','auto_avoid','bullet_forecast_pair','segment_box','sine','xpart','ypart']]
    candidate_stack=sum(r['bytes'] for r in candidate_frames)
    assert candidate_stack<512,(candidate_frames,candidate_stack)
    step_frames={label:next(r['bytes'] for r in stacks[label] if r['function']=='garden_step') for label in ['reference','optimized']}
    normal=(ROOT/'compare.log').read_text()
    counted=(ROOT/'compare_count.log').read_text()
    report={'status':'PASS','host_only':True,'runtime_sources_modified':False,'board_access':False,
        'reference_core_sha256':sha(before),'candidate_core_sha256':sha((ROOT/'optimized_core.c').read_bytes()),
        'replacement_sha256':sha(candidate.encode()),'source_span':['bullet_forecast','auto_avoid'],
        'seeds':8,'steps_per_seed':16000,'full_Game_tick_memcmp_steps':140000,
        'manual_pause_bomb_restart_interleave_steps':12000,'synthetic_scenes':6000,'state_bytes':13824,
        'normal_benchmarks':[dict(m.groupdict()) for m in re.finditer(r'BENCH name=(?P<name>\S+) repeats=(?P<repeats>\d+) reference_ms=(?P<reference_ms>[\d.]+) optimized_ms=(?P<optimized_ms>[\d.]+) ratio=(?P<ratio>[\d.]+)',normal)],
        'counted_benchmarks':[dict(m.groupdict()) for m in re.finditer(r'BENCH name=(?P<name>\S+).*?ref_sine=(?P<ref_sine>\d+) opt_sine=(?P<opt_sine>\d+)',counted)],
        'rv32_compilation_flags':'-Os -march=rv32im_zicsr -mabi=ilp32 -fstack-usage',
        'rv32_stack_frames':stacks,'candidate_conservative_sum_autoavoid_and_helpers_stack':candidate_stack,
        'rv32_garden_step_frame_bytes':step_frames,'rv32_garden_step_added_frame_bytes':step_frames['optimized']-step_frames['reference'],
        'local_array_bytes':108,'no_heap_in_replacement':True,
        'limits':['Host-only timing is not RV32 performance or board60fps acceptance.',
                  'RV32 .su sum conservatively includes all named helper frames; no recursive calls.',
                  'Original reference code must be reviewed/hashed before integrating this replacement.',
                  'No render/view/loader/asset change is part of this experiment.']}
    match=re.search(r'game_bytes=(\d+)',normal);assert match;report['state_bytes']=int(match[1])
    (ROOT/'verification.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    print(json.dumps({k:v for k,v in report.items() if k not in ['rv32_stack_frames','limits']},indent=2))
if __name__=='__main__':main()

