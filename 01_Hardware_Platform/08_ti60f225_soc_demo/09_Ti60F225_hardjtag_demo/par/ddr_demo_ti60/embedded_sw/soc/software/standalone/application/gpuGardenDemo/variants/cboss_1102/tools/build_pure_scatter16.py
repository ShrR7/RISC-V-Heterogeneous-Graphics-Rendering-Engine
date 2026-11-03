#!/usr/bin/env python3
"""91 nominal2degree Q14 rays; 8..30 slots/ray exit at full-tile boundary.
GPL-3.0-or-later generator. Offline only; no rescale, recolour or board access.
"""
from pathlib import Path
import argparse,hashlib,json,math,struct,zlib
ROOT=Path(__file__).resolve().parents[1]
BASE,MAGIC,KEY=0x009B0000,0x32584D50,0xF81F
MAX_COUNT,RAY_COUNT,Q=2730,91,16384
SPEED_MIN_Q8,SPEED_MAX_Q8,SPEED_DEFAULT_Q8=512,1024,612
PHASE_SLOTS=30
LEGACY_CUT_INDEX=7
FAST_CUT_INDEX=6
FAMILIES=['red_needle','blue_capsule','white_lance','amber_diamond','violet_crescent','pink_star','green_ring']
FORMATS={
 'START':'[PURE_START] generation=%u count=%u speed_q8=%u per_ray=%u width=640 height=480 origin_x=320 origin_y=120 tile=16 sprite_w=16 sprite_h=16 styles=7 tiles=27 rays=91 spread=180 angle_step=2 coord_q=16384 uniform=1 speed_min_q8=512 speed_max_q8=1024 per_ray_min=8 per_ray_max=30 max_count=2730 phase_slots=%u despawn=boundary edge=last_full_tile bottom_fallback=1 state_compact=1 changed_proven=1 telemetry=light route_cut=adaptive_7_6_7 cut6_k=23..29 upper_stage9_cut_degrees=%u stage9_visible_y_lt=480 gpu_bottom_clear_y=464..599 life_min=%u life_max=%u adapted=1 emitter_x=640 emitter_y=136 source_x=320 source_y=16 prep_frames=%u blob_crc=0x%x atlas_crc=0x%x ai=0 collision=0 effects=0 game_steps=0 frame_base=%u\r\n',
 'REPORT':'[PURE] generation=%u frame=%u count=%u speed_q8=%u visible=%u changed=%u screen_now=%u screen_min=%u screen_max=%u s9_count=%u legacy_count=%u gpu_bottom_count=%u family0=%u family1=%u family2=%u family3=%u family4=%u family5=%u family6=%u bucket_max=%u boundary_respawn=%u max_line=%u updates=%u tick_lo=%u tick_hi=%u hz=%u fps10=%u shown_delta=%u engine_delta=%u checksum=0x%x build_us=%u legacy_us=%u present_us=%u miss_px=%u miss_lines=%u reject=%u skip=%u errors=%u guard=%u log_dropped=%u log_truncated=%u\r\n',
 'WARMUP':'[PURE_SETUP] generation=%u target=%u speed_q8=%u per_ray=%u phase_slots=%u source_x=320 source_y=16 rays=91 styles=7 spread=180 angle_step=2 coord_q=16384 uniform=1 despawn=boundary\r\n',
 'PERIOD':'[PURE_PERIOD] generation=%u frame=%u count=%u speed_q8=%u max_ticks=%u over60=%u over3=%u hz=%u\r\n',
 'PREP':'[PURE_PREP] generation=%u speed_q8=%u per_ray=%u command_to_first_ticks=%u previous_to_first_ticks=%u interval1_ticks=%u interval2_ticks=%u max_ticks=%u over3=%u hud_mask=%u\r\n',
 'FAIL':'[PURE_FAIL] generation=%u frame=%u count=%u rc=%d preserve=1\r\n',
 'EXIT':'[PURE_EXIT] reason=T generation=%u frame=%u count=%u errors=%u game_steps=0\r\n',
 'LEFT':'FPGA / BOUNDARY FAN 2 DEGREE',
 'FOOTER':'91 RAYS / 8..30 EACH / SAME SPEED / EDGE EXIT',
 'KEYS':'T RETURN / C8..30 / V512..1024 ENTER',
}
def sha(raw):return hashlib.sha256(raw).hexdigest()
def crc(raw):return zlib.crc32(raw)&0xffffffff
def xorshift(v):
    v^=(v<<13)&0xffffffff;v^=v>>17;v^=(v<<5)&0xffffffff
    return v&0xffffffff
