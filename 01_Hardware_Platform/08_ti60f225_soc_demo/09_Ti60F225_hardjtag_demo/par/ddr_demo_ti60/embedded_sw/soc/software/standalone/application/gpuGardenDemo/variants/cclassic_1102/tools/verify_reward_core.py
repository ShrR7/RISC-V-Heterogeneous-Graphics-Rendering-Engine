#!/usr/bin/env python3
"""Finite production-core gameplay checks; no board, UART, Git or MMIO access."""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
import sys


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest().upper()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--gcc", default=shutil.which("gcc") or "D:/mingw64/bin/gcc.exe")
    parser.add_argument("--output-dir", type=Path)
    args = parser.parse_args()
    variant = Path(__file__).resolve().parent.parent
    out = args.output_dir.resolve() if args.output_dir else variant / "build/host/reward_core"
    out.mkdir(parents=True, exist_ok=True)
    source = variant / "tools/test_reward_core.c"
    binary = out / ("test_reward_core.exe" if sys.platform == "win32" else "test_reward_core")
    command = [args.gcc, "-std=c11", "-Wall", "-Wextra", "-Werror", "-O2", str(source), "-o", str(binary)]
    build = subprocess.run(command, capture_output=True, text=True, timeout=60)
    (out / "compile.log").write_text(build.stdout + build.stderr, encoding="utf-8")
    run = subprocess.run([str(binary)], capture_output=True, text=True, timeout=30) if not build.returncode else None
    log = "" if run is None else run.stdout + run.stderr
    (out / "host.log").write_text(log, encoding="utf-8")
    result = re.search(r"^RESULT (.+)$", log, re.MULTILINE)
    metrics = {} if not result else {key: int(value) for key, value in re.findall(r"(\w+)=(\d+)", result.group(1))}
    report = {
        "schema": 1,
        "status": "PASS" if not build.returncode and run is not None and not run.returncode and result else "FAIL",
        "scope": "host production-core rules and deterministic loops only; no board/performance/visual acceptance",
        "compile_exit": build.returncode,
        "test_exit": None if run is None else run.returncode,
        "compile_command": command,
        "metrics": metrics,
        "rules": {
            "bombs_per_run": 2, "bomb_grace_ticks": 40, "bomb_damage": 0,
            "graze_score": 5, "graze_once_per_projectile": True,
            "pickup_score": 25, "pickup_capacity": 8, "live_magnet_pixels": 96,
            "boss_live_budget": 80, "complete_trains": [64, 54, 72],
            "end_ticks": 320, "death_presentation_ticks": 100,
            "high_score_retained_on_restart_only": True,
            "collision_boxes_and_activation_unchanged": True,
        },
        "sources": {str(path.relative_to(variant)).replace("\\", "/"): sha256(path) for path in (
            variant / "src/garden_core.c", variant / "src/garden_core.h", source, Path(__file__).resolve())},
    }
    (out / "verification.json").write_text(json.dumps(report, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    print(json.dumps(report, indent=2, ensure_ascii=False))
    return 0 if report["status"] == "PASS" else 1


if __name__ == "__main__":
    raise SystemExit(main())
