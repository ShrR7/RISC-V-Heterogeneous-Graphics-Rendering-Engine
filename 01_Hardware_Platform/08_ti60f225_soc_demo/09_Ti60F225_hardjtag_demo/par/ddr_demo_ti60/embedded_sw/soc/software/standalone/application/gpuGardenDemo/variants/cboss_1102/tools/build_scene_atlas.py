#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Append six deterministic mechanical/Botanical scene assets to Feedback.

The frozen 274-slot RGB565 prefix, fonts and imported PNGs are unchanged.
Only code-native shapes are drawn. --check verifies bytes without rewriting.
All six are decorative; their anchors do not change any collision boundary.
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

SOURCE_VARIANT = "neon_feedback_20261014"
SOURCE_SHA256 = "46d3220fafb8ec779a34cfdad22a26ef3a8e980fa9b7e23e3f0df216e72b50e5"
SOURCE_BYTES = 228602
SOURCE_CRC32 = 0x61A21C69
BASE_COUNT = 274
FONT_START = 92
FONT_COUNT = 172
KEY = 0xF81F
ADDRESS = 0x005C4000
BUDGET = 0x3C000 - 8192
APPENDED = (
    (274, "BOSS_WING_L", 23, 61, 11, 30),
    (275, "BOSS_WING_R", 23, 61, 11, 30),
    (276, "BOSS_CHARGE", 17, 17, 8, 8),
    (277, "GARDEN_FAR", 15, 23, 7, 11),
    (278, "GARDEN_STREAK", 7, 15, 3, 7),
    (279, "BOSS_CORE", 25, 25, 12, 12),
)
ROW_RE = re.compile(r"\{(\d+)u,\s*(\d+)u,\s*(\d+)u,\s*(\d+)u,\s*(-?\d+),\s*(-?\d+)\}")


