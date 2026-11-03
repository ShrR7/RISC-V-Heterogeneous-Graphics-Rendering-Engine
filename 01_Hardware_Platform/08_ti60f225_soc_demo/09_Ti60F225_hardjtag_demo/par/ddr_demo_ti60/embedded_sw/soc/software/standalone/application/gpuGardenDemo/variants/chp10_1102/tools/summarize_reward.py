#!/usr/bin/env python3
"""Read-only R UART audit. Functional evidence and strict FPS are independent.

No log means PENDING. --self-test writes explicitly SYNTHETIC parser fixtures
under build/host/summarize_reward, never board evidence. No MMIO/serial access.
"""
from __future__ import annotations
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import re
import tempfile

FIELDS = re.compile(r"\b([A-Za-z_][A-Za-z_0-9]*)=([^\s;]+)")
FAULTS = ("errors", "swap_timeout", "front_reject", "atlas_bounds", "missed", "log_dropped", "log_truncated")
REWARD = ("tick", "mode", "phase", "bombs", "used", "cleared", "graze", "cores", "spawned", "lost", "score", "best", "run_ticks", "hits")
VARIANT = Path(__file__).resolve().parent.parent


def number(token):
    try:
        return int(token, 16 if token.lower().startswith("0x") else 10)
    except ValueError:
        return token


def row(line, index):
    return {"line": index + 1, "raw": line, **{key: number(value) for key, value in FIELDS.findall(line)}}


class MemoryLog:
    """Native display analyzer accepts read_bytes/resolve; no temp UART copy."""
    def __init__(self, text):
        self.data = text.encode("utf-8")

    def read_bytes(self):
        return self.data

    def resolve(self):
        return "<selected-current-R-boot-in-memory>"


def display_review(text):
    spec = importlib.util.spec_from_file_location("reward_native_display7", Path(__file__).with_name("analyze_display7.py"))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    try:
        native = module.analyze([MemoryLog(text)])
    except (KeyError, ValueError, TypeError) as error:
        return {"status": "FAIL", "native_format_status": "FORMAT_FAIL", "parse_error": str(error)}
    pairs = native["pairs"]
    deltas, reversals = [], []
    previous = None
    for pair in pairs:
        current = pair["aux"]["total"]
        if previous is not None:
            delta = [new - old for new, old in zip(current, previous)]
            deltas.append({"rid": pair["rid"], "delta": delta})
            if any(value < 0 for value in delta):
                reversals.append({"rid": pair["rid"], "delta": delta})
        previous = current
    running = native["runtime_window_observed_counts"]
    underrun = [running[i] for i in (0, 1, 4)]
    lower_bounds = native["runtime_counter_lower_bound_fields"]
    malformed = bool(native["issues"] or reversals or native["status"] == "FORMAT_FAIL")
    complete = bool(native["GAME_START_boundary_verified"] and native["runtime_complete_pairs"] and not native["incomplete_rids"] and not native["counter_total_window_gaps"])
    status = "FAIL" if malformed or any(underrun) else "PASS" if complete and not any(name in lower_bounds for name in ("PX", "PART", "FULL")) else "PENDING"
    return {
        "status": status, "native_format_status": native["status"],
        "counter_order": list(native["counter_order"]), "runtime_px_part_full": underrun,
        "runtime_window_counts": running, "all_window_counts": native["window_observed_counts"],
        "AR_R_stalls_are_not_functional_errors": True,
        "complete_pairs": native["complete_pairs"], "runtime_complete_pairs": native["runtime_complete_pairs"],
        "game_start_verified": native["GAME_START_boundary_verified"],
        "runtime_seconds": native["runtime_counter_duration_seconds"],
        "incomplete_rids": native["incomplete_rids"], "issues": native["issues"],
        "window_total_gaps": native["counter_total_window_gaps"],
        "total_deltas": deltas, "total_reversals_not_coerced_to_zero": reversals,
        "lower_bound_fields": lower_bounds, "overflow_pairs": native["runtime_overflow_pairs"],
        "refused_max": native["refused_max"],
        "scope": "Native paired windows after GAME_START; startup/bench counters remain separate. Saturation is a lower bound, never exact D.",
    }


