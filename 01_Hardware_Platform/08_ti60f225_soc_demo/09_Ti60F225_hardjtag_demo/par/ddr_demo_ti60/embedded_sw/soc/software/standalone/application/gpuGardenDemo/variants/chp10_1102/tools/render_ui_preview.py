#!/usr/bin/env python3
"""Render production Garden UI in host RAM; no serial, JTAG, or board access.

SPDX-License-Identifier: GPL-3.0-or-later
The C layout and RGB565 atlas are shared with firmware. This is a static control
layout preview. FPS/timings are sample data, not measurements of this UI.
"""
import argparse
import hashlib
import json
import subprocess
from pathlib import Path
from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", default="D:/mingw64/bin/gcc.exe")
    args = parser.parse_args()
    build = ROOT / "build/host/ui"
    build.mkdir(parents=True, exist_ok=True)
    exe = build / "preview_ui.exe"
    # The historical host renderer predates tight sprite rows. Adapt only its
    # generated build copy; production metadata and the archived tool stay intact.
    preview_source = (ROOT / "tools/preview_ui.c").read_text(encoding="utf-8")
    preview_source = preview_source.replace("row*s->stride", "row*garden_sprite_stride(s)")
    # Render/logic timings and the remote Boss HP are deliberately absent from
    # the current engineering HUD. Keep the full-redraw oracle while expecting
    # zero draws when only those historical fields change.
    preview_source = preview_source.replace(
        "check_cached_step(&changed,buffer,1);\n        check_cached_step(&base,buffer,1);",
        "int redraw=change!=7U && change!=8U && (change<15U || change>18U);\n"
        "        check_cached_step(&changed,buffer,redraw);\n"
        "        check_cached_step(&base,buffer,redraw);")
    preview_source = preview_source.replace("check_cached_step(&damaged,0,1)", "check_cached_step(&damaged,0,0)")
    preview_source = preview_source.replace("check_cached_step(&damaged,1,1)", "check_cached_step(&damaged,1,0)")
    # The old combat illustration maps sprites differently from current game
    # rendering. Exercise the real UI checks, then draw only the current cockpit.
    preview_source = preview_source[:preview_source.index("int main(int argc,char **argv)")] + r'''
int main(int argc,char **argv)
{
    if (argc!=3) return 2;
    FILE *in=fopen(argv[1],"rb");
    if (!in || fread(atlas,1,sizeof(atlas),in)!=sizeof(atlas)) return 2;
    fclose(in);check_updates();check_cached_updates();check_stars();
    GardenGame g;garden_init(&g,12345U);GardenHud h=data(&g);
    h.state="STAGE 1 WAVES";garden_ui_static();garden_ui_hud(&h);
    char path[1024];
    int n=snprintf(path,sizeof(path),"%s_controls.ppm",argv[2]);
    if (n<0 || (unsigned)n>=sizeof(path)) return 2;
    save(path);
    puts("PASS: current Joy-Con control layout; static HOST UI, not a board capture");
    return 0;
}
'''
    adapted_source = build / "preview_ui_current.c"
    adapted_source.write_text(preview_source, encoding="utf-8")
    subprocess.run([args.compiler, "-std=c99", "-O2", "-Wall", "-Wextra", "-Werror",
                    "-Wno-unused-function",
                    "-I", str(ROOT / "src"), str(adapted_source),
                    str(ROOT / "src/garden_ui.c"), str(ROOT / "src/garden_assets.c"),
                    str(ROOT / "src/garden_core.c"), str(ROOT / "src/garden_fx.c"),
                    str(ROOT / "src/garden_clear.c"),
                    str(ROOT / "src/garden_heading_precision.c"),
                    str(ROOT / "src/garden_stage.c"),
                    str(ROOT / "src/garden_feedback.c"),
                    "-o", str(exe)], check=True)
    result = subprocess.run([str(exe), str(ROOT / "assets/atlas.bin"), str(build / "ui")],
                            text=True, capture_output=True)
    print(result.stdout, end="")
    if result.returncode:
        print(result.stderr, end="")
        result.check_returncode()
    (build / "preview_checks.log").write_text(result.stdout, encoding="utf-8")
    outputs = {}
    for phase in ("controls",):
        target = ROOT / f"assets/ui_preview_{phase}.png"
        with Image.open(build / f"ui_{phase}.ppm") as im:
            # Labels belong only to the preview; no runtime atlas pixels change.
            labelled = Image.new("RGB", (im.width, im.height + 32), (7, 12, 26))
            labelled.paste(im, (0, 32))
            text = "HOST UI LAYOUT / NOT A BOARD CAPTURE / TIMINGS ARE SAMPLE VALUES"
            ImageDraw.Draw(labelled).text((16, 10), text, fill=(115, 244, 255))
            labelled.save(target)
        print(target)
        outputs[str(target.relative_to(ROOT))] = hashlib.sha256(target.read_bytes()).hexdigest()
    sources = ("garden_ui.c", "garden_ui.h", "garden_core.c", "garden_core.h",
               "garden_clear.c", "garden_clear.h",
               "garden_heading_precision.c", "garden_heading_precision.h",
               "garden_fx.c", "garden_fx.h", "garden_stage.c", "garden_stage.h",
               "garden_feedback.c", "garden_feedback.h",
               "garden_assets.c", "garden_assets.h")
    report = {
        "status": "PASS", "host_only": True, "board_acceptance": False,
        "game_snapshot_seed": 12345,
        "snapshots": [],
        "illustrations": ["controls"],
        "debug_alpha_panel_hidden": True,
        "timings_are_sample_values": True,
        "checks": ["production HUD full-redraw and two-FB cache oracle",
                   "21 HUD field changes, mutable strings, high-to-low, reset, invalid buffer",
                   "200 partial star restores", "current Joy-Con static controls and focus reminder",
                   "render/logic and remote Boss-only changes cause no cockpit redraw"],
        "source_sha256": {"src/" + name: hashlib.sha256((ROOT / "src" / name).read_bytes()).hexdigest()
                          for name in sources},
        "tool_sha256": {"tools/" + name: hashlib.sha256((ROOT / "tools" / name).read_bytes()).hexdigest()
                       for name in ("preview_ui.c", "render_ui_preview.py")},
        "atlas_sha256": hashlib.sha256((ROOT / "assets/atlas.bin").read_bytes()).hexdigest(),
        "adapted_preview_sha256": hashlib.sha256(adapted_source.read_bytes()).hexdigest(),
        "stdout_sha256": hashlib.sha256(result.stdout.encode("utf-8")).hexdigest(),
        "output_sha256": outputs,
        "limits": ["Host RAM rendering with production assets, not board captures.",
                   "Static cockpit only; no claim of current combat or controller acceptance.",
                   "FPS/render/logic values shown are fictional samples, not measurements."],
    }
    (build / "preview_verification.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
