#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Append focus and impact geometry to the frozen Stage RGB565 atlas.

Existing 271 IDs, offsets, pixels, masks, anchors and font slots are preserved.
No imported PNG is edited. The three new sprites are deterministic code-native
geometry for Color Key and hardware global Alpha. --check verifies every
generated output, including the source metadata and preview, without rewriting.
Python 3 + Pillow is required only for the labelled host asset preview.
"""
from __future__ import annotations

import argparse
import copy
import hashlib
import io
import json
from pathlib import Path
import re
import struct
import zlib

from PIL import Image, ImageDraw, ImageFont

SOURCE_SHA256 = "1ac25c1889b687c314f95b6233ec3c8bd8c477447ac91f211714a555059785e3"
SOURCE_BYTES = 226816
SOURCE_CRC32 = 0xF9808398
SOURCE_VARIANT = "neon_stage_20261013"
BASE_COUNT = 271
FONT_START = 92
FONT_COUNT = 172
KEY = 0xF81F
ADDRESS = 0x005C4000
BUDGET = 0x3C000 - 8192
WHITE = 0xFFFF
CYAN = 0x07FF
PALE_CYAN = 0xCFFF
GOLD = 0xFFE0
PALE_GOLD = 0xFFB2
APPENDED = ((271, "FOCUS_VULNERABLE", 19), (272, "FOCUS_GRACE", 19),
            (273, "HIT_SPARK", 13))
ROW_RE = re.compile(r"\{(\d+)u,\s*(\d+)u,\s*(\d+)u,\s*(\d+)u,\s*(-?\d+),\s*(-?\d+)\}")


def sha(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def json_bytes(value: object) -> bytes:
    return (json.dumps(value, indent=2, ensure_ascii=False) + "\n").encode("utf-8")


def parse_rows(text: str) -> list[tuple[int, ...]]:
    return [tuple(map(int, match.groups())) for match in ROW_RE.finditer(text)]


def focus_pixels(grace: bool) -> bytes:
    """19x19 boundary means inclusive +/-9px from the true player center.

    Only short corner arms and a 3x3 center marker are drawn; the interior is
    transparent. Vulnerable uses a solid white center with connected five-pixel
    white/pale-cyan arms. Grace uses a hollow cyan center and broken cyan arms.
    The entire square is a bullet-center threshold, not a one-pixel hitbox.
    """
    pixels = []
    for y in range(19):
        for x in range(19):
            xx, yy = min(x, 18 - x), min(y, 18 - y)
            color = KEY
            if xx == 0 and yy <= 4 or yy == 0 and xx <= 4:
                if grace:
                    color = CYAN if max(xx, yy) != 2 else KEY
                else:
                    color = WHITE if max(xx, yy) <= 2 else PALE_CYAN
            elif not grace and (xx == 1 and yy <= 3 or yy == 1 and xx <= 3):
                color = CYAN
            if abs(x - 9) <= 1 and abs(y - 9) <= 1:
                color = (KEY if x == y == 9 else CYAN) if grace else WHITE
            pixels.append(color)
    return struct.pack("<" + "H" * len(pixels), *pixels)


def spark_pixels() -> bytes:
    """Bright white core and gold pointed cross with short diagonal streaks."""
    pixels = []
    for y in range(13):
        for x in range(13):
            ax, ay = abs(x - 6), abs(y - 6)
            color = KEY
            if ax <= 1 and ay <= 1:
                color = WHITE
            elif (ax == 0 and ay <= 6) or (ay == 0 and ax <= 6):
                color = WHITE if max(ax, ay) <= 3 else GOLD
            elif min(ax, ay) == 1 and max(ax, ay) <= 3:
                color = PALE_GOLD
            elif ax == ay and 2 <= ax <= 4:
                color = WHITE if ax == 2 else GOLD
            pixels.append(color)
    return struct.pack("<" + "H" * len(pixels), *pixels)


def verify_new_pixels(data: bytes, size: int, name: str) -> None:
    values = struct.unpack("<" + "H" * (size * size), data)
    assert all(values[y * size + x] == values[y * size + size - 1 - x] ==
               values[(size - 1 - y) * size + x] for y in range(size) for x in range(size))
    center = size // 2
    if name.startswith("FOCUS"):
        grace = name == "FOCUS_GRACE"
        assert values[center * size + center] == (KEY if grace else WHITE)
        assert all(values[y * size + x] == KEY for y in range(2, 17) for x in range(2, 17)
                   if not (abs(x - 9) <= 1 and abs(y - 9) <= 1))
        assert values[0] != KEY and values[18] != KEY and values[18 * size] != KEY
        assert values[9] == KEY and values[9 * size] == KEY
        assert values[0] == (CYAN if grace else WHITE)
        if grace:
            assert values[2] == KEY and values[2 * size] == KEY
    else:
        assert values[center * size + center] == WHITE
        assert values[0] == values[size - 1] == values[(size - 1) * size] == KEY
        assert set(values) == {KEY, WHITE, GOLD, PALE_GOLD}


def preview_bytes(entries: list[tuple[str, int, bytes]]) -> bytes:
    """Illustration of code geometry only; no imported image is modified."""
    image = Image.new("RGB", (720, 252), (5, 9, 19))
    draw = ImageDraw.Draw(image)
    font = ImageFont.load_default()
    draw.text((14, 12), "HOST ASSET PREVIEW - NOT A BOARD CAPTURE", fill=(205, 232, 255), font=font)
    draw.text((14, 32), "Focus boundary: inclusive +/-9 px bullet-center test; open interior.",
              fill=(135, 191, 210), font=font)
    scale = 6
    for index, (name, size, data) in enumerate(entries):
        left = 30 + index * 230
        top = 70
        decoded = struct.unpack("<" + "H" * (size * size), data)
        for y in range(size):
            for x in range(size):
                value = decoded[y * size + x]
                checker = (20, 31, 44) if (x // 2 + y // 2) % 2 else (12, 21, 32)
                rgb = checker if value == KEY else (((value >> 11) & 31) * 255 // 31,
                    ((value >> 5) & 63) * 255 // 63, (value & 31) * 255 // 31)
                draw.rectangle((left + x * scale, top + y * scale,
                    left + (x + 1) * scale - 1, top + (y + 1) * scale - 1), fill=rgb)
        draw.text((left, 196), name, fill=(238, 248, 255), font=font)
        draw.text((left, 216), f"{size}x{size}; anchor {size//2},{size//2}; x6 preview",
                  fill=(138, 183, 203), font=font)
    stream = io.BytesIO()
    image.save(stream, format="PNG", optimize=False)
    return stream.getvalue()


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true", help="verify generated files; do not write")
    options = parser.parse_args()
    variant = Path(__file__).resolve().parents[1]
    reference = variant.parent / SOURCE_VARIANT
    original = (reference / "assets/atlas.bin").read_bytes()
    original_c = (reference / "src/garden_assets.c").read_text(encoding="utf-8-sig")
    original_h = (reference / "src/garden_assets.h").read_text(encoding="utf-8-sig")
    source = json.loads((reference / "assets/manifest.json").read_text(encoding="utf-8-sig"))
    rows = parse_rows(original_c)
    assert sha(original) == SOURCE_SHA256, "Frozen Stage reference atlas changed"
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
    previews = []
    for index, name, size in APPENDED:
        assert index == len(sprites)
        while len(atlas) % 4:
            atlas.append(0)
        offset = len(atlas)
        data = spark_pixels() if name == "HIT_SPARK" else focus_pixels(name == "FOCUS_GRACE")
        verify_new_pixels(data, size, name)
        assert len(data) == size * size * 2
        atlas.extend(data)
        anchor = size // 2
        row = (offset, size, size, size * 2, anchor, anchor)
        rows.append(row)
        appended_rows.append(f"    {{{offset}u, {size}u, {size}u, {size*2}u, {anchor}, {anchor}}}, /* {index}: {name} */")
        provenance = dict(kind="cached_procedural_feedback_geometry", author="Team C integer-geometry generator",
            license="GPL-3.0-or-later", external_image_source=None, original_pngs_modified=False,
            algorithm="focus_pixels(grace)" if name.startswith("FOCUS") else "spark_pixels()",
            generator="tools/build_feedback_atlas.py", color_key=f"0x{KEY:04X}",
            animation="opaque focus marker; hit spark uses GPU global Alpha fade")
        if name.startswith("FOCUS"):
            provenance.update(boundary_px_inclusive=[-9, 9], width_explains="bullet-center square threshold, not a 1px hitbox",
                core_test="abs(dx)<=9000 && abs(dy)<=9000 fixed-point units",
                center="hollow cyan 3x3; invulnerability grace" if name == "FOCUS_GRACE" else "solid white 3x3; vulnerable")
        sprites.append(dict(id=index, name=name, offset=offset, width=size, height=size,
            stride=size * 2, anchor_x=anchor, anchor_y=anchor, bytes=len(data), source=provenance,
            opaque_key_collisions_adjusted=0))
        previews.append((name, size, data))

    final = bytes(atlas)
    digest, crc = sha(final), zlib.crc32(final)
    assert len(final) <= BUDGET and len(rows) == 274
    assert final[:SOURCE_BYTES] == original
    for index in range(BASE_COUNT):
        assert sprites[index] == source["sprites"][index]
        offset, width, height, stride, _, _ = rows[index]
        assert final[offset:offset + height * stride] == original[offset:offset + height * stride]
    header = original_h.replace("Run tools/build_fx_atlas.py", "Run tools/build_feedback_atlas.py")
    header, count = re.subn(r"#define GARDEN_ATLAS_BYTES \d+u", f"#define GARDEN_ATLAS_BYTES {len(final)}u", header)
    assert count == 1
    header, count = re.subn(r"#define GARDEN_ATLAS_CRC32 0x[0-9A-Fa-f]+u", f"#define GARDEN_ATLAS_CRC32 0x{crc:08X}u", header)
    assert count == 1
    marker = "    GARDEN_ASSET_COUNT = 271\n};"
    assert header.count(marker) == 1
    definitions = "\n".join(f"    GARDEN_ASSET_{name} = {index}," for index, name, _ in APPENDED)
    header = header.replace(marker, definitions + "\n    GARDEN_ASSET_COUNT = 274\n};")
    metadata = original_c.replace("Generated by tools/build_fx_atlas.py", "Generated by tools/build_feedback_atlas.py")
    metadata, count = re.subn(r'(const char garden_atlas_sha256\[65\] = ")[0-9a-f]+(";)',
                             lambda match: match[1] + digest + match[2], metadata)
    assert count == 1
    match = re.search(r"(static const garden_sprite_t sprites\[GARDEN_ASSET_COUNT\] = \{\n)(.*?)(\n\};)", metadata, re.S)
    assert match
    metadata = metadata[:match.start(2)] + match[2] + "\n" + "\n".join(appended_rows) + metadata[match.end(2):]
    assert parse_rows(metadata) == rows
    manifest = copy.deepcopy(source)
    manifest.update(variant="neon_feedback_20261014", bytes=len(final), sha256=digest,
        crc32=f"0x{crc:08X}", budget_bytes=BUDGET, sprites=sprites, reference_variant=SOURCE_VARIANT,
        reference_atlas_sha256=SOURCE_SHA256, reference_bytes=SOURCE_BYTES, unchanged_prefix_bytes=SOURCE_BYTES,
        appended_bytes=len(final) - SOURCE_BYTES, remaining_budget_bytes=BUDGET - len(final),
        generator="tools/build_feedback_atlas.py")
    validation = dict(result="PASS", reference_variant=SOURCE_VARIANT, source_atlas_sha256=SOURCE_SHA256,
        source_atlas_crc32=f"0x{SOURCE_CRC32:08X}", reference_sprite_count=BASE_COUNT,
        reference_prefix_bytes_unchanged=True, reference_geometry_unchanged=True,
        reference_sprite_bytes_verified=BASE_COUNT, unchanged_font_slots=FONT_COUNT,
        font_ids=[FONT_START, FONT_START + FONT_COUNT - 1], focus_boundary_px_inclusive=[-9, 9],
        focus_center_solid_white_3x3=True, grace_center_hollow_cyan_3x3=True,
        focus_interior_transparent=True, all_new_geometry_symmetric=True, impact_white_gold_palette=True,
        atlas_sha256=digest, atlas_crc32=f"0x{crc:08X}", atlas_bytes=len(final),
        appended_bytes=len(final) - SOURCE_BYTES, atlas_start=f"0x{ADDRESS:08X}",
        atlas_end_exclusive=f"0x{ADDRESS + len(final):08X}", reserved_scratch_start=f"0x{ADDRESS + BUDGET:08X}",
        free_bytes_before_scratch=BUDGET - len(final), max_asset_id=len(rows) - 1, asset_count=len(rows),
        original_pngs_modified=False, hardware_access=False)
    notice = (reference / "assets/NOTICE.txt").read_text(encoding="utf-8-sig") + (
        "\nNeon Feedback appended runtime geometry (2026-10-14)\n"
        "IDs 0..270 retain the exact Stage/Neon FX runtime pixels and metadata.\n"
        "FOCUS_VULNERABLE / FOCUS_GRACE: Team C original integer corner/center\n"
        "geometry. A 19x19 boundary marks the inclusive +/-9px bullet-center\n"
        "collision threshold; the interior is transparent. Grace has a hollow\n"
        "cyan center and broken cyan corners; vulnerable has a solid white center.\n"
        "HIT_SPARK: Team C original integer cross/diagonal geometry, white/gold,\n"
        "cached for Color Key and GPU global Alpha. GPL-3.0-or-later.\n"
        "No imported PNG was edited. feedback_preview.png is a labelled host\n"
        "geometry illustration, not a board capture or a collision guarantee.\n")
    outputs = {"assets/atlas.bin": final,
        "assets/atlas.sha256": (digest + "  atlas.bin\n").encode("ascii"),
        "assets/manifest.json": json_bytes(manifest), "assets/feedback_validation.json": json_bytes(validation),
        "assets/NOTICE.txt": notice.encode("utf-8"), "assets/feedback_preview.png": preview_bytes(previews),
        "src/garden_assets.h": header.encode("utf-8"), "src/garden_assets.c": metadata.encode("utf-8")}
    checksums = {name: {"bytes": len(data), "sha256": sha(data)} for name, data in outputs.items()}
    script = Path(__file__).resolve().read_bytes()
    checksums["tools/build_feedback_atlas.py"] = {"bytes": len(script), "sha256": sha(script)}
    outputs["assets/runtime_checksums.json"] = json_bytes(dict(format="sha256", atlas_crc32=f"0x{crc:08X}", files=checksums))
    for relative, data in outputs.items():
        path = variant / relative
        if options.check:
            assert path.read_bytes() == data, "Generated output differs: " + relative
        else:
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data)
    print(json.dumps(dict(result="CHECK PASS" if options.check else "GENERATE PASS", asset_count=len(rows),
        bytes=len(final), appended_bytes=len(final) - SOURCE_BYTES, crc32=f"0x{crc:08X}", sha256=digest,
        remaining_budget_bytes=BUDGET - len(final)), indent=2))


if __name__ == "__main__":
    main()
