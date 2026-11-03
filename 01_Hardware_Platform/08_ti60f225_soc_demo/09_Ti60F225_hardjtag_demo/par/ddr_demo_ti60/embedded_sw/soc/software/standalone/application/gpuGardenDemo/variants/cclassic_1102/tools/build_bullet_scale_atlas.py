#!/usr/bin/env python3
"""Offline exact-RGB565 5/4 projectile enlargement. No board access.
SPDX-License-Identifier: GPL-3.0-or-later
"""
import argparse, copy, hashlib, json, re, struct, zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PARENT_SHA = '0cc11bcb3c864cda952d544bfec97ca5f6a85962a938f759094b7b68048af232'
BUDGET = 0xBA000
CHANGED = {4, 9, 10, 11} | set(range(12, 92)) | set(range(280, 840))

def sha(raw):
    return hashlib.sha256(raw).hexdigest()

def jb(value):
    return (json.dumps(value, ensure_ascii=False, indent=2) + '\n').encode('utf-8')

def read_json(path):
    return json.loads(path.read_text(encoding='utf-8-sig'))

def enlarged_size(size):
    return (5 * size + 2) // 4

def enlarged_anchor(anchor):
    # Midpoint of integer destination pixels mapping to this source pivot.
    first = (5 * anchor + 3) // 4
    last = (5 * (anchor + 1) + 3) // 4 - 1
    return (first + last) // 2

