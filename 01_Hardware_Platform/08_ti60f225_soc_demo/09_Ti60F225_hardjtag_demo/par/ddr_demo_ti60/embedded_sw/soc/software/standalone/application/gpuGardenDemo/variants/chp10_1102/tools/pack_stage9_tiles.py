#!/usr/bin/env python3
"""Pack lossless existing enemy RGB565 sprites into an external DDR tile library.

No image conversion is performed. All original source pixels and pivots remain
exact. The runtime copies selected 16x16 tiles from this immutable library into
the Stage9 32-slot atlas; neither metadata nor the tile pool consumes CPU SRAM.
Run without options to regenerate, or --check for a deterministic read-only
comparison of the checked files against the current original atlas/metadata.
"""
from __future__ import annotations

import argparse
import collections
import hashlib
import json
from pathlib import Path
import re
import struct
import zlib

BASE = 0x00900000
LIMIT = 0x00100000
KEY = 0xF81F
ASSETS = 859
META = struct.Struct('<IHHhhHH')
ENTRY = struct.Struct('<HBB')
SIDE = 16
TILE_BYTES = SIDE * SIDE * 2


def sha(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest().upper()


def enemy_id(ident: int) -> bool:
    return 12 <= ident < 92 or 280 <= ident < 840


def metadata(source: str) -> list[dict]:
    rows = []
    expression = r'\{(\d+)u, (\d+)u, (\d+)u, (-?\d+), (-?\d+)\}, /\* (\d+): ([^*]+) \*/'
    for match in re.finditer(expression, source):
        offset, width, height, ax, ay, ident = map(int, match.groups()[:6])
        assert ident == len(rows), ('non-contiguous source asset IDs', ident)
        rows.append(dict(id=ident, offset=offset, width=width, height=height,
                         anchor_x=ax, anchor_y=ay, name=match[7].strip()))
    assert len(rows) == ASSETS, ('unexpected source asset count', len(rows))
    return rows


def pack(root: Path) -> tuple[bytes, str, dict]:
    atlas_path = root / 'assets/atlas.bin'
    source_path = root / 'src/garden_assets.c'
    original = atlas_path.read_bytes()
    source = source_path.read_bytes()
    sprites = metadata(source.decode('utf-8-sig'))
    unique: list[bytes] = []
    tile_ids: dict[bytes, int] = {}
    entries: list[tuple[int, int, int]] = []
    records = []
    rows = []
    all_key = struct.pack('<H', KEY) * (SIDE * SIDE)
    for sprite in sprites:
        ident = sprite['id']
        width, height = sprite['width'], sprite['height']
        start = len(entries)
        if enemy_id(ident):
            assert 0 < width <= 48 and 0 < height <= 48, sprite
            end = sprite['offset'] + width * height * 2
            assert end <= len(original), sprite
            pixels = original[sprite['offset']:end]
            for sy in range(0, height, SIDE):
                for sx in range(0, width, SIDE):
                    tile = bytearray(all_key)
                    for row in range(min(SIDE, height - sy)):
                        count = min(SIDE, width - sx) * 2
                        pos = ((sy + row) * width + sx) * 2
                        tile[row * SIDE * 2:row * SIDE * 2 + count] = pixels[pos:pos + count]
                    tile_bytes = bytes(tile)
                    if tile_bytes == all_key:
                        continue
                    tile_id = tile_ids.get(tile_bytes)
                    if tile_id is None:
                        tile_id = len(unique)
                        assert tile_id < 65536
                        tile_ids[tile_bytes] = tile_id
                        unique.append(tile_bytes)
                    entries.append((tile_id, sx, sy))
        count = len(entries) - start
        records.append((start if count else 0, width, height,
                        sprite['anchor_x'], sprite['anchor_y'], count, 0))
        if enemy_id(ident):
            assert count > 0, ('empty enemy sprite', sprite)
            rows.append(dict(id=ident, name=sprite['name'], width=width, height=height,
                             anchor_x=sprite['anchor_x'], anchor_y=sprite['anchor_y'],
                             first_entry=start, count=count,
                             tiles=[dict(unique_tile_id=t, source_x=x, source_y=y)
                                    for t, x, y in entries[start:]]))
    meta_offset = 0
    entry_offset = len(records) * META.size
    pixel_offset = (entry_offset + len(entries) * ENTRY.size + 511) & ~511
    blob = b''.join(META.pack(*row) for row in records)
    blob += b''.join(ENTRY.pack(*row) for row in entries)
    blob += bytes(pixel_offset - len(blob))
    blob += b''.join(unique)
    assert len(blob) < LIMIT, ('library exceeds reserved DDR', len(blob))
    assert BASE + len(blob) < 0x00A00000
    assert not (pixel_offset & 15)

    # Independently decode the packed blob, reconstruct the entire padded source
    # rectangle, and compare every original pixel plus every outer padding pixel.
    compared_pixels = 0
    mismatches = 0
    padding_pixels = 0
    for sprite in sprites:
        ident = sprite['id']
        first, width, height, ax, ay, count, reserved = META.unpack_from(blob, ident * META.size)
        assert (width, height, ax, ay, reserved) == (sprite['width'], sprite['height'],
                sprite['anchor_x'], sprite['anchor_y'], 0)
        if not enemy_id(ident):
            assert count == 0 and first == 0
            continue
        padded_w, padded_h = (width + 15) & ~15, (height + 15) & ~15
        rebuilt = bytearray(struct.pack('<H', KEY) * (padded_w * padded_h))
        locations = set()
        for index in range(first, first + count):
            tid, x, y = ENTRY.unpack_from(blob, entry_offset + index * ENTRY.size)
            assert tid < len(unique) and x in (0, 16, 32) and y in (0, 16, 32)
            assert x < width and y < height and (x, y) not in locations
            locations.add((x, y))
            tile = blob[pixel_offset + tid * TILE_BYTES:pixel_offset + (tid + 1) * TILE_BYTES]
            assert len(tile) == TILE_BYTES and tile != all_key
            for row in range(SIDE):
                target = ((y + row) * padded_w + x) * 2
                rebuilt[target:target + SIDE * 2] = tile[row * SIDE * 2:(row + 1) * SIDE * 2]
        for y in range(padded_h):
            for x in range(padded_w):
                actual = struct.unpack_from('<H', rebuilt, (y * padded_w + x) * 2)[0]
                if x < width and y < height:
                    expected = struct.unpack_from('<H', original,
                                sprite['offset'] + (y * width + x) * 2)[0]
                else:
                    expected = KEY
                    padding_pixels += 1
                compared_pixels += 1
                mismatches += actual != expected
    assert mismatches == 0

    crc = zlib.crc32(blob) & 0xffffffff
    header = f'''/* Generated by tools/pack_stage9_tiles.py; lossless RGB565, no CPU tables. */
#ifndef STAGE9_TILES_H
#define STAGE9_TILES_H
#include <stdint.h>
#define ST9_TILES_BASE 0x{BASE:08X}U
#define ST9_TILES_META_OFFSET {meta_offset}U
#define ST9_TILES_ENTRY_OFFSET {entry_offset}U
#define ST9_TILES_PIXEL_OFFSET {pixel_offset}U
#define ST9_TILES_ASSET_COUNT {len(records)}U
#define ST9_TILES_ENTRY_COUNT {len(entries)}U
#define ST9_TILES_UNIQUE_COUNT {len(unique)}U
#define ST9_TILES_BLOB_BYTES {len(blob)}U
#define ST9_TILES_CRC32 0x{crc:08X}U
#define ST9_TILES_KEY 0x{KEY:04X}U
#define ST9_TILES_TILE_BYTES {TILE_BYTES}U
#define ST9_TILES_SOURCE_SHA256 "{sha(original)}"
#define ST9_TILES_BLOB_SHA256 "{sha(blob)}"
typedef struct {{
    uint32_t first_entry; /* Entry index, not a byte offset. */
    uint16_t width, height;
    int16_t anchor_x, anchor_y;
    uint16_t count, reserved;
}} Stage9TileAsset;
typedef struct {{
    uint16_t unique_tile_id;
    uint8_t source_x, source_y; /* Source rectangle offset: 0,16,32. */
}} Stage9TileEntry;
_Static_assert(sizeof(Stage9TileAsset)==16U,"Stage9 tile metadata ABI");
_Static_assert(sizeof(Stage9TileEntry)==4U,"Stage9 tile entry ABI");
_Static_assert(ST9_TILES_BASE+ST9_TILES_BLOB_BYTES<0x00A00000U,"Tile library DDR bound");
/* Absolute DDR reads; this header allocates no metadata/pixel array in SRAM. */
static inline const volatile Stage9TileAsset *stage9_tile_asset(unsigned id)
{{ return (const volatile Stage9TileAsset *)(uintptr_t)(ST9_TILES_BASE+ST9_TILES_META_OFFSET+id*16U); }}
static inline const volatile Stage9TileEntry *stage9_tile_entry(unsigned index)
{{ return (const volatile Stage9TileEntry *)(uintptr_t)(ST9_TILES_BASE+ST9_TILES_ENTRY_OFFSET+index*4U); }}
static inline const volatile uint16_t *stage9_tile_pixels(unsigned id)
{{ return (const volatile uint16_t *)(uintptr_t)(ST9_TILES_BASE+ST9_TILES_PIXEL_OFFSET+id*ST9_TILES_TILE_BYTES); }}
#endif
'''
    report = dict(schema=1, status='PASS', method='lossless source-grid 16px slices; full byte-content deduplication',
                  source_atlas='assets/atlas.bin', source_atlas_bytes=len(original),
                  source_atlas_sha256=sha(original), source_metadata_sha256=sha(source),
                  ddr_base=BASE, ddr_end_exclusive=BASE + len(blob), reserved_limit=LIMIT,
                  meta_offset=meta_offset, metadata_bytes=META.size * len(records),
                  entry_offset=entry_offset, entry_bytes=ENTRY.size * len(entries),
                  pixel_offset=pixel_offset, pixel_bytes=len(unique) * TILE_BYTES,
                  asset_count=len(records), enemy_asset_count=len(rows),
                  entry_count=len(entries), unique_tile_count=len(unique),
                  per_enemy_tile_histogram=dict(sorted(collections.Counter(row['count'] for row in rows).items())),
                  blob_bytes=len(blob), blob_crc32=f'{crc:08X}', blob_sha256=sha(blob),
                  header_sha256=sha(header.encode('utf-8')),
                  verification=dict(assets_reconstructed=len(rows), non_enemy_zero_count=ASSETS-len(rows),
                                    compared_pixels=compared_pixels, padding_pixels=padding_pixels,
                                    pixel_mismatches=mismatches, original_atlas_modified=False),
                  assets=rows)
    return blob, header, report


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--check', action='store_true')
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    blob, header, report = pack(root)
    products = {'assets/stage9_tiles.bin': blob,
                'src/stage9_tiles.h': header.encode('utf-8'),
                'build/host/stage9_tiles_report.json': (json.dumps(report, indent=2) + '\n').encode('utf-8')}
    for name, data in products.items():
        path = root / name
        if args.check:
            assert path.read_bytes() == data, ('generated output differs', name)
        else:
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data)
    print(json.dumps({key: value for key, value in report.items() if key != 'assets'}, indent=2))


if __name__ == '__main__':
    main()