def sha(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def json_bytes(value: object) -> bytes:
    return (json.dumps(value, indent=2, ensure_ascii=False) + "\n").encode("utf-8")


def rgb565(red: int, green: int, blue: int) -> int:
    color = (red >> 3) << 11 | (green >> 2) << 5 | (blue >> 3)
    assert color != KEY, "Opaque scene palette collides with transparency key"
    return color


def rgb888(value: int) -> tuple[int, int, int]:
    return ((value >> 11 & 31) * 255 // 31,
            (value >> 5 & 63) * 255 // 63, (value & 31) * 255 // 31)


def parse_rows(text: str) -> list[tuple[int, ...]]:
    return [tuple(map(int, match.groups())) for match in ROW_RE.finditer(text)]


INK = rgb565(8, 18, 30)
EDGE = rgb565(38, 72, 96)
SHADOW = rgb565(18, 32, 52)
GUNMETAL = rgb565(54, 77, 94)
ARMOR = rgb565(80, 113, 137)
LIT_ARMOR = rgb565(113, 157, 184)
PLATINUM = rgb565(198, 226, 238)
CYAN = rgb565(78, 225, 245)
ICE = rgb565(183, 247, 255)
WHITE = 0xFFFF
TEAL_DARK = rgb565(16, 32, 48)
TEAL_DIM = rgb565(24, 64, 80)
TEAL_EDGE = rgb565(40, 104, 112)
STREAK_EDGE = rgb565(16, 40, 64)
STREAK_CORE = rgb565(32, 88, 120)


def packed(image: Image.Image) -> bytes:
    pixels = image.load()
    values = [pixels[x, y] for y in range(image.height) for x in range(image.width)]
    assert all(0 <= value <= 0xFFFF for value in values)
    return struct.pack("<" + "H" * len(values), *values)


def wing_pixels(right: bool) -> bytes:
    """Cool articulated satellite pod: armour, hinge, turbine and paired cannon."""
    image = Image.new("I", (23, 61), KEY)
    draw = ImageDraw.Draw(image)
    outline = [(15, 0), (21, 6), (21, 20), (17, 26), (22, 28),
               (22, 33), (18, 35), (17, 50), (12, 60), (5, 60),
               (1, 52), (1, 16), (5, 5)]
    draw.polygon(outline, fill=INK)
    draw.line(outline + [outline[0]], fill=EDGE, width=1)
    # Separate armoured nose and intake rather than one undifferentiated plate.
    draw.polygon([(14, 3), (19, 7), (19, 18), (14, 23), (4, 23),
                  (4, 15), (7, 7)], fill=GUNMETAL)
    draw.polygon([(14, 4), (18, 8), (18, 13), (6, 17), (8, 8)], fill=ARMOR)
    draw.line([(8, 7), (14, 3), (19, 7)], fill=LIT_ARMOR)
    draw.line([(5, 14), (5, 21), (12, 21)], fill=PLATINUM)
    draw.line([(9, 10), (15, 8), (17, 10)], fill=CYAN)
    for y in (16, 18, 20):
        draw.line([(10, y), (17, y)], fill=INK)
        draw.point((17, y), fill=LIT_ARMOR)
    # The inboard coupling is an actual raised circular part with dark hub.
    draw.rectangle((12, 27, 22, 33), fill=GUNMETAL)
    draw.line([(13, 27), (22, 27)], fill=LIT_ARMOR)
    draw.line([(16, 31), (22, 31)], fill=CYAN)
    draw.ellipse((5, 24, 15, 34), fill=EDGE)
    draw.ellipse((6, 25, 14, 33), fill=ARMOR)
    draw.ellipse((8, 27, 12, 31), fill=INK)
    draw.point((10, 29), fill=ICE)
    # Lower twin-barrel module and thin emissive rails.
    draw.polygon([(3, 36), (14, 35), (16, 40), (13, 53), (5, 56),
                  (3, 51)], fill=SHADOW)
    draw.polygon([(4, 37), (12, 36), (14, 40), (11, 48), (4, 48)], fill=GUNMETAL)
    draw.line([(4, 37), (11, 36), (14, 40)], fill=LIT_ARMOR)
    draw.rectangle((5, 41, 7, 56), fill=ARMOR)
    draw.rectangle((10, 41, 12, 53), fill=GUNMETAL)
    draw.line([(6, 42), (6, 53)], fill=CYAN)
    draw.line([(11, 42), (11, 50)], fill=CYAN)
    draw.line([(4, 55), (8, 55)], fill=PLATINUM)
    draw.line([(9, 52), (13, 52)], fill=ICE)
    draw.line([(5, 58), (10, 58)], fill=EDGE)
    for x, y in ((4, 19), (16, 7), (3, 39), (14, 44), (18, 29)):
        draw.point((x, y), fill=PLATINUM)
    if right:
        image = image.transpose(Image.Transpose.FLIP_LEFT_RIGHT)
    return packed(image)


def charge_pixels() -> bytes:
    """Open-point charge bloom, usable in front of the core or at the muzzle."""
    image = Image.new("I", (17, 17), KEY)
    draw = ImageDraw.Draw(image)
    draw.line([(8, 0), (8, 16)], fill=CYAN)
    draw.line([(0, 8), (16, 8)], fill=CYAN)
    draw.line([(3, 3), (13, 13)], fill=EDGE)
    draw.line([(13, 3), (3, 13)], fill=EDGE)
    draw.polygon([(8, 2), (14, 8), (8, 14), (2, 8)], outline=CYAN)
    draw.polygon([(8, 5), (11, 8), (8, 11), (5, 8)], fill=ICE)
    draw.rectangle((7, 7, 9, 9), fill=WHITE)
    for x, y in ((8, 1), (15, 8), (8, 15), (1, 8), (4, 4),
                 (12, 4), (4, 12), (12, 12)):
        draw.point((x, y), fill=ICE)
    return packed(image)


def far_pixels() -> bytes:
    """A dim cyan botanical light stem with two leaves and an angular flower."""
    image = Image.new("I", (15, 23), KEY)
    draw = ImageDraw.Draw(image)
    draw.line([(7, 21), (7, 11), (6, 8)], fill=TEAL_DIM)
    draw.polygon([(7, 17), (2, 15), (1, 11), (5, 12)], fill=TEAL_DARK)
    draw.line([(1, 11), (5, 14), (7, 17)], fill=TEAL_DIM)
    draw.polygon([(7, 14), (12, 12), (13, 8), (10, 9)], fill=TEAL_DARK)
    draw.line([(13, 8), (10, 12), (7, 14)], fill=TEAL_DIM)
    draw.polygon([(7, 0), (10, 4), (14, 5), (10, 8), (7, 11),
                  (4, 8), (0, 5), (4, 4)], outline=TEAL_EDGE)
    draw.line([(7, 3), (7, 8)], fill=TEAL_DIM)
    draw.line([(4, 5), (10, 5)], fill=TEAL_DIM)
    draw.point((7, 5), fill=TEAL_EDGE)
    draw.line([(4, 22), (7, 20), (10, 22)], fill=TEAL_DARK)
    return packed(image)


def streak_pixels() -> bytes:
    """Dim short vertically tapered flow particle; no white shooting star."""
    image = Image.new("I", (7, 15), KEY)
    draw = ImageDraw.Draw(image)
    draw.line([(3, 0), (3, 14)], fill=STREAK_EDGE)
    draw.line([(2, 6), (2, 12)], fill=TEAL_DARK)
    draw.line([(4, 6), (4, 12)], fill=TEAL_DARK)
    draw.line([(3, 7), (3, 12)], fill=STREAK_CORE)
    draw.line([(1, 11), (5, 11)], fill=TEAL_DIM)
    draw.point((3, 11), fill=STREAK_CORE)
    return packed(image)


def core_pixels() -> bytes:
    """Gunmetal/platinum reactor with a cyan lens and four independently readable clamps."""
    image = Image.new("I", (25, 25), KEY)
    draw = ImageDraw.Draw(image)
    octagon = [(8, 1), (16, 1), (23, 8), (23, 16), (16, 23),
               (8, 23), (1, 16), (1, 8)]
    draw.polygon(octagon, fill=INK)
    draw.line(octagon + [octagon[0]], fill=EDGE)
    draw.polygon([(8, 3), (16, 3), (21, 8), (21, 16), (16, 21),
                  (8, 21), (3, 16), (3, 8)], fill=GUNMETAL)
    draw.ellipse((5, 5, 19, 19), fill=SHADOW, outline=LIT_ARMOR)
    draw.ellipse((7, 7, 17, 17), fill=INK, outline=CYAN)
    draw.polygon([(12, 8), (16, 12), (12, 16), (8, 12)], fill=CYAN)
    draw.polygon([(12, 10), (14, 12), (12, 14), (10, 12)], fill=ICE)
    draw.point((12, 12), fill=WHITE)
    for box in ((9, 2, 15, 4), (9, 20, 15, 22), (2, 9, 4, 15), (20, 9, 22, 15)):
        draw.rectangle(box, fill=ARMOR)
    draw.line([(10, 2), (14, 2)], fill=PLATINUM)
    draw.line([(10, 22), (14, 22)], fill=PLATINUM)
    draw.line([(2, 10), (2, 14)], fill=PLATINUM)
    draw.line([(22, 10), (22, 14)], fill=PLATINUM)
    for x, y in ((5, 5), (19, 5), (5, 19), (19, 19)):
        draw.point((x, y), fill=ICE)
    return packed(image)


GENERATORS = {"BOSS_WING_L": lambda: wing_pixels(False),
              "BOSS_WING_R": lambda: wing_pixels(True),
              "BOSS_CHARGE": charge_pixels, "GARDEN_FAR": far_pixels,
              "GARDEN_STREAK": streak_pixels, "BOSS_CORE": core_pixels}


def verify_pixels(data: bytes, width: int, height: int, name: str) -> dict:
    assert len(data) == width * height * 2
    values = struct.unpack("<" + "H" * (width * height), data)
    opaque = [(x, y) for y in range(height) for x in range(width)
              if values[y * width + x] != KEY]
    assert opaque and len(opaque) < width * height
    assert values[0] == values[width - 1] == values[(height - 1) * width] == values[-1] == KEY
    palette = set(values) - {KEY}
    max_channels = [max(rgb888(value)[channel] for value in palette) for channel in range(3)]
    if name.startswith("GARDEN_"):
        assert max_channels[0] <= 48 and max_channels[1] <= 112 and max_channels[2] <= 128
    if name in ("BOSS_CORE", "BOSS_CHARGE"):
        assert values[(height // 2) * width + width // 2] == WHITE
    return dict(width=width, height=height, bytes=len(data), opaque_pixels=len(opaque),
                key_pixels=len(values) - len(opaque), palette_size=len(palette),
                opaque_bounds=[min(x for x, _ in opaque), min(y for _, y in opaque),
                               max(x for x, _ in opaque), max(y for _, y in opaque)],
                opaque_max_rgb888=max_channels, opaque_key_collisions=0,
                sha256=sha(data), decorative_only=True)


def paste_rgb565(image: Image.Image, data: bytes, width: int, height: int,
                 left: int, top: int, scale: int = 1, checker: bool = False) -> None:
    draw = ImageDraw.Draw(image)
    values = struct.unpack("<" + "H" * (width * height), data)
    for y in range(height):
        for x in range(width):
            color = values[y * width + x]
            if color == KEY and not checker:
                continue
            rgb = ((13, 23, 35) if (x // 2 + y // 2) % 2 else (20, 31, 44)) if color == KEY else rgb888(color)
            draw.rectangle((left + x * scale, top + y * scale,
                            left + (x + 1) * scale - 1, top + (y + 1) * scale - 1), fill=rgb)


def png(image: Image.Image) -> bytes:
    stream = io.BytesIO()
    image.save(stream, format="PNG", optimize=False)
    return stream.getvalue()


def preview_bytes(entries: dict[str, bytes]) -> bytes:
    image = Image.new("RGB", (900, 408), (5, 9, 19))
    draw = ImageDraw.Draw(image)
    font = ImageFont.load_default()
    draw.text((15, 12), "HOST SCENE ASSET PREVIEW - NOT A BOARD CAPTURE", fill=(205, 232, 255), font=font)
    draw.text((15, 30), "Original 274 sprites unchanged; mechanical parts are decorative only.", fill=(135, 191, 210), font=font)
    positions = [(20, 64, 4), (160, 64, 4), (340, 70, 5), (480, 70, 5), (610, 70, 6), (730, 70, 5)]
    for (index, name, width, height, ax, ay), (left, top, scale) in zip(APPENDED, positions):
        paste_rgb565(image, entries[name], width, height, left, top, scale, True)
        draw.text((left, 320), f"{index}: {name}", fill=(230, 246, 255), font=font)
        draw.text((left, 338), f"{width}x{height}; x{scale}", fill=(135, 191, 210), font=font)
        draw.text((left, 355), f"anchor {ax},{ay}", fill=(135, 191, 210), font=font)
    draw.text((15, 385), "Botanical motifs/streaks use dim source pixels even when global Alpha is disabled.", fill=(135, 191, 210), font=font)
    return png(image)


def boss_preview_bytes(original: bytes, rows: list[tuple[int, ...]], entries: dict[str, bytes]) -> bytes:
    """One-to-one atlas decode and x3 composite, not a board or gameplay capture."""
    image = Image.new("RGB", (980, 530), (5, 9, 19))
    draw = ImageDraw.Draw(image)
    font = ImageFont.load_default()
    draw.text((15, 12), "HOST BOSS COMPOSITE ILLUSTRATION - NOT A BOARD CAPTURE", fill=(205, 232, 255), font=font)
    draw.text((15, 30), "Normal / flash body: unchanged original pixels. Pods at +/-61px; total width 145px.", fill=(135, 191, 210), font=font)
    for center, asset_id, label in ((248, 3, "NORMAL: COOL SATELLITE PODS + FRONT REACTOR"),
                                    (730, 268, "FLASH: RETAINED WHITE SILHOUETTE + PODS")):
        cx, cy, scale = center, 250, 3
        for name, wing_x in (("BOSS_WING_L", -61), ("BOSS_WING_R", 61)):
            paste_rgb565(image, entries[name], 23, 61, cx + (wing_x - 11) * scale, cy - 30 * scale, scale)
        offset, width, height, stride, ax, ay = rows[asset_id]
        paste_rgb565(image, original[offset:offset + stride * height], width, height,
                     cx - ax * scale, cy - ay * scale, scale)
        paste_rgb565(image, entries["BOSS_CORE"], 25, 25, cx - 12 * scale, cy - 12 * scale, scale)
        draw.text((cx - 219, 450), label, fill=(230, 246, 255), font=font)
    draw.text((15, 485), "Decoration does not enlarge collisions. Actual pod motion/charge placement is chosen by garden_scene.c.", fill=(135, 191, 210), font=font)
    draw.text((15, 505), "This illustration has no measured FPS, damage values, or claim of visible attack timing.", fill=(135, 191, 210), font=font)
    return png(image)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true", help="verify deterministic outputs; do not write")
    options = parser.parse_args()
    variant = Path(__file__).resolve().parents[1]
    reference = variant.parent / SOURCE_VARIANT
    original = (reference / "assets/atlas.bin").read_bytes()
    original_c = (reference / "src/garden_assets.c").read_text(encoding="utf-8-sig")
    original_h = (reference / "src/garden_assets.h").read_text(encoding="utf-8-sig")
    source = json.loads((reference / "assets/manifest.json").read_text(encoding="utf-8-sig"))
    rows = parse_rows(original_c)
    assert sha(original) == source["sha256"] == SOURCE_SHA256
    assert len(original) == SOURCE_BYTES and zlib.crc32(original) == SOURCE_CRC32
    assert len(rows) == len(source["sprites"]) == BASE_COUNT
    for index, row in enumerate(rows):
        entry = source["sprites"][index]
        assert entry["id"] == index
        assert row == tuple(entry[key] for key in ("offset", "width", "height", "stride", "anchor_x", "anchor_y"))
        offset, width, height, stride, ax, ay = row
        assert offset % 2 == 0 and stride == width * 2 and width > 0 and height > 0
        assert 0 <= ax < width and 0 <= ay < height and offset + height * stride <= SOURCE_BYTES
    source_pngs = {}
    for directory in ("assets/imported", "assets/source", "assets/sprites"):
        for path in sorted((reference / directory).rglob("*.png")):
            relative = path.relative_to(reference).as_posix()
            data = path.read_bytes()
            assert (variant / relative).read_bytes() == data, "Frozen source PNG differs: " + relative
            source_pngs[relative] = sha(data)

    atlas = bytearray(original)
    sprites = copy.deepcopy(source["sprites"])
    appended_rows = []
    entries, new_validation = {}, {}
    for index, name, width, height, ax, ay in APPENDED:
        assert index == len(sprites)
        while len(atlas) % 4:
            atlas.append(0)
        offset = len(atlas)
        data = GENERATORS[name]()
        new_validation[name] = verify_pixels(data, width, height, name)
        atlas.extend(data)
        row = (offset, width, height, width * 2, ax, ay)
        rows.append(row)
        appended_rows.append(f"    {{{offset}u, {width}u, {height}u, {width*2}u, {ax}, {ay}}}, /* {index}: {name} */")
        provenance = dict(kind="cached_procedural_scene_geometry", author="Team C integer-geometry generator",
                          license="GPL-3.0-or-later", external_image_source=None, original_pngs_modified=False,
                          generator="tools/build_scene_atlas.py", color_key=f"0x{KEY:04X}",
                          decorative_only=True, collision_or_damage_changes=False)
        if name.startswith("GARDEN_"):
            provenance.update(source_palette="dim blue/teal, remains low contrast with Alpha disabled",
                              algorithm="far_pixels()" if name == "GARDEN_FAR" else "streak_pixels()")
        elif name.startswith("BOSS_WING"):
            provenance.update(algorithm="wing_pixels(right)", right_exact_mirror_of_left=True,
                              mounting_example_center_dx=61, composite_example_width_px=145)
        else:
            provenance.update(algorithm="core_pixels()" if name == "BOSS_CORE" else "charge_pixels()",
                              palette="cool gunmetal/cyan/platinum; distinct from magenta bullets")
        sprites.append(dict(id=index, name=name, offset=offset, width=width, height=height,
                            stride=width * 2, anchor_x=ax, anchor_y=ay, bytes=len(data),
                            source=provenance, opaque_key_collisions_adjusted=0))
        entries[name] = data
    wing_l = struct.unpack("<" + "H" * (23 * 61), entries["BOSS_WING_L"])
    wing_r = struct.unpack("<" + "H" * (23 * 61), entries["BOSS_WING_R"])
    assert all(wing_l[y * 23 + x] == wing_r[y * 23 + 22 - x] for y in range(61) for x in range(23))
    final = bytes(atlas)
    digest, crc = sha(final), zlib.crc32(final)
    assert len(final) <= BUDGET and len(rows) == 280 and final[:SOURCE_BYTES] == original
    assert sprites[:BASE_COUNT] == source["sprites"]
    header = original_h.replace("Run tools/build_feedback_atlas.py", "Run tools/build_scene_atlas.py")
    header, count = re.subn(r"#define GARDEN_ATLAS_BYTES \d+u", f"#define GARDEN_ATLAS_BYTES {len(final)}u", header)
    assert count == 1
    header, count = re.subn(r"#define GARDEN_ATLAS_CRC32 0x[0-9A-Fa-f]+u", f"#define GARDEN_ATLAS_CRC32 0x{crc:08X}u", header)
    assert count == 1
    marker = "    GARDEN_ASSET_COUNT = 274\n};"
    assert header.count(marker) == 1
    definitions = "\n".join(f"    GARDEN_ASSET_{name} = {index}," for index, name, *_ in APPENDED)
    header = header.replace(marker, definitions + "\n    GARDEN_ASSET_COUNT = 280\n};")
    metadata = original_c.replace("Generated by tools/build_feedback_atlas.py", "Generated by tools/build_scene_atlas.py")
    metadata, count = re.subn(r'(const char garden_atlas_sha256\[65\] = ")[0-9a-f]+(";)',
                              lambda match: match[1] + digest + match[2], metadata)
    assert count == 1
    match = re.search(r"(static const garden_sprite_t sprites\[GARDEN_ASSET_COUNT\] = \{\n)(.*?)(\n\};)", metadata, re.S)
    assert match
    metadata = metadata[:match.start(2)] + match[2] + "\n" + "\n".join(appended_rows) + metadata[match.end(2):]
    assert parse_rows(metadata) == rows
    manifest = copy.deepcopy(source)
    manifest.update(variant="neon_scene_20261014", bytes=len(final), sha256=digest, crc32=f"0x{crc:08X}",
                    budget_bytes=BUDGET, sprites=sprites, reference_variant=SOURCE_VARIANT,
                    reference_atlas_sha256=SOURCE_SHA256, reference_bytes=SOURCE_BYTES,
                    unchanged_prefix_bytes=SOURCE_BYTES, appended_bytes=len(final) - SOURCE_BYTES,
                    remaining_budget_bytes=BUDGET - len(final), generator="tools/build_scene_atlas.py")
    validation = dict(result="PASS", reference_variant=SOURCE_VARIANT,
                      source_atlas_sha256=SOURCE_SHA256, source_atlas_crc32=f"0x{SOURCE_CRC32:08X}",
                      reference_sprite_count=BASE_COUNT, reference_prefix_bytes_unchanged=True,
                      reference_geometry_unchanged=True, reference_sprite_bytes_verified=BASE_COUNT,
                      unchanged_font_slots=FONT_COUNT, font_ids=[FONT_START, FONT_START + FONT_COUNT - 1],
                      original_pngs_verified=len(source_pngs), original_pngs_sha256=source_pngs,
                      new_geometry=new_validation, wings_exact_horizontal_mirrors=True,
                      background_alpha_off_palette_dim=True, all_new_sprites_decorative_only=True,
                      atlas_sha256=digest, atlas_crc32=f"0x{crc:08X}", atlas_bytes=len(final),
                      appended_bytes=len(final) - SOURCE_BYTES, atlas_start=f"0x{ADDRESS:08X}",
                      atlas_end_exclusive=f"0x{ADDRESS + len(final):08X}",
                      reserved_scratch_start=f"0x{ADDRESS + BUDGET:08X}", free_bytes_before_scratch=BUDGET - len(final),
                      max_asset_id=len(rows) - 1, asset_count=len(rows), original_pngs_modified=False,
                      hardware_access=False)
    notice = (reference / "assets/NOTICE.txt").read_text(encoding="utf-8-sig") + (
        "\nNeon Scene appended runtime geometry (2026-10-14)\n"
        "IDs 0..273 retain the exact Feedback runtime pixels and metadata.\n"
        "BOSS_WING_L/R: Team C articulated mirrored satellite-pod geometry.\n"
        "BOSS_CORE: Team C gunmetal/cyan reactor; BOSS_CHARGE: pointed light cue.\n"
        "GARDEN_FAR/STREAK: Team C dim botanical and flow geometry.\n"
        "All six are cached code-native shapes, GPL-3.0-or-later, with Color Key\n"
        "and optional global Alpha. They are decorative only: collision, damage,\n"
        "game timing and imported PNGs are unchanged. scene_preview.png and\n"
        "scene_boss_preview.png are labelled host illustrations, not captures.\n")
    outputs = {"assets/atlas.bin": final, "assets/atlas.sha256": (digest + "  atlas.bin\n").encode("ascii"),
               "assets/manifest.json": json_bytes(manifest), "assets/scene_validation.json": json_bytes(validation),
               "assets/NOTICE.txt": notice.encode("utf-8"), "assets/scene_preview.png": preview_bytes(entries),
               "assets/scene_boss_preview.png": boss_preview_bytes(original, rows, entries),
               "src/garden_assets.h": header.encode("utf-8"), "src/garden_assets.c": metadata.encode("utf-8")}
    checksums = json.loads((reference / "assets/runtime_checksums.json").read_text(encoding="utf-8-sig"))["files"]
    # Preserve the identity of the prior geometry generator/validation/preview.
    for relative in set(checksums) - set(outputs):
        data = (variant / relative).read_bytes()
        assert {"bytes": len(data), "sha256": sha(data)} == checksums[relative]
    checksums.update({name: {"bytes": len(data), "sha256": sha(data)} for name, data in outputs.items()})
    script = Path(__file__).resolve().read_bytes()
    checksums["tools/build_scene_atlas.py"] = {"bytes": len(script), "sha256": sha(script)}
    outputs["assets/runtime_checksums.json"] = json_bytes(dict(format="sha256", atlas_crc32=f"0x{crc:08X}", files=checksums))
    for relative, data in outputs.items():
        path = variant / relative
        if options.check:
            assert path.read_bytes() == data, "Generated output differs: " + relative
        else:
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data)
    print(json.dumps(dict(result="CHECK PASS" if options.check else "GENERATE PASS", asset_count=len(rows),
                          bytes=len(final), appended_bytes=len(final) - SOURCE_BYTES, crc32=f"0x{crc:08X}",
                          sha256=digest, remaining_budget_bytes=BUDGET - len(final),
                          runtime_checksum_files=len(checksums), original_pngs_verified=len(source_pngs)), indent=2))


if __name__ == "__main__":
    main()