def analyze(paths, require_keys=(), synthetic=False):
    inputs, all_lines, partial_tails = [], [], []
    for path in paths:
        raw = path.read_bytes()
        text = raw.decode("utf-8-sig", errors="replace")
        lines = text.splitlines()
        partial = bool(text and not text.endswith(("\n", "\r")))
        if partial:
            lines = lines[:-1]
            partial_tails.append(str(path))
        inputs.append({"path": str(path.resolve()), "bytes": len(raw), "sha256": hashlib.sha256(raw).hexdigest().upper(), "partial_tail_excluded": partial})
        all_lines.extend(lines)
    boots = [i for i, line in enumerate(all_lines) if line.startswith("GARDEN Ti60 cropped port ")]
    readies = [i for i, line in enumerate(all_lines) if line.startswith("[READY]")]
    # A full boot is the only scope that certifies startup probes. A READY-only
    # capture remains analyzable but cannot inherit probes from an older boot.
    start = boots[-1] if boots else readies[-1] if readies else 0
    lines = all_lines[start:]
    rows = [row(line, start + i) for i, line in enumerate(lines)]
    ready = [r for r in rows if r["raw"].startswith("[READY]")]
    r_ctrl = [r for r in rows if r["raw"].startswith("[REWARD_CTRL]")]
    identified = bool(ready and "Q bomb" in ready[-1]["raw"] and "I panel-info" in ready[-1]["raw"] and r_ctrl)
    ready_line = ready[-1]["line"] if ready else None
    runtime = [r for r in rows if ready_line is not None and r["line"] > ready_line]
    live = [r for r in runtime if r["raw"].startswith("[LIVE]")]
    rewards = [r for r in runtime if r["raw"].startswith("[REWARD]")]
    inputs_state = [r for r in runtime if r["raw"].startswith("[INPUT_STATE]")]
    fatal = [r for r in rows if re.search(r"\bFAIL\b|\bTRAP\b", r["raw"])]
    issues = []
    def one(prefix):
        found = [r for r in rows if r["raw"].startswith(prefix)]
        return found[-1] if found else None
    alpha, precision = one("[ALPHA_PROBE]"), one("[PRECISION_PROBE]")
    def probe_ok(probe, cases):
        return bool(probe and probe.get("rc") == 0 and probe.get("cases") == cases and probe.get("commands") == cases and all(probe.get(k) == 0 for k in ("pixel_errors", "source_errors", "guard_errors")) and isinstance(probe.get("key_pixels"), int) and probe["key_pixels"] > 0)
    atlases = [r for r in rows if r["raw"].startswith(("[ATLAS]", "[ATLAS_POST_PROBE]"))]
    crc_ok = len(atlases) == 2 and all(r.get("crc32") == r.get("expected") == 0x6486F38E and r.get("bytes") == 631954 for r in atlases)
    gpu = [r for r in rows if r["raw"].startswith("GPU_ID=")]
    startup = {"alpha35": probe_ok(alpha, 35), "precision40": probe_ok(precision, 40),
        "atlas_crc_before_and_after": crc_ok,
        "color_key": any(r["raw"].startswith("[COLOR_KEY]") and "verified PASS" in r["raw"] for r in rows),
        "gpu_identity": bool(gpu and gpu[-1].get("GPU_ID") == gpu[-1].get("expected") == 0x47505531),
        "current_R_identified": identified, "full_boot_captured": bool(boots)}
    for label, observed, valid in (("Alpha", alpha, startup["alpha35"]),
            ("precision", precision, startup["precision40"]),
            ("atlas CRC", atlases, startup["atlas_crc_before_and_after"]),
            ("GPU identity", gpu, startup["gpu_identity"])):
        if observed and not valid:
            issues.append({"error": "observed startup check invalid", "check": label})
    for r in live:
        missing = [k for k in FAULTS + ("fps10", "device_ms", "frame") if not isinstance(r.get(k), int)]
        if missing:
            issues.append({"line": r["line"], "error": "malformed LIVE", "fields": missing})
    fault_records = {k: [{"line": r["line"], "value": r[k]} for r in live if isinstance(r.get(k), int) and r[k] != 0] for k in FAULTS}
    maxima = {k: max((r[k] for r in live if isinstance(r.get(k), int)), default=None) for k in FAULTS}
    live_reversals = []
    for a, b in zip(live, live[1:]):
        for key in FAULTS + ("device_ms", "frame"):
            if isinstance(a.get(key), int) and isinstance(b.get(key), int) and b[key] < a[key]:
                live_reversals.append({"line": b["line"], "field": key, "before": a[key], "after": b[key]})
    reward_issues, restarts, victories = [], [], []
    previous = None
    for r in rewards:
        missing = [k for k in REWARD if not isinstance(r.get(k), int) or r[k] < 0]
        if missing:
            reward_issues.append({"line": r["line"], "error": "malformed REWARD", "fields": missing})
            continue
        if r["bombs"] + r["used"] != 2 or r["cores"] > r["spawned"] or r["best"] < r["score"] or r["mode"] > 3 or r["phase"] > 2:
            reward_issues.append({"line": r["line"], "error": "reward bounds/conservation violated"})
        if r["score"] < min(0xFFFFFFFF, r["graze"] * 5 + r["cores"] * 25):
            reward_issues.append({"line": r["line"], "error": "score below real pickup/graze minimum"})
        if r["mode"] == 2 and (previous is None or previous["mode"] != 2):
            victories.append(r)
        key_restart = bool(previous and any(k.get("key") in ("r", "R") and previous["line"] < k["line"] <= r["line"] for k in inputs_state))
        restart = bool(previous and r["tick"] < previous["tick"] and r["mode"] == 0 and (previous["mode"] in (2, 3) or key_restart))
        if restart:
            retained = r["best"] >= max(previous["best"], previous["score"]) and r["bombs"] == 2 and r["used"] == 0
            restarts.append({"line": r["line"], "previous_tick": previous["tick"], "new_tick": r["tick"], "previous_best": previous["best"], "best": r["best"], "previous_score": previous["score"], "retained_best_and_replenished": retained})
            if not retained:
                reward_issues.append({"line": r["line"], "error": "restart failed retained-best/replenishment"})
        elif previous:
            for key in ("tick", "used", "cleared", "graze", "cores", "spawned", "lost", "score", "best", "run_ticks", "hits"):
                if r[key] < previous[key]:
                    reward_issues.append({"line": r["line"], "field": key, "before": previous[key], "after": r[key], "error": "counter decreased without observed terminal->WAVES restart; wrap/reset not assumed zero"})
        previous = r
    valid_rewards = [r for r in rewards if all(isinstance(r.get(k), int) for k in REWARD)]
    seen = {k: max((r[k] for r in valid_rewards), default=None) for k in ("used", "cleared", "graze", "cores", "spawned", "lost", "score", "best", "hits")}
    keys = {str(r.get("key")) for r in inputs_state}
    missing_keys = [key for key in require_keys if key not in keys]
    if missing_keys:
        issues.append({"error": "required UART input effect not observed", "keys": missing_keys})
    display = display_review("\n".join(lines) + "\n") if lines else {"status": "PENDING", "reason": "no UART"}
    functions_failed = bool(fatal or issues or reward_issues or live_reversals or any(fault_records.values()) or display["status"] == "FAIL")
    functional = "FAIL" if functions_failed else "PASS" if identified and all(startup.values()) and live and rewards and display["status"] == "PASS" else "PENDING"
    fps = [r["fps10"] / 10 for r in live if r.get("backend") == "GPU" and isinstance(r.get("fps10"), int)]
    strict = "PASS" if identified and fps and min(fps) >= 60.0 else "FAIL" if identified and fps else "PENDING"
    mechanics = "FAIL" if reward_issues else "PASS" if identified and all((seen.get(k) or 0) > 0 for k in ("used", "cleared", "graze", "cores")) and victories and restarts and all(r["retained_best_and_replenished"] for r in restarts) else "PENDING"
    return {"schema": 1, "evidence_kind": "SYNTHETIC_PARSER_TEST" if synthetic else "UART_CAPTURE_AUDIT", "inputs": inputs,
        "scope": {"selected_boot_line": start + 1 if boots else None, "selected_READY_line": ready_line, "prior_lines_excluded": start, "partial_tails_excluded": partial_tails, "R_identified": identified},
        "functional_status": functional, "reward_closed_loop_status": mechanics,
        "strict_60fps": {"status": strict, "threshold_fps": 60.0, "threshold_fps10": 600, "observed_GPU_interval_fps_min": min(fps) if fps else None, "observed_GPU_interval_fps_max": max(fps) if fps else None, "samples": len(fps), "CPU_rows_excluded": sum(r.get("backend") == "CPU" for r in live), "scope": "GPU LIVE interval averages only; does not prove every-frame deadline or latency", "is_600_second_acceptance": False},
        "startup": startup, "startup_records": {"alpha": alpha, "precision": precision, "atlas": atlases},
        "live": {"samples": len(live), "last_device_ms": max((r.get("device_ms", 0) for r in live), default=None), "counter_maxima": maxima, "nonzero_faults": fault_records, "counter_reversals": live_reversals, "malformed_records": issues},
        "rewards": {"samples": len(rewards), "maxima": seen, "last": rewards[-1] if rewards else None, "victories": victories, "terminal_restarts": restarts, "issues": reward_issues},
        "keys": {"required": list(require_keys), "observed": sorted(keys), "missing": missing_keys, "records": inputs_state, "scope": "INPUT_STATE confirms reported key/effect fields, not physical-button wiring"},
        "display": display, "fatal_records": fatal,
        "visual_acceptance": "PENDING_USER_OBSERVATION", "new_600_second_test": False,
        "limits": ["No UART or missing startup/current-R markers stays PENDING, never fabricated PASS.", "Strict FPS and function/score/display results are independent.", "Counter decreases are reported, never coerced to zero or silently treated as overflow.", "Victory/restart conclusions require observed terminal and new-run reward snapshots; sparse logs can leave closure pending.", "No output proves collision fairness, graphics appearance, or every-frame60FPS."]}


