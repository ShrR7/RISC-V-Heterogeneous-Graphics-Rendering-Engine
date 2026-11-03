#!/usr/bin/env python3
"""Compact production PB/EB position interpolation contracts. Host only.
SPDX-License-Identifier: GPL-3.0-or-later
"""
import argparse,hashlib,json,re,subprocess
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def run(cmd,log):
 r=subprocess.run(cmd,capture_output=True,text=True);out=r.stdout+r.stderr;log.write_text(out,encoding='utf-8')
 if r.returncode:print(out,end='');raise RuntimeError(f'exit{r.returncode}: {cmd}')
 return out

def main():
 ap=argparse.ArgumentParser(description=__doc__)
 ap.add_argument('--compiler',default='D:/mingw64/bin/gcc.exe')
 ap.add_argument('--riscv-compiler',default='D:/Efinity/efinity-riscv-ide-2026.1/toolchain/bin/riscv-none-elf-gcc.exe')
 ap.add_argument('--output-dir',type=Path,default=ROOT/'build/host/motion')
 a=ap.parse_args();build=a.output_dir.resolve();build.mkdir(parents=True,exist_ok=True)
 reference=ROOT.parent/'neon_heading_precision_20261015'
 frozen=('garden_core.c','garden_core.h','garden_assets.c','garden_assets.h','garden_fx.c','garden_fx.h',
         'garden_combat.c','garden_combat.h',
         'garden_scene.c','garden_scene.h','garden_actor_ui.c','garden_actor_ui.h','garden_feedback.c','garden_feedback.h')
 for f in frozen:assert (ROOT/'src'/f).read_bytes()==(reference/'src'/f).read_bytes(),f
 assert (ROOT/'assets/atlas.bin').read_bytes()==(reference/'assets/atlas.bin').read_bytes()
 sources=('garden_motion.c','garden_fx.c','garden_feedback.c','garden_scene.c','garden_combat.c',
          'garden_core.c','garden_assets.c','garden_heading_precision.c','garden_actor_ui.c')
 exe=build/'test.exe';cmd=[a.compiler,'-std=c99','-O2','-DGARDEN_MOTION_HOST_Q4_PROBE','-Wall','-Wextra','-Werror','-I',str(ROOT/'src'),str(ROOT/'tools/test_motion.c')]
 cmd += [str(ROOT/'src'/f) for f in sources]+['-lm','-o',str(exe)]
 run(cmd,build/'compile_host.log');out=run([str(exe)],build/'test_motion.log');print(out,end='')
 line=re.search(r'^SUMMARY (.+)$',out,re.M);assert line
 metrics={k:int(v) for k,v in re.findall(r'(\w+)=(\d+)',line.group(1))}
 assert metrics['real_steps']==12000 and metrics['state_bytes']<=2752 and metrics['slot_bytes']==12
 obj=build/'motion.o';assembly=build/'motion.s'
 rv=[a.riscv_compiler,'-std=c99','-Os','-Wall','-Wextra','-Werror','-march=rv32imac','-mabi=ilp32','-ffunction-sections','-fdata-sections','-I',str(ROOT/'src')]
 object_cmd=rv+['-c',str(ROOT/'src/garden_motion.c'),'-o',str(obj)]
 assembly_cmd=rv+['-S',str(ROOT/'src/garden_motion.c'),'-o',str(assembly)]
 run(object_cmd,build/'compile_rv32_object.log');run(assembly_cmd,build/'compile_rv32_assembly.log')
 nm=Path(a.riscv_compiler).with_name('riscv-none-elf-nm.exe')
 undefined=run([str(nm),'--undefined-only',str(obj)],build/'rv32_undefined.log')
 assert not re.search(r'__(?:[u]?divdi|[u]?moddi|add[df]|sub[df]|mul[df]|fix|float)',undefined)
 asm=assembly.read_text();q4_asm=re.search(r'^q4:\n(.+?)^\s*\.size\s+q4,',asm,re.M|re.S)
 assert q4_asm and 'mulhu' in q4_asm.group(1)
 assert not re.search(r'^\s*(?:divu?|remu?|call|tail)\s',q4_asm.group(1),re.M),'Q4 contains division/remainder/helper instead of high multiply'
 (build/'q4_rv32.s.txt').write_text(q4_asm.group(1),encoding='utf-8')
 assert not re.search(r'^\s*f(?:add|sub|mul|div|cvt|sqrt)[.\w]*\s',asm,re.M)
 size=Path(a.riscv_compiler).with_name('riscv-none-elf-size.exe')
 sizes=run([str(size),str(obj)],build/'rv32_size.log');parts=sizes.splitlines()[1].split()
 text,data,bss=map(int,parts[:3]);increment=text+data+bss+metrics['state_bytes']
 assert increment<6144,('module alone exceeds6KiB compact budget',increment)
 files=tuple('src/'+f for f in sources)+tuple('src/'+f for f in ('garden_motion.h','garden_render.h','garden_combat.h','garden_scene.h','garden_actor_ui.h','garden_core.h','garden_assets.h','garden_fx.h','garden_feedback.h','garden_heading_precision.h'))+('tools/test_motion.c','tools/verify_motion.py')
 report={'status':'PASS','host_only':True,'board_acceptance':False,'metrics':metrics,'compiler_command':cmd,
         'rv32_object_command':object_cmd,'rv32_assembly_command':assembly_cmd,
         'rv32_motion_text_bytes':text,'rv32_motion_data_bytes':data,'rv32_motion_bss_bytes':bss,
         'module_code_plus_state_bytes':increment,'no_float_or_64bit_division_helpers':True,
         'q4_exhaustive_raw_points':4096129,'q4_bit_exact_to_original_division':True,'q4_RV32_MULHU_no_div_rem_or_calls':True,
         'rv32_undefined_symbols':undefined.splitlines(),'rv32_assembly_sha256':sha(assembly),
         'frozen_core_assets_existing_presentation_byte_identical_to_P64':True,'heading_binary_search_and_exact_matcher_intentionally_optimized':True,'atlas_sha256':sha(ROOT/'assets/atlas.bin'),
         'source_sha256':{f:sha(ROOT/f) for f in files},
         'checks':['All537600draw candidate combinations validate original helper type/colour/direction ID; no wrong atlas ID is admitted by the new two-boundary predicate.',
                   'All4096129raw values -2048064..2048064 plus signed32 extremes: exact original converter acceptance/result/rejected-output sentinel, while productionQ4 uses high multiply and no division.',
                   '12byte fixed per-slot Q4 previous/current cache for216 real PB/EB, no full-game copies or new draw entries.',
                   'Scheduled60,57.6,34.2,32.7Hz independent timestamp trajectories, multi-step catchup, negative/diagonal motion; interpolated position error<1pixel.',
                   'Constant60Hz v=6pixels/logicstep changes0/6/6 into approximately4pixels/frame after a valid previous/current pair.',
                   'Birth/age<=1, death, slot age/type/colour reuse, pause/same-tick, explicit reset, rollback, mode epoch, teleport and skipped observations fail closed.',
                   'Signed32 malformed positions rejected before Q4 packing; every capacity0..250, null pointers and both leading/tail canaries.',
                   'Actual12000-step core+combat+scene+bank+HP draw pipeline retains64heading/fivecolour IDs, alpha, list order, model/HP/head damage/marker positions and all core bytes.',
                   'DecorativeSHOT entries with nonmatching coordinates/opacity and entries after the real PB pool are untouched.',
                   'ProductionRV32IMAC-Os object has no floating-point or64bit divide/remainder helper; code+fixed state<6KiB.'],
         'limits':['Previous/current interpolation deliberately delays bullet appearance by at most25ms. Core collision and damage remain at the current40Hz state.',
                   'Only PB/EB positions are interpolated. Actors, HP bars, independent fixed-birth head damage, real focus marker and camera policy remain unchanged.',
                   'Q4 endpoint precision is1/16pixel; final nearest pixel plus Q8 interpolation error remains<1pixel for tested physically bounded trajectories.',
                   'Teleport guard conservatively snaps from63.875pixel Q4 displacement so every true>64pixel jump is rejected despite endpoint quantization.',
                   'Apply must run before camera/death fragment insertion; it recognizes the production stable pool ordering plus exact unsmoothed position/ID/alpha.',
                   'Identical additionalSHOT entries inserted before real PBs cannot carry distinct semantic identity in the existing GardenDraw format; the specified production insertion point prevents that ambiguity.',
                   'Object-level code+state budget excludes root main/log/other module changes, alignment and final linked SDK placement; root must check final RAM.',
                   'No FPGA, physical display, GPU/DDR timing or full main scheduler is executed by these host contracts.']}
 (build/'verification.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
 print('PASS motion host report:',build/'verification.json')
if __name__=='__main__':main()