def radial(unit,age,speed):return (unit*age*speed+(1<<21))>>22
def first_outside(ray,speed):
    ax,ay=ray['anchor']
    for age in range(1,512):
        x=320+radial(ray['vx'],age,speed)-ax
        y=16+radial(ray['vy'],age,speed)-ay
        if x<0 or y<0 or x+16>640 or y+16>480:return age
    raise AssertionError('ray has no full-tile boundary exit')
def generate():
    canonical=(ROOT/'assets/pure_27_canonical.bin').read_bytes()
    previous=json.loads((ROOT/'assets/pure_27_canonical.json').read_text(encoding='utf-8-sig'))
    h=struct.unpack_from('<16I',canonical)
    assert h[5:7]==(27,33) and sha(canonical)==previous['blob_sha256']
    meta=canonical[h[8]:h[9]];pixels=canonical[h[11]:h[12]]
    atlas=(ROOT/'assets/atlas.bin').read_bytes()
    assert sha(atlas)==previous['source_atlas_sha256']
    rays=[];members=[[] for _ in range(33)]
    for ray in range(RAY_COUNT):
        nominal=float(ray*2)
        vx=round(Q*math.cos(nominal*math.pi/180));vy=round(Q*math.sin(nominal*math.pi/180))
        actual=math.degrees(math.atan2(vy,vx));bin64=min(32,max(0,math.floor(actual/5.625+0.5)))
        _,packed,_,_=struct.unpack_from('<4I',canonical,h[9]+16*bin64)
        tile=(packed>>16)&255;family=packed>>24;ax=packed&255;ay=(packed>>8)&255
        rays.append(dict(ray=ray,nominal_angle=nominal,actual_angle=actual,bin64=bin64,
            art_angle=bin64*5.625,nominal_angle_error=abs(actual-nominal),art_angle_error=abs(actual-bin64*5.625),
            family=family,tile=tile,source_asset=previous['rays'][bin64]['source_asset'],anchor=[ax,ay],
            vx=vx,vy=vy,ttl=0,phase_offset=0,base_count=22,count=30,packed=f'0x{packed:08X}'))
        members[bin64].append(ray)
    assert len({(r['vx'],r['vy']) for r in rays})==RAY_COUNT
    weights=[22]*RAY_COUNT;populations=[PHASE_SLOTS]*RAY_COUNT
    old_weights=[sum(weights[r] for r in group) for group in members]
    plans=[dict(ray=ray,phase_slot=wave+1) for wave in range(PHASE_SLOTS) for ray in range(RAY_COUNT)]
    legacy_rows=0;fast_legacy_rows=0
    all_lives=[]
    for speed in range(SPEED_MIN_Q8,SPEED_MAX_Q8+1):
        lives=[first_outside(r,speed) for r in rays]
        all_lives.extend(lives)
        for r,life in zip(rays,lives):
            for age in range(life):
                py=radial(r['vy'],age,speed)
                bottom=16+py-r['anchor'][1]+16
                if not LEGACY_CUT_INDEX<=r['ray']<RAY_COUNT-LEGACY_CUT_INDEX:
                    legacy_rows=max(legacy_rows,bottom)
                if not FAST_CUT_INDEX<=r['ray']<RAY_COUNT-FAST_CUT_INDEX:
                    fast_legacy_rows=max(fast_legacy_rows,bottom)
    assert legacy_rows==91 and fast_legacy_rows==80
    ray_data=b''.join(struct.pack('<4I',(r['vx']&65535)|((r['vy']&65535)<<16),int(r['packed'],16),0,r['base_count']|(r['phase_offset']<<16)) for r in rays)
    plan_data=b''.join(struct.pack('<HH',p['ray'],p['phase_slot']) for p in plans)
    meta_offset,ray_offset=64,64+27*16;plan_offset=ray_offset+RAY_COUNT*16
    pixel_offset=plan_offset+MAX_COUNT*4;string_offset=pixel_offset+27*512
    strings=dict(FORMATS)
    strings['KEYS']+=' '*((-sum(len(t.encode('ascii'))+1 for t in strings.values()))%4)
    string_data=bytearray();string_records={}
    for name,text in strings.items():
        raw=text.encode('ascii')+b'\0'
        string_records[name]=dict(offset=string_offset+len(string_data),bytes=len(raw),sha256=sha(raw))
        string_data.extend(raw)
    assert (string_offset+len(string_data))%4==0
    payload=meta+ray_data+plan_data+pixels+bytes(string_data)
    values=[MAGIC,2,64+len(payload),len(payload),crc(payload),27,RAY_COUNT,MAX_COUNT,meta_offset,ray_offset,plan_offset,pixel_offset,string_offset,len(string_data),320|(16<<16),PHASE_SLOTS]
    blob=struct.pack('<16I',*values)+payload
    assert len(blob)<65536 and BASE+len(blob)<0x009C0000
    names=['magic','revision','total_bytes','payload_bytes','payload_crc','tile_count','ray_count','plan_count','tile_meta_offset','ray_meta_offset','plan_offset','pixel_offset','strings_offset','strings_bytes','source_packed','phase_slots']
    header=['/* Generated91-ray/unitQ14 radial ABI; canonical27game tiles unchanged. */','#ifndef PURE_BULLET_SCATTER16_H','#define PURE_BULLET_SCATTER16_H','#include <stdint.h>',
      f'#define PURE_SCATTER_BASE 0x{BASE:08X}U',f'#define PURE_SCATTER_MAGIC 0x{MAGIC:08X}U',f'#define PURE_SCATTER_BYTES {len(blob)}U',f'#define PURE_SCATTER_PAYLOAD_BYTES {len(payload)}U',f'#define PURE_SCATTER_PAYLOAD_CRC 0x{crc(payload):08X}U',f'#define PURE_SCATTER_BLOB_CRC 0x{crc(blob):08X}U',
      '#define PURE_SCATTER_TILE_COUNT 27U','#define PURE_SCATTER_RAY_COUNT 91U','#define PURE_SCATTER_FAMILY_COUNT 7U','#define PURE_SCATTER_BUCKET 256U',
      f'#define PURE_SCATTER_META_OFFSET {meta_offset}U',f'#define PURE_SCATTER_RAY_OFFSET {ray_offset}U',f'#define PURE_SCATTER_PLAN_OFFSET {plan_offset}U',f'#define PURE_SCATTER_PIXEL_OFFSET {pixel_offset}U',f'#define PURE_SCATTER_PHASE_SLOTS {PHASE_SLOTS}U',f'#define PURE_UNIFORM_LEGACY_ROWS {legacy_rows}U',f'#define PURE_FAST_LEGACY_ROWS {fast_legacy_rows}U',
      'typedef struct { uint32_t '+','.join(names)+'; } PureBulletScatterHeader;',
      'typedef struct {uint32_t asset,packed,source_crc,tile_crc;} PureBulletTileMeta;',
      'typedef struct {uint32_t velocity,packed,fixed_ttl,reserved;} PureBulletRayMeta;',
      'typedef struct {uint16_t ray,phase_slot;} PureBulletPlan;',
      '_Static_assert(sizeof(PureBulletScatterHeader)==64U,"header ABI");','_Static_assert(sizeof(PureBulletRayMeta)==16U,"ray ABI");',
      '_Static_assert(sizeof(PureBulletTileMeta)==16U,"tile metadata ABI");','_Static_assert(sizeof(PureBulletPlan)==4U,"plan ABI");']
    for name,r in string_records.items():header.append(f'#define PURE_SCATTER_TEXT_{name} ((const char *)(uintptr_t)(PURE_SCATTER_BASE+{r["offset"]}U))')
    header.extend(['#endif','']);header_raw='\n'.join(header).encode('utf-8')
    for r in rays:r['boundary_lifetime_default']=first_outside(r,SPEED_DEFAULT_Q8)
    family_counts=[0]*7;tile_counts=[0]*27
    for r in rays:family_counts[r['family']]+=r['count'];tile_counts[r['tile']]+=r['count']
    actual=[r['actual_angle'] for r in rays]
    report=dict(schema=2,purpose='91 distinct moving trajectories,nominal2degree,180degree source and exact27previous game adaptations',adapted=True,
      blob_path='assets/pure_scatter16.bin',blob_bytes=len(blob),blob_sha256=sha(blob),blob_crc32=f'0x{crc(blob):08X}',load_address=f'0x{BASE:08X}',header=dict(zip(names,values)),
      count=27,tile_count=27,ray_count=RAY_COUNT,plan_count=MAX_COUNT,payload_bytes=len(payload),payload_crc32=f'0x{crc(payload):08X}',source_atlas_sha256=sha(atlas),source_png_unmodified=True,normal_game_atlas_unmodified=True,
      meta_offset=meta_offset,ray_offset=ray_offset,plan_offset=plan_offset,pixel_offset=pixel_offset,tile_bytes=512,format_strings=string_records,key_rgb565='0xF81F',families=FAMILIES,family_counts=family_counts,tile_counts=tile_counts,plan_counts=populations,plan_frames=PHASE_SLOTS,plans=plans,styles=previous['styles'],rays=rays,
      canonical_source='assets/pure_27_canonical.bin',canonical_sha256=sha(canonical),canonical27tile_pixels_identical=True,
      coordinate_shift=14,coordinate_one=16384,target_speed_pixels_per_new_frame=SPEED_DEFAULT_Q8/256,
      uniform=True,per_ray=22,per_ray_min=8,per_ray_max=30,max_count=MAX_COUNT,phase_slots=PHASE_SLOTS,despawn_policy='boundary',edge_policy='last_full_16x16_tile',fixed_ttl=0,
      bottom_fallback=True,state_compact=True,changed_proven=True,telemetry_mode='light_periodic',upper_stage9_cut_degrees=14,gpu_upper_per_ray=14,route_cut_rule='cut7 for k8..22 and k30; cut6 for k23..29',fast_cut_per_ray_min=23,fast_cut_per_ray_max=29,fast_upper_stage9_cut_degrees=12,fast_gpu_upper_per_ray=12,stage9_visible_global_y_exclusive=480,gpu_bottom_clear_global_y=[464,600],
      screen_now_semantics='count of sprites routed to a video-visible RTL19 path after full-tile bounds checks; Stage9 only if global top_y+16<=480, otherwise keyed legacy GPU framebuffer',
      speed_min_q8=SPEED_MIN_Q8,speed_max_q8=SPEED_MAX_Q8,speed_default_q8=SPEED_DEFAULT_Q8,
      boundary_lifetime_min_all_speeds=min(all_lives),boundary_lifetime_max_all_speeds=max(all_lives),
      boundary_lifetime_default_by_ray=[r['boundary_lifetime_default'] for r in rays],
      legacy_cut_degrees=14,legacy_clear_local_rows=legacy_rows,fast_legacy_cut_degrees=12,fast_legacy_clear_local_rows=fast_legacy_rows,
      stage9_no_fallback_baseline_count=(RAY_COUNT-2*LEGACY_CUT_INDEX)*22,legacy_base_count=2*LEGACY_CUT_INDEX*22,
      angle_step_degrees=2,spread_degrees=180,source_point_local=[320,16],source_point_global=[640,136],
      actual_direction_error_max=max(r['nominal_angle_error'] for r in rays),art_direction_error_max=max(r['art_angle_error'] for r in rays),
      actual_neighbor_angle_min=min(actual[i+1]-actual[i] for i in range(RAY_COUNT-1)),actual_neighbor_angle_max=max(actual[i+1]-actual[i] for i in range(RAY_COUNT-1)),
      base_ray_counts=weights,canonical_bin_base_totals=old_weights,canonical_bin_members=members,phase_seed=None,
      phase_rule='for each selected k in8..30, every ray uses slots0..k-1 initialized at floor(slot*first_outside_age/k); each advances at common speed_q8/256 and returns to emitter only when its next full tile would cross the playfield boundary',
      count_rule='91*k border-valid instances for selected per-ray k=8..30; upper cut7 Stage9 and physical y480 GPU fallback route totals vary and sum to count',
      header_path='src/pure_bullet_scatter16.h',header_sha256=sha(header_raw),generator_path='tools/build_pure_scatter16.py',generator_sha256=sha(Path(__file__).read_bytes()),hardware_access=False)
    return {'assets/pure_scatter16.bin':blob,'assets/pure_scatter16.json':(json.dumps(report,ensure_ascii=False,indent=2)+'\n').encode('utf-8'),'src/pure_bullet_scatter16.h':header_raw}
def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--check',action='store_true');a=parser.parse_args()
    for name,raw in generate().items():
        path=ROOT/name
        if a.check:assert path.read_bytes()==raw,name
        else:path.write_bytes(raw)
    print('UNIFORM_TWO_DEGREE '+('CHECK PASS' if a.check else 'GENERATED')+':91distinct Q14 rays,27unchanged tiles')
if __name__=='__main__':main()
