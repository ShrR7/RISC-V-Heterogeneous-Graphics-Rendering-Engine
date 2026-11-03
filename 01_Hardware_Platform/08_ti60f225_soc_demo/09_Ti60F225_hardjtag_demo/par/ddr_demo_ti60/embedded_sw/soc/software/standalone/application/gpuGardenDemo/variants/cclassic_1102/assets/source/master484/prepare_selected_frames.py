#!/usr/bin/env python3
"""Deterministically import selected Master484 pixels without touching firmware.

Output dimensions/anchors exactly match the original C RGB565 sprite slots.
Only tight cropping, exact-black transparency, nearest-neighbour resizing,
and explicitly recorded substitutions from the author's own palette are used.
No new geometry, AI generation, runtime rotation, or extra atlas frames.
"""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

HERE = Path(__file__).resolve().parent
APP = HERE.parents[2]
SOURCES = {
    "M484BulletCollection2.png": {
        "sha256": "cae852d8f4511cd1172a2e302faa91110372ad574a14b7f32907df88d5eefee8",
        "page": "https://opengameart.org/content/bullet-collection-2-m484-games",
        "download": "https://opengameart.org/sites/default/files/M484BulletCollection2.png",
        "author": "Master484", "license": "CC0-1.0",
    },
    "M484ExplosionSet2.png": {
        "sha256": "8896d066127c80880619980c0baf29809e8008dae9180985fad615fbed1643a0",
        "page": "https://opengameart.org/content/explosion-set-2-m484-games",
        "download": "https://opengameart.org/sites/default/files/M484ExplosionSet2.png",
        "author": "Master484", "license": "CC0-1.0",
    },
}
# Native 16-direction wheels: select east, SE, south, SW, west, NW, north, NE.
# Source crops are half-open x0,y0,x1,y1; all annotations/examples are excluded.
DART_BOXES = [(455,116,469,122), (455,139,466,149),
              (429,138,435,152), (398,139,409,149),
              (395,116,409,122), (398,89,409,99),
              (429,85,435,99), (455,89,466,99)]
DIAMOND_BOXES = [(457,200,473,207), (457,227,470,239),
                 (429,227,436,243), (395,227,408,239),
                 (392,200,408,207), (395,168,408,180),
                 (429,164,436,180), (457,168,470,180)]
CIRCLE_BOXES = {
    "CIRCLE_RED": (253,109,262,118),
    "CIRCLE_BLUE": (253,217,262,226),
    "CIRCLE_GREEN": (253,164,262,173),
}
PALETTE_INPUT = [(130,58,11), (179,81,15), (211,96,18),
                 (249,152,21), (255,201,14), (255,255,255)]
PALETTES = {
    1: [(173,37,108)]*2 + [(255,85,89)]*2 + [(255,140,143), (255,255,255)],
    2: [(28,89,140)]*2 + [(75,121,241)]*2 + [(149,175,247), (255,255,255)],
    3: [(66,168,36)]*2 + [(35,237,86)]*2 + [(141,245,168), (255,255,255)],
    4: PALETTE_INPUT,
    5: [(72,57,253)]*2 + [(120,109,254)]*2 + [(159,152,254), (255,255,255)],
}
PALETTE_REFERENCES = {
    1: [("M484BulletCollection2.png", [63,406,75,413]),
        ("M484BulletCollection2.png", list(CIRCLE_BOXES["CIRCLE_RED"])),
        ("M484BulletCollection2.png", [390,80,475,248])],
    2: [("M484BulletCollection2.png", [197,406,209,413]),
        ("M484BulletCollection2.png", list(CIRCLE_BOXES["CIRCLE_BLUE"])),
        ("M484BulletCollection2.png", [390,80,475,248])],
    3: [("M484BulletCollection2.png", [130,406,142,413]),
        ("M484BulletCollection2.png", list(CIRCLE_BOXES["CIRCLE_GREEN"])),
        ("M484BulletCollection2.png", [390,80,475,248])],
    4: [("M484BulletCollection2.png", [390,80,475,248])],
    5: [("M484ExplosionSet2.png", [226,560,256,590]),
        ("M484BulletCollection2.png", [390,80,475,248])],
}


