#!/usr/bin/env python3
"""Finite old/new production-core automatic-bomb checks, no hardware access."""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
import sys


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest().upper()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--gcc", default=shutil.which("gcc") or "D:/mingw64/bin/gcc.exe")
    parser.add_argument("--output-dir", type=Path)
    args = parser.parse_args()
    variant = Path(__file__).resolve().parent.parent
    output = args.output_dir.resolve() if args.output_dir else variant / "build/host/demo_bomb"
    output.mkdir(parents=True, exist_ok=True)
    baseline_path = variant / "demo_bomb_baseline.json"
    baseline = json.loads(baseline_path.read_text(encoding="utf-8-sig"))
    source = variant / "src/garden_core.c"
    header = source.with_suffix(".h")
    reference = variant / "tools/reference_demo_bomb/garden_core.c"
    test = variant / "tools/test_demo_bomb.c"
    reference_object = output / "reference_demo_bomb.o"
    binary = output / ("test_demo_bomb.exe" if sys.platform == "win32" else "test_demo_bomb")
    unchanged = {rel: sha(variant / rel) == expected for rel, expected in baseline["runtime_source_sha256"].items() if rel != "src/garden_core.c"}
    reference_ok = sha(reference) == baseline["reference_core_sha256"]
    common = [args.gcc, "-std=c11", "-Wall", "-Wextra", "-Werror", "-O2", "-I", str(variant / "src")]
    commands = [common + ["-Dgarden_init=reference_init", "-Dgarden_step=reference_step", "-Dgarden_boss_fire_countdown=reference_countdown", "-c", str(reference), "-o", str(reference_object)],
                common + [str(test), str(source), str(reference_object), "-o", str(binary)]]
    exits, build_logs = [], []
    if reference_ok and all(unchanged.values()):
        for command in commands:
            run = subprocess.run(command, capture_output=True, text=True, timeout=60)
            exits.append(run.returncode)
            build_logs.append(run.stdout + run.stderr)
            if run.returncode:
                break
    (output / "compile.log").write_text("\n".join(build_logs), encoding="utf-8")
    run = subprocess.run([str(binary)], capture_output=True, text=True, timeout=30) if exits == [0, 0] else None
    text = "" if run is None else run.stdout + run.stderr
    (output / "host.log").write_text(text, encoding="utf-8")
    result = re.search(r"^RESULT (.+)$", text, re.MULTILINE)
    metrics = {} if not result else {key: int(value) for key, value in re.findall(r"(\w+)=(\d+)", result.group(1))}
    traces = []
    for line in text.splitlines():
        if line.startswith("TRACE "):
            tokens = line.split()
            fields = {key: int(value) if value.isdigit() else value for key, value in (part.split("=", 1) for part in tokens[2:])}
            traces.append({"variant": tokens[1], **fields})
    report = {
        "schema": 1, "status": "PASS" if result and run and run.returncode == 0 and exits == [0, 0] and reference_ok and all(unchanged.values()) else "FAIL",
        "scope": "HOST_ONLY actual production-core rules and deterministic traces; no board, visual, FPS or600s acceptance",
        "compile_exit": exits, "test_exit": None if run is None else run.returncode,
        "metrics": metrics, "traces": traces,
        "reference_source_pinned": reference_ok,
        "unchanged_runtime_files": len(unchanged), "unchanged_runtime_hash_checks": unchanged,
        "commands": commands,
        "policy": {"manual_Q_unchanged": True, "auto_bomb_finite_max_uses_per_run": 1,
            "auto_requires": ["current mode already BOSS when forecast is made", "demo pilot", "alive and not respawning", "currently vulnerable", "activated incoming shot withTTL>1", "current offsets each<100px", "6tick forecast offsets x<36px/y<42px", "forecast Manhattan distance decreases"],
            "enemy_bullet_activation_and_collision_boxes_unchanged": True,
            "no_new_arrays_or_persistent_state": True,
            "score_expectation": "derived from real hit/kill/graze/pickup counters each tick; no hard-coded final2220/2225 gate"},
        "sources": {str(path.relative_to(variant)).replace("\\", "/"): sha(path) for path in (source, header, reference, baseline_path, test, Path(__file__).resolve())},
    }
    (output / "verification.json").write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({"status": report["status"], "metrics": metrics, "verification": str(output / "verification.json"), "runtime_core_sha256": sha(source)}, ensure_ascii=False, indent=2))
    return 0 if report["status"] == "PASS" else 1


if __name__ == "__main__":
    raise SystemExit(main())
