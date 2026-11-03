#!/usr/bin/env python3
"""Independent complete-pixel, descriptor, pivot, alias and ABI verification.
SPDX-License-Identifier: GPL-3.0-or-later. Host-only; no board access.
"""
import argparse, csv, hashlib, json, math, struct, subprocess, zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
KEY = 0xf81f

def sha(raw):
    return hashlib.sha256(raw).hexdigest()

def read_json(path):
    return json.loads(path.read_text(encoding='utf-8-sig'))

def write_json(path, value):
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2)+'\n', encoding='utf-8')

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--reference', type=Path, default=ROOT.parent/'neon_damage_sum_20261023')
    parser.add_argument('--compiler', default='D:/mingw64/bin/gcc.exe')
    args = parser.parse_args()
    build = ROOT/'build/host/bullet_scale'
    build.mkdir(parents=True, exist_ok=True)
    manifest = read_json(ROOT/'assets/manifest.json')
    before = read_json(args.reference/'assets/manifest.json')
    gen = read_json(ROOT/'assets/bullet_scale_generation.json')
    oracle = read_json(ROOT/'assets/bullet_scale_anchor_oracle.json')
    blob = (ROOT/'assets/atlas.bin').read_bytes()
    old = (args.reference/'assets/atlas.bin').read_bytes()
    checks = 0
    def check(condition, detail=''):
        nonlocal checks
        checks += 1
        if not condition:
            raise AssertionError(detail)
    check(sha(old) == '0cc11bcb3c864cda952d544bfec97ca5f6a85962a938f759094b7b68048af232')
    check(len(blob) == manifest['bytes'] == gen['atlas_bytes'] == 614916)
    check(len(blob) <= 0xBA000)
    check(sha(blob) == manifest['sha256'] == gen['atlas_sha256'])
    check(f'0x{zlib.crc32(blob):08X}' == manifest['crc32'] == gen['atlas_crc32'])
    check(len(manifest['sprites']) == len(before['sprites']) == 859)
    changed = {4, 9, 10, 11} | set(range(12, 92)) | set(range(280, 840))
    check(set(gen['changed_ids']) == changed and len(changed) == 644)
    check({r['id'] for r in oracle['records']} == changed)
    # Compile the actual generated metadata and exercise both public angle APIs.
    source = build/'compiled_asset_probe.c'
    source.write_text(r'''#include <stdio.h>
#include "garden_assets.h"
int main(void) {
    unsigned tests=0;
    if(sizeof(garden_sprite_t)!=8u || GARDEN_ASSET_COUNT!=859) return 1;
    for(unsigned i=0;i<GARDEN_ASSET_COUNT;i++) {
        const garden_sprite_t*s=garden_asset(i);
        if(!s) return 2;
        printf("%u,%u,%u,%u,%u,%d,%d\n",i,s->offset,s->width,s->height,garden_sprite_stride(s),s->anchor_x,s->anchor_y);
        unsigned enemy=(i>=12u && i<92u)||(i>=280u && i<840u);
        if(garden_asset_is_enemy_bullet(i)!=enemy) return 3;
        tests++;
    }
    if(garden_asset(859u)!=0 || garden_asset(0xffffffffu)!=0) return 4;
    for(unsigned type=1;type<=2;type++) for(unsigned colour=1;colour<=5;colour++) for(unsigned angle=0;angle<1024;angle++) {
        unsigned r=((angle+8u)&1023u)/16u;
        unsigned expect;
        if(r%8u==0u) expect=(type==2u?52u:12u)+(colour-1u)*8u+r/8u;
        else expect=280u+(type==2u?280u:0u)+(colour-1u)*56u+r-r/8u-1u;
        if(garden_bullet_precision_id(type,colour,angle)!=expect) return 5;
        unsigned oldexpect=(type==2u?52u:12u)+(colour-1u)*8u+((angle+64u)&1023u)/128u;
        if(garden_bullet_id(type,colour,angle)!=oldexpect) return 6;
        if(garden_bullet_asset(type,colour,angle)!=garden_asset(oldexpect)) return 7;
        tests+=3;
    }
    if(garden_bullet_precision_id(1,0,0)!=garden_bullet_precision_id(1,3,0)) return 8;
    if(garden_bullet_id(2,6,0)!=garden_bullet_id(2,3,0)) return 9;
    fprintf(stderr,"PASS compiled metadata api_checks=%u bytes=%u crc=%08X sha=%s\n",tests,GARDEN_ATLAS_BYTES,GARDEN_ATLAS_CRC32,garden_atlas_sha256);
    return 0;
}
''', encoding='utf-8')
    exe = build/'compiled_asset_probe.exe'
    command = [args.compiler, '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror',
               '-I', str(ROOT/'src'), str(source), str(ROOT/'src/garden_assets.c'), '-o', str(exe)]
    compile_result = subprocess.run(command, capture_output=True, text=True)
    (build/'compile.log').write_text(compile_result.stdout+compile_result.stderr, encoding='utf-8')
    check(compile_result.returncode == 0, compile_result.stdout+compile_result.stderr)
    result = subprocess.run([str(exe)], capture_output=True, text=True)
    (build/'compiled_descriptors.csv').write_text(result.stdout, encoding='utf-8')
    (build/'test_api.log').write_text(result.stderr, encoding='utf-8')
    check(result.returncode == 0, result.stderr)
    checks += 859 + 10240 * 3
    check(f'bytes={len(blob)} crc={zlib.crc32(blob):08X} sha={sha(blob)}' in result.stderr)
    compiled = [list(map(int, row)) for row in csv.reader(result.stdout.splitlines())]
    check(len(compiled) == 859)
    records = {r['id']: r for r in oracle['records']}
    physical = {}
    aliases = {}
    unchanged = 0
    scaled_pixels = 0
    white_max = 0.0
    for i, (sprite, parent) in enumerate(zip(manifest['sprites'], before['sprites'])):
        check(sprite['id'] == parent['id'] == i)
        check(sprite['name'] == parent['name'])
        fields = ('id', 'offset', 'width', 'height', 'stride', 'anchor_x', 'anchor_y')
        check(compiled[i] == [sprite[k] for k in fields], ('compiled', i))
        w, h = sprite['width'], sprite['height']
        pw, ph = parent['width'], parent['height']
        payload = blob[sprite['offset']:sprite['offset']+sprite['bytes']]
        parent_payload = old[parent['offset']:parent['offset']+parent['bytes']]
        check(sprite['offset'] % 4 == 0 and sprite['offset']+sprite['bytes'] <= len(blob))
        check(sprite['bytes'] == len(payload) == 2*w*h and sprite['stride'] == 2*w)
        if 'rgb565_sha256' in sprite:
            check(sha(payload) == sprite['rgb565_sha256'])
        values = struct.unpack('<'+'H'*(w*h), payload)
        parent_values = struct.unpack('<'+'H'*(pw*ph), parent_payload)
        physical.setdefault(sprite['offset'], (sprite['bytes'], payload))
        check(physical[sprite['offset']] == (sprite['bytes'], payload))
        aliases.setdefault(parent['offset'], sprite['offset'])
        check(aliases[parent['offset']] == sprite['offset'])
        if i not in changed:
            check(payload == parent_payload, ('unchanged payload', i))
            for key in ('width', 'height', 'stride', 'anchor_x', 'anchor_y'):
                check(sprite[key] == parent[key], ('unchanged geometry', i, key))
            unchanged += 1
            continue
        check(w == (5*pw+2)//4 and h == (5*ph+2)//4, ('dimensions', i))
        # Inverse-enumeration oracle: every original pixel occupies precisely
        # the integer destination preimage of its source cell.
        visited = set()
        for sy in range(ph):
            ys = [y for y in range(h) if y*4//5 == sy]
            for sx in range(pw):
                xs = [x for x in range(w) if x*4//5 == sx]
                for y in ys:
                    for x in xs:
                        index = y*w+x
                        check(values[index] == parent_values[sy*pw+sx], ('pixel', i, x, y))
                        check(index not in visited)
                        visited.add(index)
        check(len(visited) == w*h)
        scaled_pixels += len(visited)
        for axis, limit in (('x', w), ('y', h)):
            original = parent['anchor_'+axis]
            preimage = [d for d in range(limit) if d*4//5 == original]
            check(bool(preimage))
            check(sprite['anchor_'+axis] == (preimage[0]+preimage[-1])//2, ('pivot', i, axis))
        ax, ay = sprite['anchor_x'], sprite['anchor_y']
        check(values[ay*w+ax] == 0xffff, ('white pivot', i))
        check(set(values).issubset(set(parent_values)), ('new colour', i))
        white = [(j%w, j//w) for j, pixel in enumerate(values) if pixel == 0xffff]
        cx = sum(x for x, y in white)/len(white)
        cy = sum(y for x, y in white)/len(white)
        white_max = max(white_max, math.hypot(cx-ax, cy-ay))
        record = records[i]
        check(record['descriptor_anchor'] == [ax, ay])
        check(record['rgb565_sha256'] == sha(payload))
        check(record['white_pixels'] == len(white))
        check(record['key_pixels'] == values.count(KEY))
        if i != 4:
            check(sprite['offset'] >= 0x3c000, ('extended DDR', i))
    check(unchanged == 215)
    check(len(physical) == len(aliases) == 670)
    check(len(set(aliases.values())) == len(aliases), 'original separate groups newly aliased')
    spans = sorted((offset, offset+n) for offset, (n, payload) in physical.items())
    for previous, following in zip(spans, spans[1:]):
        check(previous[1] <= following[0], 'partial overlaps')
        check(all(v == 0 for v in blob[previous[1]:following[0]]), 'nonzero padding')
    # Generated numeric metadata changes, while public helper implementations
    # and actual collision/trajectory/state-machine sources remain identical.
    tail = 'const garden_sprite_t *garden_asset(unsigned id)'
    check((ROOT/'src/garden_assets.c').read_text(encoding='utf-8-sig').split(tail, 1)[1] ==
          (args.reference/'src/garden_assets.c').read_text(encoding='utf-8-sig').split(tail, 1)[1])
    gameplay_files = ['garden_core.c', 'garden_core.h', 'garden_combat.c', 'garden_combat.h',
                      'garden_motion.c', 'garden_motion.h', 'garden_animation.c', 'garden_animation.h',
                      'garden_render.c', 'garden_render.h', 'garden_scene.c', 'garden_scene.h',
                      'garden_actor_ui.c', 'garden_actor_ui.h', 'garden_ordnance_view.c', 'garden_ordnance_view.h']
    for name in gameplay_files:
        check((ROOT/'src'/name).read_bytes() == (args.reference/'src'/name).read_bytes(), ('gameplay changed', name))
    rebuild = subprocess.run([str(Path(__import__('sys').executable)), '-X', 'utf8', '-B',
        str(ROOT/'tools/build_bullet_scale_atlas.py'), '--reference', str(args.reference), '--check'], capture_output=True, text=True)
    (build/'rebuild.log').write_text(rebuild.stdout+rebuild.stderr, encoding='utf-8')
    check(rebuild.returncode == 0, rebuild.stdout+rebuild.stderr)
    generated = read_json(ROOT/'assets/runtime_checksums.json')['files']
    runtime_paths = ['assets/atlas.bin', 'assets/atlas.sha256', 'assets/manifest.json',
        'assets/bullet_scale_generation.json', 'assets/bullet_scale_anchor_oracle.json', 'assets/NOTICE.txt',
        'src/garden_assets.c', 'src/garden_assets.h', 'tools/build_bullet_scale_atlas.py', 'tools/verify_bullet_scale_assets.py']
    for name in runtime_paths:
        raw = (ROOT/name).read_bytes()
        check(generated[name] == dict(bytes=len(raw), sha256=sha(raw)), ('runtime checksum', name))
    largest = max((s['width']*s['height'], s['id'], s['width'], s['height']) for s in manifest['sprites'] if 12<=s['id']<92 or 280<=s['id']<840)
    report = dict(status='PASS', checks=checks, hardware_access=False,
        variant=ROOT.name, reference_variant=args.reference.name,
        scale=[5,4], changed_ids=sorted(changed), scaled_sprites=644, scaled_pixel_samples=scaled_pixels,
        unchanged_sprites=unchanged, all_rgb565_samples_exact=True, color_key_preserved=True,
        exact_white_source_pivots_preserved=True, white_centroid_max_distance_px=white_max,
        old_offset_alias_groups_preserved=670, all_64_direction_api_mappings_preserved=True,
        actual_compiled_descriptor_verification=True, public_helper_implementations_unchanged=True,
        collision_and_gameplay_sources_unchanged=True, gameplay_files=gameplay_files,
        atlas_bytes=len(blob), atlas_sha256=sha(blob), atlas_crc32=f'0x{zlib.crc32(blob):08X}',
        budget_bytes=0xBA000, remaining_budget_bytes=0xBA000-len(blob),
        largest_enemy_sprite=dict(id=largest[1], width=largest[2], height=largest[3], area=largest[0]),
        deterministic_rebuild=True,
        source_sha256={name:sha((ROOT/'src'/name).read_bytes()) for name in ['garden_assets.c', 'garden_assets.h']},
        tool_sha256={name:sha((ROOT/'tools'/name).read_bytes()) for name in ['build_bullet_scale_atlas.py', 'verify_bullet_scale_assets.py']},
        runtime_checksums_sha256=sha((ROOT/'assets/runtime_checksums.json').read_bytes()),
        full_game_board_acceptance='NOT_PERFORMED_BY_HOST_ASSET_TEST')
    write_json(build/'verification.json', report)
    print(json.dumps({k:report[k] for k in ('status','checks','scaled_sprites','scaled_pixel_samples','unchanged_sprites','atlas_bytes','atlas_sha256','atlas_crc32','largest_enemy_sprite')}, indent=2))

if __name__ == '__main__':
    main()