def sha(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def flatten(image: Image.Image):
    return list(image.get_flattened_data()) if hasattr(image, "get_flattened_data") else list(image.getdata())


def transparent_crop(source: Image.Image, box) -> Image.Image:
    image = source.crop(box).convert("RGBA")
    image.putdata([(0,0,0,0) if p[:3] == (0,0,0) else (*p[:3],255)
                   for p in flatten(image)])
    return image


def recolour(image: Image.Image, family: int) -> Image.Image:
    mapping = dict(zip(PALETTE_INPUT, PALETTES[family]))
    image = image.copy()
    pixels = []
    for p in flatten(image):
        if p[3] == 0:
            pixels.append(p)
        else:
            assert p[:3] in mapping, f"Unexpected source colour {p}"
            pixels.append((*mapping[p[:3]], 255))
    image.putdata(pixels)
    return image


def fit(image: Image.Image, record: dict, tail_direction: int | None):
    w, h = record["width"], record["height"]
    available = (w-2, h-2)
    scale = min(available[0]/image.width, available[1]/image.height)
    tw, th = max(1, round(image.width*scale)), max(1, round(image.height*scale))
    resized = image.resize((tw,th), Image.Resampling.NEAREST)
    ax, ay = record["anchor_x"], record["anchor_y"]
    if tail_direction is None:
        x,y = ax-tw//2, ay-th//2
    else:
        # The previous dart's anchor is its tail, not its image centre.
        positions = [(ax,ay-th//2), (ax,ay), (ax-tw//2,ay),
                     (ax-tw+1,ay), (ax-tw+1,ay-th//2),
                     (ax-tw+1,ay-th+1), (ax-tw//2,ay-th+1),
                     (ax,ay-th+1)]
        x,y = positions[tail_direction]
    assert x >= 0 and y >= 0 and x+tw <= w and y+th <= h
    out = Image.new("RGBA", (w,h))
    out.paste(resized, (x,y))
    return out, {"method": "nearest-neighbour", "size": [tw,th],
                 "paste_xy": [x,y], "geometry": "preserve aspect ratio; no runtime rotation"}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--baseline-manifest", type=Path, default=APP/"assets/manifest.json")
    parser.add_argument("--out", type=Path, default=HERE/"prepared")
    args = parser.parse_args()
    out = args.out.resolve()
    out.mkdir(parents=True, exist_ok=True)
    originals = json.loads(args.baseline_manifest.read_text(encoding="utf-8-sig"))["sprites"]
    by_name = {record["name"]: record for record in originals}
    source_images = {}
    for filename, info in SOURCES.items():
        data = (HERE/filename).read_bytes()
        assert sha(data) == info["sha256"], filename
        source_images[filename] = Image.open(HERE/filename).convert("RGBA")
        info["bytes"] = len(data)
        info["dimensions"] = list(source_images[filename].size)
    # Every target palette RGB must exist in the cited author's source crops.
    for family, colours in PALETTES.items():
        cited = set()
        for filename, box in PALETTE_REFERENCES[family]:
            cited.update(p[:3] for p in flatten(source_images[filename].crop(box)))
        assert set(colours) <= cited, (family, set(colours)-cited)
    selections = []
    for name, box in CIRCLE_BOXES.items():
        selections.append((name, "M484BulletCollection2.png", box, None, None))
    selections.append(("SHOT", "M484BulletCollection2.png", (308,182,314,205), None, None))
    for family in range(1,6):
        for direction in range(8):
            selections.append((f"DART_B{family}_R{direction}", "M484BulletCollection2.png",
                               DART_BOXES[direction], family, direction))
            selections.append((f"DIAMOND_B{family}_R{direction}", "M484BulletCollection2.png",
                               DIAMOND_BOXES[direction], family, None))
    # 30x30 interior of source middle-row effect cells (grid lines excluded).
    for frame, source_frame in enumerate((0,1,3,5)):
        x = 22+31*source_frame
        selections.append((f"EXPLOSION{frame}", "M484ExplosionSet2.png",
                           (x,238,x+30,268), None, None))
    records = []
    for name, filename, box, family, tail_direction in selections:
        old = by_name[name]
        raw_crop = source_images[filename].crop(box)
        image = transparent_crop(source_images[filename], box)
        assert image.getbbox(), name
        palette = []
        if family is not None:
            image = recolour(image, family)
            palette = [{"source_rgb": list(k), "target_rgb": list(v)}
                       for k,v in zip(PALETTE_INPUT,PALETTES[family]) if k != v]
        if name.startswith("CIRCLE_"):
            assert image.size == (old["width"],old["height"])
            transform = {"method": "none", "size": list(image.size), "paste_xy": [0,0]}
        elif name.startswith("EXPLOSION") or name == "SHOT":
            target_size = (old["width"],old["height"])
            image = image.resize(target_size, Image.Resampling.NEAREST)
            transform = {"method": "nearest-neighbour", "size": list(target_size), "paste_xy": [0,0]}
        else:
            image, transform = fit(image,old,tail_direction)
        target = out/(name+".png")
        image.save(target, optimize=False)
        assert all(p[3] in (0,255) for p in flatten(image)), name
        assert image.size == (old["width"],old["height"])
        records.append({
            "name": name, "original_id": old["id"], "png": target.name,
            "width": old["width"], "height": old["height"],
            "anchor_x": old["anchor_x"], "anchor_y": old["anchor_y"],
            "source_file": filename, "source_sha256": SOURCES[filename]["sha256"],
            "source_crop_xyxy_exclusive": list(box),
            "source_crop_rgba_sha256": sha(raw_crop.tobytes()),
            "transparency": "exact RGB (0,0,0) -> alpha0; all other pixels alpha255",
            "palette_substitution": palette,
            "palette_reference_crops": PALETTE_REFERENCES.get(family, []),
            "resize": transform,
            "rgba_sha256": sha(image.tobytes()), "png_sha256": sha(target.read_bytes()),
            "opaque_pixels": sum(p[3] != 0 for p in flatten(image)),
            "rgb565_slot_bytes": image.width*image.height*2,
            "author": "Master484", "license": "CC0-1.0",
        })
    assert len(records) == 88 and len({r["original_id"] for r in records}) == 88
    manifest = {
        "schema": "master484-selected-frames-v1", "author": "Master484", "license": "CC0-1.0",
        "license_evidence": "Each unmodified PNG embeds PUBLIC DOMAIN / CC0 author notice; see source pages.",
        "source_files": SOURCES, "baseline_manifest_sha256": sha(args.baseline_manifest.read_bytes()),
        "script_sha256": sha(Path(__file__).read_bytes()), "sprites": sorted(records,key=lambda r:r["original_id"]),
        "rgb565_slot_bytes": sum(r["rgb565_slot_bytes"] for r in records),
        "atlas_budget_effect": "replace existing IDs at identical dimensions/anchors; zero extra atlas bytes or frames",
        "runtime_changes": "none; no gameplay/collision/FPGA/serial changes",
        "adaptations": ["Tight native sprite selection; no text/grid/example graphics",
                        "Exact-black transparency from author black-backed sheets",
                        "Select eight of the native sixteen directions; no rotation interpolation",
                        "White-core directional frames use author's original red/blue/green palette ramps",
                        "Purple ramp imported from same author's ExplosionSet2; source has no purple directional wheel",
                        "Nearest-neighbour offline size fit to existing RGB565 slots; original anchors retained"],
    }
    (out/"manifest.json").write_text(json.dumps(manifest,ensure_ascii=False,indent=2)+"\n",encoding="utf-8")
    # Compact inspection sheet only; never imported into RGB565 atlas.
    columns,cellw,cellh = 8,100,76
    sheet = Image.new("RGB",(columns*cellw,((len(records)+columns-1)//columns)*cellh),(9,15,28))
    draw = ImageDraw.Draw(sheet)
    for i,record in enumerate(sorted(records,key=lambda r:r["original_id"])):
        x,y = (i%columns)*cellw,(i//columns)*cellh
        image = Image.open(out/record["png"])
        scale = max(1,min(2,(cellw-4)//image.width,(cellh-20)//image.height))
        shown = image.resize((image.width*scale,image.height*scale),Image.Resampling.NEAREST)
        sheet.paste(shown,(x+(cellw-shown.width)//2,y+2),shown)
        draw.text((x+3,y+cellh-15),record["name"],fill=(220,230,244))
    sheet.save(out/"selected_preview.png")
    print(json.dumps({"out": str(out), "sprites":len(records),
                      "rgb565_slot_bytes":manifest["rgb565_slot_bytes"],
                      "extra_atlas_bytes":0},ensure_ascii=False))


if __name__ == "__main__":
    main()
