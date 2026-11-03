#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Append cached feedback shaders to the exact accepted Void RGB565 atlas.

This generator does not edit imported PNGs or load a board. Existing IDs,
offsets, pixels and anchors remain byte-for-byte identical. White silhouettes
and geometric rings are deterministic source-code-generated renderer caches.
Run with --check to verify all generated runtime files without writing them.
"""
from __future__ import annotations

import argparse
import copy
import hashlib
import json
from pathlib import Path
import re
import struct
import zlib

SOURCE_SHA256 = "f6b156d28750dfe8489786b8524d68e1240509a22c4836cef117ba777cc4bc3d"
SOURCE_BYTES = 186524
SOURCE_CRC32 = 0xD1D80764
KEY = 0xF81F
ADDRESS = 0x005C4000
BUDGET = 0x3C000 - 8192
BASE_COUNT = 266
FONT_START = 92
FONT_COUNT = 172
FLASH = ((266, "FLASH_DARTER1", 1), (267, "FLASH_STOPPER1", 2), (268, "FLASH_BOSS1", 3))
RINGS = ((269, "FX_RING32", 32), (270, "FX_RING48", 48))
RING_COLORS = (0xCFFF, 0x07FF, 0x0353)
ROW_RE = re.compile(r"\{(\d+)u,\s*(\d+)u,\s*(\d+)u,\s*(\d+)u,\s*(-?\d+),\s*(-?\d+)\}")


def sha(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def json_bytes(value: object) -> bytes:
    return (json.dumps(value, indent=2, ensure_ascii=False) + "\n").encode("utf-8")


def parse_rows(text: str) -> list[tuple[int, ...]]:
    return [tuple(map(int, match.groups())) for match in ROW_RE.finditer(text)]


def ring_pixels(size: int) -> bytes:
    """Two-pixel-wide circle sampled at doubled integer pixel centers.

    Center=(size-1)/2; radii outer=size/2-2, inner=size/2-4.
    The inner half has a thin pale-cyan core, followed by bright cyan and a
    darker outer band. All other pixels use the exact color-key sentinel.
    Global GPU Alpha supplies the animation's fade; no per-pixel alpha buffer
    or frame-time square root is required.
    """
    outer_sq = (size - 4) ** 2
    middle_sq = (size - 6) ** 2
    core_sq = (size - 7) ** 2
    inner_sq = (size - 8) ** 2
    pixels = []
    for y in range(size):
        dy = 2 * y + 1 - size
        for x in range(size):
            dx = 2 * x + 1 - size
            distance_sq = dx * dx + dy * dy
            if not inner_sq <= distance_sq <= outer_sq:
                color = KEY
            elif distance_sq >= middle_sq:
                color = RING_COLORS[2]
            elif distance_sq >= core_sq:
                color = RING_COLORS[1]
            else:
                color = RING_COLORS[0]
            pixels.append(color)
    return struct.pack("<" + "H" * len(pixels), *pixels)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true", help="verify generated files; do not write")
    options = parser.parse_args()
    variant = Path(__file__).resolve().parents[1]
    reference = variant.parent / "void_art_20261013"
    source_atlas = reference / "assets/atlas.bin"
    source_c = reference / "src/garden_assets.c"
    source_h = reference / "src/garden_assets.h"
    source_manifest = reference / "assets/manifest.json"
    source_notice = reference / "assets/NOTICE.txt"
    original = source_atlas.read_bytes()
    original_c = source_c.read_text(encoding="utf-8-sig")
    original_h = source_h.read_text(encoding="utf-8-sig")
    source = json.loads(source_manifest.read_text(encoding="utf-8-sig"))
    rows = parse_rows(original_c)
    assert sha(original) == SOURCE_SHA256, "Void reference atlas changed"
    assert len(original) == SOURCE_BYTES and zlib.crc32(original) == SOURCE_CRC32
    assert len(rows) == len(source["sprites"]) == BASE_COUNT
    assert source["sha256"] == SOURCE_SHA256
    for index, row in enumerate(rows):
        entry = source["sprites"][index]
        assert entry["id"] == index
        assert row == tuple(entry[key] for key in ("offset", "width", "height", "stride", "anchor_x", "anchor_y"))
        offset, width, height, stride, ax, ay = row
        assert offset % 2 == 0 and stride == width * 2 and width > 0 and height > 0
        assert 0 <= ax < width and 0 <= ay < height
        assert offset + height * stride <= len(original)

    atlas = bytearray(original)
    sprites = copy.deepcopy(source["sprites"])
    appended_rows = []

    def append_sprite(index: int, name: str, width: int, height: int,
                      ax: int, ay: int, data: bytes, provenance: dict) -> None:
        assert index == len(sprites)
        while len(atlas) % 4:
            atlas.append(0)
        offset = len(atlas)
        assert len(data) == width * height * 2
        atlas.extend(data)
        row = (offset, width, height, width * 2, ax, ay)
        appended_rows.append(f"    {{{offset}u, {width}u, {height}u, {width*2}u, {ax}, {ay}}}, /* {index}: {name} */")
        rows.append(row)
        sprites.append(dict(id=index, name=name, offset=offset, width=width, height=height,
                            stride=width * 2, anchor_x=ax, anchor_y=ay, bytes=len(data),
                            source=provenance, opaque_key_collisions_adjusted=0))

    for index, name, source_index in FLASH:
        offset, width, height, stride, ax, ay = rows[source_index]
        input_pixels = struct.unpack("<" + "H" * (width * height), original[offset:offset + height * stride])
        output_pixels = tuple(KEY if pixel == KEY else 0xFFFF for pixel in input_pixels)
        data = struct.pack("<" + "H" * len(output_pixels), *output_pixels)
        provenance = dict(kind="cached_RGB565_white_silhouette", author="Team C runtime-cache generator",
            original_author=sprites[source_index]["source"]["author"], license="CC0-1.0",
            reference_variant="void_art_20261013", reference_asset_id=source_index,
            reference_asset_name=sprites[source_index]["name"], reference_atlas_sha256=SOURCE_SHA256,
            algorithm="source==0xF81F -> 0xF81F; every other RGB565 source pixel -> 0xFFFF",
            source_png_unmodified=True, source_geometry_and_key_mask_preserved=True)
        append_sprite(index, name, width, height, ax, ay, data, provenance)

    for index, name, size in RINGS:
        provenance = dict(kind="cached_procedural_ring", author="Team C integer-geometry generator",
            license="GPL-3.0-or-later", external_image_source=None,
            algorithm="d2=(2*x+1-size)^2+(2*y+1-size)^2; transparent outside [(size-8)^2,(size-4)^2]; pale core / cyan / dark outer band",
            palette=[f"0x{color:04X}" for color in RING_COLORS],
            animation="GPU global Alpha fade; select 32/48 precomputed assets, no runtime scaling",
            original_pngs_modified=False)
        append_sprite(index, name, size, size, size // 2, size // 2, ring_pixels(size), provenance)

    final = bytes(atlas)
    digest = sha(final)
    crc = zlib.crc32(final)
    assert len(final) <= BUDGET and len(rows) == 271
    assert final[:len(original)] == original
    for index in range(BASE_COUNT):
        offset, width, height, stride, ax, ay = rows[index]
        assert sprites[index] == source["sprites"][index]
        assert final[offset:offset + height * stride] == original[offset:offset + height * stride]
    for index, _, source_index in FLASH:
        offset, width, height, stride, ax, ay = rows[index]
        original_offset, _, _, _, _, _ = rows[source_index]
        pixels = struct.unpack("<" + "H" * (width * height), final[offset:offset + height * stride])
        baseline = struct.unpack("<" + "H" * (width * height), original[original_offset:original_offset + height * stride])
        assert all(a == (KEY if b == KEY else 0xFFFF) for a, b in zip(pixels, baseline))
    for index, _, size in RINGS:
        offset, width, height, stride, ax, ay = rows[index]
        values = struct.unpack("<" + "H" * (size * size), final[offset:offset + size * stride])
        assert set(values) == {KEY, *RING_COLORS}
        assert values[(size // 2) * size + size // 2] == KEY
        assert all(values[x] == values[(size - 1) * size + x] == KEY for x in range(size))
        assert all(values[y * size] == values[y * size + size - 1] == KEY for y in range(size))
        assert all(values[y * size + x] == values[y * size + size - 1 - x] ==
                   values[(size - 1 - y) * size + x] for y in range(size) for x in range(size))

    header = original_h.replace("Run tools/build_atlas.py", "Run tools/build_fx_atlas.py")
    header, count = re.subn(r"#define GARDEN_ATLAS_BYTES \d+u", f"#define GARDEN_ATLAS_BYTES {len(final)}u", header)
    assert count == 1
    header, count = re.subn(r"#define GARDEN_ATLAS_CRC32 0x[0-9A-Fa-f]+u", f"#define GARDEN_ATLAS_CRC32 0x{crc:08X}u", header)
    assert count == 1
    marker = "    GARDEN_ASSET_COUNT\n};"
    assert header.count(marker) == 1
    definitions = "\n".join(f"    GARDEN_ASSET_{name} = {index}," for index, name, _ in FLASH + RINGS)
    header = header.replace(marker, definitions + "\n    GARDEN_ASSET_COUNT = 271\n};")
    metadata = original_c.replace("Generated by tools/build_atlas.py", "Generated by tools/build_fx_atlas.py")
    metadata, count = re.subn(r'(const char garden_atlas_sha256\[65\] = ")[0-9a-f]+(";)',
                             lambda match: match[1] + digest + match[2], metadata)
    assert count == 1
    match = re.search(r"(static const garden_sprite_t sprites\[GARDEN_ASSET_COUNT\] = \{\n)(.*?)(\n\};)", metadata, re.S)
    assert match
    metadata = metadata[:match.start(2)] + match[2] + "\n" + "\n".join(appended_rows) + metadata[match.end(2):]
    assert parse_rows(metadata) == rows

    manifest = copy.deepcopy(source)
    manifest.update(variant="neon_fx_20261013", bytes=len(final), sha256=digest,
                    crc32=f"0x{crc:08X}", budget_bytes=BUDGET, sprites=sprites,
                    reference_variant="void_art_20261013", reference_atlas_sha256=SOURCE_SHA256,
                    reference_bytes=SOURCE_BYTES, unchanged_prefix_bytes=SOURCE_BYTES,
                    appended_bytes=len(final) - SOURCE_BYTES, remaining_budget_bytes=BUDGET - len(final),
                    generator="tools/build_fx_atlas.py")
    validation = dict(result="PASS", source_atlas_sha256=SOURCE_SHA256,
        source_atlas_crc32=f"0x{SOURCE_CRC32:08X}", reference_sprite_count=BASE_COUNT,
        reference_prefix_bytes_unchanged=True, reference_geometry_unchanged=True,
        reference_sprite_bytes_verified=BASE_COUNT, unchanged_font_slots=FONT_COUNT,
        font_ids=[FONT_START, FONT_START + FONT_COUNT - 1], white_key_masks_exact=True,
        ring_border_and_center_transparent=True, ring_symmetric=True, opaque_ring_palette_avoids_key=True,
        atlas_sha256=digest, atlas_crc32=f"0x{crc:08X}", atlas_bytes=len(final),
        atlas_start=f"0x{ADDRESS:08X}", atlas_end_exclusive=f"0x{ADDRESS + len(final):08X}",
        reserved_scratch_start=f"0x{ADDRESS + BUDGET:08X}", free_bytes_before_scratch=BUDGET - len(final),
        max_asset_id=len(rows) - 1, asset_count=len(rows), original_pngs_modified=False,
        hardware_access=False)
    notice = source_notice.read_text(encoding="utf-8-sig") + (
        "\nNeon FX appended runtime caches (2026-10-13)\n"
        "IDs 0..265 retain the exact Void runtime pixels and metadata.\n"
        "FLASH_DARTER1 / FLASH_STOPPER1 / FLASH_BOSS1: derived from the\n"
        "Foozle CC0 runtime sprites by deterministic RGB565 silhouette shading;\n"
        "the transparency key and original PNGs are unchanged.\n"
        "FX_RING32 / FX_RING48: Team C original integer-circle generator,\n"
        "GPL-3.0-or-later; pale core/cyan/dark outer band, cached for Color Key\n"
        "and GPU global Alpha. No downloaded image was edited for these rings.\n")
    outputs = {
        "assets/atlas.bin": final,
        "assets/atlas.sha256": (digest + "  atlas.bin\n").encode("ascii"),
        "assets/manifest.json": json_bytes(manifest),
        "assets/fx_validation.json": json_bytes(validation),
        "assets/NOTICE.txt": notice.encode("utf-8"),
        "src/garden_assets.h": header.encode("utf-8"),
        "src/garden_assets.c": metadata.encode("utf-8"),
    }
    checksums = {name: {"bytes": len(data), "sha256": sha(data)} for name, data in outputs.items()}
    script = Path(__file__).resolve()
    checksums["tools/build_fx_atlas.py"] = {"bytes": len(script.read_bytes()), "sha256": sha(script.read_bytes())}
    outputs["assets/runtime_checksums.json"] = json_bytes(dict(format="sha256", atlas_crc32=f"0x{crc:08X}", files=checksums))
    for relative, data in outputs.items():
        path = variant / relative
        if options.check:
            assert path.read_bytes() == data, "Generated output differs: " + relative
        else:
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data)
    print(json.dumps(dict(result="CHECK PASS" if options.check else "GENERATE PASS",
        asset_count=len(rows), bytes=len(final), appended_bytes=len(final) - SOURCE_BYTES,
        crc32=f"0x{crc:08X}", sha256=digest, remaining_budget_bytes=BUDGET - len(final)), indent=2))


if __name__ == "__main__":
    main()
