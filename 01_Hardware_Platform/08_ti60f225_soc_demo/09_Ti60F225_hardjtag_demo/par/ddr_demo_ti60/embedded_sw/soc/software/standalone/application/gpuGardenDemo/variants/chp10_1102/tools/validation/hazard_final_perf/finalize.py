import hashlib,json,difflib,re
from pathlib import Path
H=Path(__file__).resolve().parent
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest().upper()
raw_input='2C4759A4D745D34E9766691ABF095A6011AA59D7CF44C98120A31B3150B9762F'
for sub in [H,H/'sat_o2',H/'ai_box']:
    p=sub/'verification.json';r=json.loads(p.read_text())
    r['original_W_runtime_source_SHA256']=raw_input
    r['generated_reference_line_ending_note']='Original runtime source uses LF; Windows text output stores CRLF experiment snapshots. Byte-normalized text is identical; exact SDK reference ELF SHA matches deployed 7A984335...'
    if sub.name=='sat_o2':r['change']='auto_avoid and general segment_box definitions both receive optimize("O2"); not selected'
    if sub.name=='ai_box':
        r['selected']=True
        r['independent_SAT']={'comparisons_per_optimization':2083521,'compilations':['-Os','-O2'],
          'outside_guard_range_or_boundary_inputs_per_compilation':1054960,'oracles':['unchanged general segment_box','independent signed __int128'],
          'input_range':'[-2000000000,2000000000], plus exhaustive17^4 boundary grid and1million within guarded range',
          'scope':'No gameplay collision path changed; helper used only for automatic pilot EB forecast.'}
        r['range_proof']={'guard_each_coordinate_absolute_max':10000000,'max_abs_mx_my_dx_dy':20000000,
          'max_abs_single_product':400000000000000,'max_abs_cross_difference':800000000000000,
          'max_abs32_sum':40000000,'max_extent':1280000000000,
          'guard_arithmetic':'((uint32_t)coordinate +10000000U)>20000000U; unsigned arithmetic is defined even at INT32 extrema',
          'outside_guard':'Original general segment_box; fixed bounding/endpoint checks are identical before guard.'}
        r['RV32_stack_observation']={'reference_garden_step_frame':304,'reference_segment_box_frame':64,
           'candidate_garden_step_frame':160,'candidate_auto_avoid_frame':288,'candidate_segment_box_fallback_frame':64,
           'candidate_path_frames_sum':512,'stack_region_bytes':4096,
           'limits':'Observed named-function frames, not a complete whole-program maximum stack proof.'}
    p.write_text(json.dumps(r,indent=2)+'\n')
ref=(H/'ai_box/reference_core.c').read_text();candidate=(H/'ai_box/optimized_core.c').read_text()
patch=''.join(difflib.unified_diff(ref.splitlines(True),candidate.splitlines(True),fromfile='a/src/garden_core.c',tofile='b/src/garden_core.c',n=4))
(H/'ai_box/minimal.patch').write_text(patch)
readme='''FINAL PERFORMANCE CANDIDATE (offline; no hardware access)

SELECTED: ai_box/. Only three source edits are required:
1. Insert ai_box_patch.txt verbatim immediately before the auto_avoid definition.
   The TXT contains ONLY the helper and range-proof comment, no auto_avoid body.
2. Add __attribute__((optimize("O2"))) to the auto_avoid definition only:
   static void __attribute__((optimize("O2"))) auto_avoid(const GardenGame *g,int32_t *vx,int32_t *vy)
   Its earlier forward declaration may remain unchanged.
3. Within auto_avoid's enemy-bullet inner loop only, replace
   segment_box(ax,ay,bx,by,-16000,-16000,16000,16000)
   with ai_bullet_segment_box(ax,ay,bx,by).
   Do not replace the same text in the helper's rare fallback!

ai_box/minimal.patch shows exactly those changes. Do NOT copy the complete
optimized_core.c over a newer strict-wave core: the whole-file candidate is
an immutable R2 experiment snapshot, not a runtime installation mechanism.

Validation:
- Original runtime core SHA2C4759A4...; Windows CRLF snapshots normalize original LF.
- Reference SDK -Os -flto relink exactly reproduced the deployed ELF SHA
  7A98433558AF425F17B7033054D53CACA1C47EEE4FEBD3390A8B39B57C613A5B.
-140000 full-Game per-tick comparisons (8seeds plus mixed manual controls),
  and6000 decision scenes all match every byte/action.
-2083521 independent SAT comparisons, both Os/O2, match general reference
  and128-bit oracle, including large-coordinate fallback and exact boundaries.
- RV32 RAM123216->123984 (+768B); data/bss unchanged;2992B free.
- Observed function-frame path512B (step160+AI288+fallback64),4KiB stack kept.
- Host dense snapshot1.87x, synthetic1.47x. Host timing is NOT a board-FPS
  acceptance and must not be presented as measured RV32 acceleration.

No runtime source, FPGA, UART, DDR, Flash or protected evidence was changed.
Existing W non-core build objects were read-only and SHA-checked after relink.
Freeze: do not relink again using W objects after root installs strict-wave.
'''
(H/'README.txt').write_text(readme)
files=[p for p in H.rglob('*') if p.is_file() and p.name!='evidence_manifest.json']
manifest={'status':'FROZEN_OFFLINE_CANDIDATE','selected':'ai_box','hardware_access':False,'runtime_modified':False,
 'original_runtime_core_SHA256':raw_input,'files':{p.relative_to(H).as_posix():sha(p) for p in sorted(files)}}
(H/'evidence_manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
print(json.dumps({'helper_SHA256':sha(H/'ai_box_patch.txt'),'minimal_patch_SHA256':sha(H/'ai_box/minimal.patch'),
 'verification_SHA256':sha(H/'ai_box/verification.json'),'manifest_SHA256':sha(H/'evidence_manifest.json')},indent=2))
