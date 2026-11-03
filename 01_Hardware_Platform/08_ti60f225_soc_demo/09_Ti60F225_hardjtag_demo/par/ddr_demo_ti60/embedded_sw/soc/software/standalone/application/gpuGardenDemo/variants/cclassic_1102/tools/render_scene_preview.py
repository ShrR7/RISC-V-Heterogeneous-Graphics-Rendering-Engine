#!/usr/bin/env python3
"""Render actual-core mechanical Boss/parallax frames in host RGB565 RAM.

SPDX-License-Identifier: GPL-3.0-or-later
No serial, JTAG, GPU/FPGA access. New and original sprites come from the runtime
atlas. The production core/observers/builders produce every projectile/pose.
HUD FPS/timings are illustrative samples, not performance measurements.
"""
import argparse
import hashlib
import json
import subprocess
from pathlib import Path
from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[1]
SOURCE_NAMES = (
    "garden_ui.c", "garden_ui.h", "garden_core.c", "garden_core.h",
    "garden_fx.c", "garden_fx.h", "garden_stage.c", "garden_stage.h",
    "garden_feedback.c", "garden_feedback.h", "garden_scene.c", "garden_scene.h",
    "garden_assets.c", "garden_assets.h", "gpu_alpha_ref.h", "garden_render.h",
)


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", default="D:/mingw64/bin/gcc.exe")
    args = parser.parse_args()
    build = ROOT / "build/host/scene_preview"
    build.mkdir(parents=True, exist_ok=True)
    exe = build / "preview_scene.exe"
    command = [args.compiler, "-std=c99", "-O2", "-Wall", "-Wextra", "-Werror",
               "-I", str(ROOT / "src"), str(ROOT / "tools/preview_scene.c")]
    command += [str(ROOT / "src" / name) for name in SOURCE_NAMES if name.endswith(".c")]
    command += ["-o", str(exe)]
    subprocess.run(command, check=True)
    result = subprocess.run([str(exe), str(ROOT / "assets/atlas.bin"), str(build / "scene")],
                            check=True, capture_output=True, text=True)
    print(result.stdout, end="")
    (build / "preview_checks.log").write_text(result.stdout, encoding="utf-8")
    records = [json.loads(line) for line in result.stdout.splitlines() if line.startswith("{")]
    by_name = {row["name"]: row for row in records}
    outputs = {}
    for phase in ("boss", "charge", "parallax_a", "parallax_b"):
        row = by_name[phase]
        target = ROOT / f"assets/scene_runtime_{phase}_preview.png"
        with Image.open(build / f"scene_{phase}.ppm") as im:
            labelled = Image.new("RGB", (im.width, im.height + 56), (7, 12, 26))
            labelled.paste(im, (0, 32))
            draw = ImageDraw.Draw(labelled)
            draw.text((16, 10), "HOST ACTUAL-CORE SNAPSHOT / NOT BOARD / HUD FPS AND TIMINGS ARE SAMPLES",
                      fill=(115, 244, 255))
            caption = (f"SEED 12345 / TICK {row['tick']} / DRAWS {row['draws']}/240 / "
                       f"VISIBLE BULLETS {row['visible_bullets']} / CHARGE {row['charge_countdown']} TICKS / "
                       f"BACKGROUND SPRITES {row['backgrounds']}")
            draw.text((16, im.height + 40), caption, fill=(115, 244, 255))
            labelled.save(target)
        outputs[str(target.relative_to(ROOT))] = digest(target)
        print(target)
    frames = []
    for i in range(5):
        row = by_name[f"charge_{i}"]
        with Image.open(build / f"scene_charge_{i}.ppm") as im:
            crop = im.crop((320, 120, 960, 600))
            labelled = Image.new("RGB", (640, 524), (7, 12, 26))
            labelled.paste(crop, (0, 44))
            draw = ImageDraw.Draw(labelled)
            draw.text((12, 6), "HOST / NOT BOARD / 5 REAL CORE SNAPSHOTS / SLOWED PREVIEW", fill=(115, 244, 255))
            draw.text((12, 24), f"TICK {row['tick']}  CHARGE {row['charge_countdown']}  BURST {row['burst_active']}  BULLETS {row['visible_bullets']}",
                      fill=(255, 215, 130))
            frames.append(labelled.copy())
    gif = ROOT / "assets/scene_runtime_charge_preview.gif"
    frames[0].save(gif, save_all=True, append_images=frames[1:], duration=250, loop=0,
                   disposal=2, optimize=False)
    outputs[str(gif.relative_to(ROOT))] = digest(gif)
    print(gif)
    # Different actual core ticks also move both backdrop layers. The image
    # geometry comes from production lists, not retouched star motion.
    report = {
        "status": "PASS", "host_only": True, "board_acceptance": False,
        "game_snapshot_seed": 12345, "snapshots": records,
        "timings_are_sample_values": True,
        "animations_are_slowed_snapshots": True,
        "checks": ["actual core snapshots with normal Boss flash=0 and full entrance opacity",
                   "genuine <=12-tick pre-shot anticipation and counter-backed release",
                   "new two-layer backdrop, paired wings and reactor use exact runtime RGB565 atlas",
                   "two physical framebuffer dirty-history RAM oracle equals clean full redraw",
                   "12 paused draw-list and RGB565 frames remain identical"],
        "source_sha256": {"src/" + name: digest(ROOT / "src" / name) for name in SOURCE_NAMES},
        "integration_source_sha256": {"src/" + name: digest(ROOT / "src" / name)
                                      for name in ("main.c", "garden_render.c")},
        "tool_sha256": {"tools/" + name: digest(ROOT / "tools" / name)
                       for name in ("preview_scene.c", "render_scene_preview.py")},
        "atlas_sha256": digest(ROOT / "assets/atlas.bin"),
        "stdout_sha256": hashlib.sha256(result.stdout.encode("utf-8")).hexdigest(),
        "output_sha256": outputs,
        "limits": ["RGB565 host rendering is not a board capture or hardware acceptance.",
                   "FPGA/AXI/DDR arbitration and display timing are not exercised.",
                   "Sprite-band/pixel counts are estimates of list geometry, not measured render cost.",
                   "GIF frames are real ticks spaced by 3 logic steps but displayed slowly at 250ms/frame.",
                   "HUD FPS/render/logic data are illustrative samples."],
    }
    (build / "preview_verification.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
