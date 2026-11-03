#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Compare current production core with a hash-verified scene core in another TU.

No board, serial, FPGA or MMIO calls. Baseline paths are explicit/portable;
the source/header hashes must match combat_baseline.json before compilation.
"""
import argparse
import datetime
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
BASE = ROOT.parent / "neon_scene_20261014"
BRIDGE_SOURCE = r"""
/* Generated host-only bridge: legacy GardenGame is never reinterpreted as
 * the larger new type. Copies use its verified original size. */
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "garden_core.h"
static GardenGame legacy;
size_t combat_legacy_size(void) { return sizeof(legacy); }
void combat_legacy_init(uint32_t seed) { legacy_garden_init(&legacy, seed); }
void combat_legacy_load(const void *prefix, size_t size)
{
    if (size != sizeof(legacy)) abort();
    memcpy(&legacy, prefix, size);
}
void combat_legacy_step(uint16_t buttons)
{ legacy_garden_step(&legacy, buttons); }
void combat_legacy_copy(void *prefix, size_t size)
{
    if (size != sizeof(legacy)) abort();
    memcpy(prefix, &legacy, size);
}
"""


def sha(data):
    return hashlib.sha256(data).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", default="D:/mingw64/bin/gcc.exe")
    parser.add_argument("--baseline", type=Path, default=BASE)
    args = parser.parse_args()
    baseline = args.baseline.resolve()
    manifest_path = ROOT / "combat_baseline.json"
    manifest_bytes = manifest_path.read_bytes()
    manifest = json.loads(manifest_bytes.decode("utf-8-sig"))
    expected = {entry["path"]: entry["source_sha256"]
                for entry in manifest["files"]}
    legacy_names = ("src/garden_core.c", "src/garden_core.h")
    legacy_bytes = {}
    for name in legacy_names:
        data = (baseline / name).read_bytes()
        if sha(data) != expected[name]:
            raise SystemExit(f"Refusing untrusted legacy baseline: {name}")
        legacy_bytes[name] = data
    build = ROOT / "build/host/combat_core"
    build.mkdir(parents=True, exist_ok=True)
    legacy = build / "legacy_scene"
    legacy.mkdir(exist_ok=True)
    report_path = build / "verification.json"
    if report_path.exists():
        report_path.unlink()
    for name, data in legacy_bytes.items():
        (legacy / Path(name).name).write_bytes(data)
    (legacy / "bridge.c").write_text(BRIDGE_SOURCE, encoding="utf-8")
    # Prove a local altered baseline is rejected before compilation. Only
    # generated host fixtures are touched; original scene evidence stays read-only.
    rejected_legacy = build / "rejected_legacy"
    (rejected_legacy / "src").mkdir(parents=True, exist_ok=True)
    for name, data in legacy_bytes.items():
        (rejected_legacy / name).write_bytes(
            data + (b"\n/* deliberately untrusted fixture */\n"
                    if name.endswith(".c") else b""))
    rejection = subprocess.run(
        [sys.executable, str(Path(__file__).resolve()), "--baseline",
         str(rejected_legacy)], capture_output=True, text=True, timeout=30)
    rejection_log = rejection.stdout + rejection.stderr
    if not rejection.returncode or "Refusing untrusted legacy baseline" not in rejection_log:
        raise SystemExit("Verifier failed to reject altered baseline source")
    (build / "baseline_rejection.log").write_text(rejection_log, encoding="utf-8")
    exe = build / "test_combat_core.exe"
    compiler_flags = ["-std=c99", "-O2", "-Wall", "-Wextra", "-Werror"]
    rename = ["-Dgarden_init=legacy_garden_init",
              "-Dgarden_step=legacy_garden_step"]
    commands = [
        [args.compiler, *compiler_flags, *rename, "-I", str(legacy),
         "-c", str(legacy / "garden_core.c"), "-o", str(build / "legacy_core.o")],
        [args.compiler, *compiler_flags, *rename, "-I", str(legacy),
         "-c", str(legacy / "bridge.c"), "-o", str(build / "legacy_bridge.o")],
        [args.compiler, *compiler_flags, "-I", str(ROOT / "src"),
         str(ROOT / "tools/test_combat_core.c"),
         str(ROOT / "src/garden_core.c"), str(build / "legacy_core.o"),
         str(build / "legacy_bridge.o"), "-o", str(exe)],
    ]
    compile_outputs = []
    for command in commands:
        result = subprocess.run(command, text=True, capture_output=True)
        compile_outputs.append(result.stdout + result.stderr)
        if result.returncode:
            (build / "compile.log").write_text(
                "".join(compile_outputs), encoding="utf-8")
            print(result.stdout + result.stderr, end="", file=sys.stderr)
            raise SystemExit(result.returncode)
    (build / "compile.log").write_text(
        "".join(compile_outputs), encoding="utf-8")
    result = subprocess.run([str(exe)], capture_output=True, text=True)
    log = result.stdout + result.stderr
    (build / "test_combat_core.log").write_text(log, encoding="utf-8")
    print(log, end="")
    if result.returncode:
        raise SystemExit(result.returncode)
    match = re.search(
        r"combat core checks=(\d+) prefix_compared_steps=(\d+) "
        r"observed_hits=(\d+) observed_kills=(\d+) boss_hits=(\d+) "
        r"victories=(\d+) auto_restarts=(\d+)", result.stdout)
    if not match:
        raise SystemExit("Missing real completed core contract summary")
    metrics = dict(zip(("contract_checks", "prefix_compared_steps",
                        "observed_hits", "observed_kills", "boss_hits",
                        "victories", "automatic_restarts"),
                       map(int, match.groups())))
    if metrics["prefix_compared_steps"] < 24000:
        raise SystemExit("Insufficient original-vs-new state comparisons")
    source_names = ("src/garden_core.c", "src/garden_core.h",
                    "tools/test_combat_core.c", "tools/verify_combat_core.py")
    source_sha = {name: sha((ROOT / name).read_bytes()) for name in source_names}
    source_sha["combat_baseline.json"] = sha(manifest_bytes)
    report = {
        "status": "PASS",
        "host_only": True,
        "board_acceptance": False,
        "completed_at_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
        **metrics,
        "compiler_commands": commands,
        "source_sha256": source_sha,
        "trusted_legacy_input_commit": manifest["input_commit"],
        "trusted_legacy_sha256": {name: sha(data)
                                  for name, data in legacy_bytes.items()},
        "legacy_bridge_sha256": sha(BRIDGE_SOURCE.encode("utf-8")),
        "altered_baseline_rejected_before_compilation": True,
        "baseline_rejection_log_sha256": sha(rejection_log.encode("utf-8")),
        "legacy_size_equals_new_combat_offset": True,
        "legacy_physics_prefix_byte_identical_except_score": True,
        "authorized_scoring_rule": manifest["authorized_rule_change"],
        "completed_stdout_sha256": sha(result.stdout.encode("utf-8")),
        "test_log_sha256": sha(log.encode("utf-8")),
        "checks": [
            "Independent baseline TU, verified SHA256, correctly typed byte bridge",
            "24000+ complete gameplay prefixes normalize score only",
            "Multiple vulnerable automatic wins/restarts and manual/slow/diagonal input",
            "Authoritative projectile contact and raw target sprite coordinates",
            "All per-step target HP loss equals summed event damage; one-use consumed-projectile proof",
            "Exact effective damage with bounded overkill and retained kill rewards",
            "Actual score difference equals accumulated hit points in normal epochs",
            "Two simultaneous hits, first enemy collision priority, inclusive borders",
            "24 simultaneous projectile hits with no truncation",
            "No miss/offscreen/negative-y Boss/zero-damage credit",
            "Consumed projectile idempotence, pause/end/restart clears",
            "UINT32_MAX score and accounting counters saturate without wrap",
        ],
        "limits": [
            "This is production game core under a host compiler; no board performance or pixel claim.",
            "Presentation, serial loop and hardware drivers are outside this suite.",
            "Overflow fixtures intentionally use saturated scoring; old core overflow is not retained.",
        ],
    }
    report_path.write_text(
        json.dumps(report, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