def self_test():
    out = VARIANT / "build/host/summarize_reward"
    out.mkdir(parents=True, exist_ok=True)
    checks = 0
    def check(condition):
        nonlocal checks
        checks += 1
        if not condition:
            raise AssertionError(f"SYNTHETIC parser check {checks} failed")
    check(analyze([])["functional_status"] == "PENDING")
    startup = """GARDEN Ti60 cropped port SYNTHETIC\nGPU_ID=0x47505531 expected=0x47505531\n[ATLAS] bytes=631954 crc32=0x6486f38e expected=0x6486f38e\n[ALPHA_PROBE] rc=0 present=1 cases=35 key_pixels=889 pixel_errors=0 source_errors=0 guard_errors=0\n[PRECISION_PROBE] rc=0 cases=40 key_pixels=1058 pixel_errors=0 source_errors=0 guard_errors=0\n[ATLAS_POST_PROBE] bytes=631954 crc32=0x6486f38e expected=0x6486f38e\n[COLOR_KEY] source pixels and transparent holes verified PASS\n[READY] Q bomb I panel-info\n[REWARD_CTRL] bombs=2 bullet_budget=80\n"""
    live = "[LIVE] secs=2 frame=120 fps10=601 backend=GPU errors=0 swap_timeout=0 front_reject=0 atlas_bounds=0 missed=0 device_ms=2000 log_dropped=0 log_truncated=0\n"
    startup = startup.replace("cases=35 key_pixels", "cases=35 commands=35 key_pixels").replace("cases=40 key_pixels", "cases=40 commands=40 key_pixels")
    reward = "[REWARD] tick=3900 mode=2 phase=2 bombs=1 used=1 cleared=64 graze=3 cores=4 spawned=5 lost=0 score=2000 best=2000 run_ticks=3894 hits=1\n"
    restart = "[REWARD] tick=20 mode=0 phase=0 bombs=2 used=0 cleared=0 graze=0 cores=0 spawned=0 lost=0 score=0 best=2000 run_ticks=20 hits=0\n"
    def display_pair(rid, saturated=False):
        n = 65535 if saturated else 7
        counts = f"0/0/{n}/{n}/0"
        sat = "0/0/1/1/0" if saturated else "0/0/0/0/0"
        total = f"0/0/{rid*n}/{rid*n}/0"
        total_sat = f"0/0/{rid}/{rid}/0" if saturated else "0/0/0/0/0"
        phase = "GAME_START" if rid == 1 else "SECOND"
        start, end = (0, 10) if rid == 1 else (20, 30)
        return (f"[DISPLAY7] phase={phase} present=1 rid={rid} frame={rid} tick={rid} mode=0 backend=GPU alpha=1 panel=16 ctrl=4 q=1024 qor=1024 sm={12 if saturated else 0} ov=0 n=1 gb=10 span=10 rawstall={(n<<16)|n} t={start}..{end} sum={counts} sat={sat} max={counts} last={counts}\n"
            f"[DISPLAY7_AUX] rid={rid} total={total} total_sat={total_sat} tov=0 event=0/0/0/0/0 et={start}..{end} game_start=11 game_elapsed={max(0,end-11)} refused=0 ev=0 ep=0 snapshot_not_runtime=1 pred_not_D=1 clear_all_written=0\n")
    with tempfile.TemporaryDirectory(prefix="SYNTHETIC_", dir=out) as temporary:
        path = Path(temporary) / "SYNTHETIC.log"
        def sample(text):
            path.write_text(text, encoding="utf-8")
            return analyze([path], synthetic=True)
        report = sample(startup + live + reward + restart)
        check(report["reward_closed_loop_status"] == "PASS")
        check(report["functional_status"] == "PENDING") # Native display absent.
        check(report["strict_60fps"]["status"] == "PASS")
        check(report["strict_60fps"]["is_600_second_acceptance"] is False)
        check(sample(startup + live.replace("fps10=601", "fps10=321") + reward)["strict_60fps"]["status"] == "FAIL")
        check(sample(startup + live.replace("errors=0", "errors=1") + reward)["functional_status"] == "FAIL")
        check(sample(startup + live.replace("log_dropped=0", "log_dropped=1") + reward)["functional_status"] == "FAIL")
        check(sample(startup + live + reward + restart.replace("best=2000", "best=100"))["reward_closed_loop_status"] == "FAIL")
        check(sample(startup + live + reward + reward.replace("tick=3900", "tick=3901").replace("cleared=64", "cleared=1"))["rewards"]["issues"])
        check(sample(startup.replace("crc32=0x6486f38e", "crc32=0x0") + live)["startup"]["atlas_crc_before_and_after"] is False)
        check(sample(startup + live + "GARDEN Ti60 cropped port NEW_BOOT\n")["startup"]["alpha35"] is False)
        check(sample(startup + live + "[LIVE] errors=999")["live"]["samples"] == 1)
        check(sample(startup + live + "unfinished")["scope"]["partial_tails_excluded"])
        check(sample(startup.replace("Q bomb", "OLD") + live)["strict_60fps"]["status"] == "PENDING")
        check(sample(startup + live.replace("fps10=601", "fps10=590"))["strict_60fps"]["status"] == "FAIL")
        pair_text = display_pair(1) + display_pair(2)
        report = sample(startup + pair_text + live + reward + restart)
        check(report["functional_status"] == "PASS")
        check(report["display"]["runtime_px_part_full"] == [0, 0, 0])
        check(report["display"]["runtime_window_counts"][2:] == [7, 7, 0])
        sat_report = sample(startup + display_pair(1, True) + display_pair(2, True) + live + reward)
        check(sat_report["display"]["status"] == "PASS")
        check(sat_report["display"]["lower_bound_fields"] == ["AR", "R"])
        check(sample(startup + display_pair(1) + display_pair(2).replace("total=0/0/14/14/0", "total=0/0/1/1/0") + live + reward)["display"]["status"] == "FAIL")
        check(sample(startup + display_pair(1) + display_pair(2).splitlines()[0] + "\n" + live + reward)["display"]["status"] == "PENDING")
    result = {"status": "PASS", "kind": "SYNTHETIC_PARSER_TEST_ONLY", "checks": checks, "board_evidence": False, "source_sha256": hashlib.sha256(Path(__file__).read_bytes()).hexdigest().upper()}
    (out / "verification.json").write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--log", type=Path, action="append", default=[], help="UART segments in chronological order; newest full boot selected")
    parser.add_argument("--require-key", action="append", default=[], help="Optional INPUT_STATE key that must actually be observed")
    parser.add_argument("--output", type=Path, help="New JSON file; refuses overwrite. Omit for stdout only")
    parser.add_argument("--require-strict-60", action="store_true", help="Nonzero exit unless GPU LIVE interval FPS status is PASS at fps10>=600; still no per-frame/600s claim")
    parser.add_argument("--self-test", action="store_true", help="SYNTHETIC parser tests only; never board acceptance")
    args = parser.parse_args()
    result = self_test() if args.self_test else analyze(args.log, args.require_key)
    text = json.dumps(result, ensure_ascii=False, indent=2) + "\n"
    if args.output:
        with args.output.open("x", encoding="utf-8") as stream:
            stream.write(text)
    print(text, end="")
    failed = result.get("status") == "FAIL" or result.get("functional_status") == "FAIL" or result.get("reward_closed_loop_status") == "FAIL"
    if args.require_strict_60 and not args.self_test:
        failed = failed or result["strict_60fps"]["status"] != "PASS"
    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