def enlarge(payload, width, height):
    nw, nh = enlarged_size(width), enlarged_size(height)
    out = bytearray()
    for y in range(nh):
        sy = min(height - 1, y * 4 // 5)
        for x in range(nw):
            sx = min(width - 1, x * 4 // 5)
            at = 2 * (sy * width + sx)
            out.extend(payload[at:at + 2])
    return bytes(out), nw, nh

def build(reference):
    old = (reference / 'assets/atlas.bin').read_bytes()
    before = read_json(reference / 'assets/manifest.json')
    assert sha(old) == PARENT_SHA == before['sha256']
    sprites = copy.deepcopy(before['sprites'])
    assert len(sprites) == 859 and len(CHANGED) == 644
    groups = {}
    for sprite in sprites:
        groups.setdefault(sprite['offset'], []).append(sprite)
    atlas = bytearray()
    records = []
    for old_offset, members in sorted(groups.items()):
        first = members[0]
        payload = old[old_offset:old_offset + first['bytes']]
        scaled = first['id'] in CHANGED
        for sprite in members:
            assert (sprite['id'] in CHANGED) == scaled
            for key in ('bytes', 'width', 'height', 'anchor_x', 'anchor_y'):
                assert sprite[key] == first[key], (old_offset, key)
        width, height = first['width'], first['height']
        if scaled:
            payload, width, height = enlarge(payload, width, height)
        while len(atlas) & 3:
            atlas.append(0)
        offset = len(atlas)
        atlas.extend(payload)
        for sprite in members:
            sprite['offset'] = offset
            if not scaled:
                continue
            old_sprite = before['sprites'][sprite['id']]
            ax = enlarged_anchor(sprite['anchor_x'])
            ay = enlarged_anchor(sprite['anchor_y'])
            sprite.update(width=width, height=height, stride=2 * width,
                          anchor_x=ax, anchor_y=ay, bytes=len(payload),
                          rgb565_sha256=sha(payload))
            values = struct.unpack('<' + 'H' * (width * height), payload)
            whites = [(i % width, i // width) for i, c in enumerate(values) if c == 0xffff]
            assert whites and values[ay * width + ax] == 0xffff
            sums = [sum(p[0] for p in whites), sum(p[1] for p in whites)]
            record = dict(id=sprite['id'], name=sprite['name'],
                          parent_offset=old_offset, offset=offset,
                          parent_size=[old_sprite['width'], old_sprite['height']],
                          size=[width, height],
                          parent_anchor=[old_sprite['anchor_x'], old_sprite['anchor_y']],
                          descriptor_anchor=[ax, ay],
                          parent_rgb565_sha256=sha(old[old_offset:old_offset + old_sprite['bytes']]),
                          rgb565_sha256=sha(payload), white_pixels=len(whites),
                          white_centroid_sums=sums,
                          white_centroid=[sums[0] / len(whites), sums[1] / len(whites)],
                          key_pixels=values.count(0xf81f))
            sprite['visual_scale'] = dict(numerator=5, denominator=4,
                source_pixel='floor(4*destination/5), clamped at final source pixel',
                size='nearest integer: (5*dimension+2)//4',
                anchor='lower integer midpoint of exact source-anchor preimage',
                parent_size=record['parent_size'], parent_anchor=record['parent_anchor'],
                parent_rgb565_sha256=record['parent_rgb565_sha256'],
                collision_geometry_changed=False)
            # Keep original-art provenance, but refresh runtime pixel facts.
            source = sprite.setdefault('source', {})
            if isinstance(source, dict):
                for key in ('descriptor_anchor', 'rgb565_sha256', 'white_pixels',
                            'white_centroid_sums', 'white_centroid'):
                    source[key] = copy.deepcopy(record[key])
                source.pop('white_core_error_px', None)
                source['postprocess'] = copy.deepcopy(sprite['visual_scale'])
            records.append(record)
    final = bytes(atlas)
    assert len(final) == 614916 <= BUDGET
    digest, crc = sha(final), f'0x{zlib.crc32(final):08X}'
    records.sort(key=lambda r: r['id'])
    report = dict(schema='garden-projectile-scale-v1',
        status='HOST_ASSET_PREPARED_NOT_BOARD_ACCEPTANCE', variant=ROOT.name,
        reference_variant=reference.name, reference_atlas_sha256=sha(old),
        reference_atlas_bytes=len(old), atlas_bytes=len(final), atlas_sha256=digest,
        atlas_crc32=crc, budget_bytes=BUDGET, remaining_budget_bytes=BUDGET-len(final),
        scale=[5, 4], changed_ids=sorted(CHANGED), changed_count=644,
        enemy_projectile_ids=640, legacy_circle_ids=[9, 10, 11], player_shot_id=4,
        unchanged_sprites=215, physical_blob_groups=len(groups),
        original_alias_groups_preserved=True, source_palette_exact=True,
        runtime_scaling=False, collision_rule_changes=False,
        laser_bomb_heal_effects_changed=False,
        dimensions_rule='(5*old_dimension+2)//4',
        pixel_rule='source_x=floor(4*dest_x/5); source_y=floor(4*dest_y/5)',
        anchor_rule='midpoint of integer destination preimage of original pivot',
        records=records)
    manifest = copy.deepcopy(before)
    manifest.update(variant=ROOT.name, bytes=len(final), sha256=digest, crc32=crc,
        sprites=sprites, generator='tools/build_bullet_scale_atlas.py',
        reference_variant=reference.name, reference_atlas_sha256=sha(old),
        remaining_budget_bytes=BUDGET-len(final), visual_scale=[5, 4],
        original_ids_scope='All 859 IDs retained; 644 ordinary projectile sprites enlarged 5/4; 215 other payloads and geometry unchanged',
        bullet_scale_generation='assets/bullet_scale_generation.json')
    c = (reference / 'src/garden_assets.c').read_text(encoding='utf-8-sig')
    c = c.replace('Generated by tools/build_variety_atlas.py', 'Generated by tools/build_bullet_scale_atlas.py')
    c = re.sub(r'(garden_atlas_sha256\[65\] = ")[0-9a-f]+(";)', lambda m: m[1]+digest+m[2], c)
    rows = ['    {%du, %du, %du, %d, %d}, /* %d: %s */' %
            (s['offset'], s['width'], s['height'], s['anchor_x'], s['anchor_y'], s['id'], s['name']) for s in sprites]
    c, count = re.subn(r'(static const garden_sprite_t sprites\[GARDEN_ASSET_COUNT\] = \{\n).*?(\n\};)',
        lambda m: m[1]+'\n'.join(rows)+m[2], c, count=1, flags=re.S)
    assert count == 1
    h = (reference / 'src/garden_assets.h').read_text(encoding='utf-8-sig')
    h = h.replace('Run tools/build_variety_atlas.py', 'Run tools/build_bullet_scale_atlas.py')
    h = re.sub(r'#define GARDEN_ATLAS_BYTES \d+u', f'#define GARDEN_ATLAS_BYTES {len(final)}u', h)
    h = re.sub(r'#define GARDEN_ATLAS_CRC32 0x[0-9A-Fa-f]+u', f'#define GARDEN_ATLAS_CRC32 {crc}u', h)
    outputs = {'assets/atlas.bin': final, 'assets/atlas.sha256': (digest+'  atlas.bin\n').encode(),
        'assets/manifest.json': jb(manifest), 'assets/bullet_scale_generation.json': jb(report),
        'assets/bullet_scale_anchor_oracle.json': jb(dict(schema='source-pivot-preserving-scale-v1', records=records)),
        'src/garden_assets.c': c.encode(), 'src/garden_assets.h': h.encode()}
    notice = (reference / 'assets/NOTICE.txt').read_text(encoding='utf-8-sig')
    notice += '\nProjectile enlargement / neon_bullet_large_20261023 / 2026-10-23\n'
    notice += '644 ordinary projectile sprite IDs enlarged offline by 5/4 using exact RGB565 nearest-neighbour samples. Original user-provided art, license and attribution remain as above. All 215 non-projectile payloads, laser/bomb/heal/effects, logical collision rules and original source PNGs are unchanged. Imported PNG previews and older generation reports are historical parent assets; atlas.bin and bullet_scale_generation.json describe this runtime.\n'
    outputs['assets/NOTICE.txt'] = notice.encode()
    checks = copy.deepcopy(read_json(reference/'assets/runtime_checksums.json').get('files', {}))
    for path, raw in outputs.items():
        checks[path] = dict(bytes=len(raw), sha256=sha(raw))
    for path in ('tools/build_bullet_scale_atlas.py', 'tools/verify_bullet_scale_assets.py'):
        raw = (ROOT / path).read_bytes()
        checks[path] = dict(bytes=len(raw), sha256=sha(raw))
    outputs['assets/runtime_checksums.json'] = jb(dict(format='sha256', atlas_crc32=crc, files=checks))
    return outputs

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--reference', type=Path, default=ROOT.parent/'neon_damage_sum_20261023')
    parser.add_argument('--check', action='store_true', help='Rebuild in memory and compare, without writes')
    args = parser.parse_args()
    outputs = build(args.reference)
    for name, raw in outputs.items():
        path = ROOT / name
        if args.check:
            assert path.read_bytes() == raw, name
        else:
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(raw)
    print(json.dumps(dict(status='PASS', mode='check' if args.check else 'build',
        atlas_bytes=len(outputs['assets/atlas.bin']), atlas_sha256=sha(outputs['assets/atlas.bin']),
        generated_files=sorted(outputs)), indent=2))

if __name__ == '__main__':
    main()
