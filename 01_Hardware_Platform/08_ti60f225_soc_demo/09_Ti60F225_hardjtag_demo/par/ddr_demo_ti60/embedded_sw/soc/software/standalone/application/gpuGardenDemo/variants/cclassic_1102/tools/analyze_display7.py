#!/usr/bin/env python3
"""Read-only DISPLAY7 pair/coverage review; never decide HDMI acceptance."""
import argparse
import hashlib
import json
from pathlib import Path
import re

FIELDS = re.compile(r"\b([A-Za-z_][A-Za-z_0-9]*)=([^\s;]+)")
NAMES = ("PX", "PART", "AR", "R", "FULL")
HZ = 100000000
U64 = (1 << 64) - 1


def value(token):
    if ".." in token:
        return [value(x) for x in token.split("..")]
    if "/" in token:
        return [value(x) for x in token.split("/")]
    try:
        return int(token, 16 if token.lower().startswith("0x") else 10)
    except ValueError:
        return token


def ints(v, length, maximum=U64):
    return isinstance(v, list) and len(v) == length and all(isinstance(x, int) and 0 <= x <= maximum for x in v)


def analyze(paths):
    if isinstance(paths, Path):
        paths = [paths]
    inputs, lines, origins = [], [], []
    for path in paths:
        raw = path.read_bytes()
        text = raw.decode("utf-8-sig", errors="replace")
        partial = bool(text and not text.endswith(("\n", "\r")))
        part = text.splitlines()
        if partial:
            part = part[:-1]
        inputs.append({"path": str(path.resolve()), "bytes": len(raw), "sha256": hashlib.sha256(raw).hexdigest(),
                       "partial_tail_excluded": partial, "virtual_first_line": len(lines) + 1})
        origins.extend((str(path.resolve()), i + 1) for i in range(len(part)))
        lines.extend(part)
    boots = [i for i, line in enumerate(lines) if line.startswith("GARDEN Ti60 cropped port ")]
    start = boots[-1] if boots else 0
    rows = [{"line": i + 1, "source": origins[i][0], "source_line": origins[i][1], "raw": line,
             **{k: value(v) for k, v in FIELDS.findall(line)}}
            for i, line in enumerate(lines) if i >= start]
    mains, auxes, issues, duplicates = {}, {}, [], []
    absent = [r for r in rows if r["raw"].startswith("[DISPLAY7]") and r.get("present") == 0]
    for row in rows:
        target = mains if row["raw"].startswith("[DISPLAY7]") else auxes if row["raw"].startswith("[DISPLAY7_AUX]") else None
        if target is None or row.get("present") == 0:
            continue
        rid = row.get("rid")
        if not isinstance(rid, int):
            issues.append({"line": row["line"], "error": "missing/integer rid"})
            continue
        if rid in target:
            duplicates.append({"rid": rid, "line": row["line"], "identical": target[rid]["raw"] == row["raw"]})
            if target[rid]["raw"] != row["raw"]:
                issues.append({"line": row["line"], "error": "conflicting duplicate rid"})
            continue
        target[rid] = row
    complete = sorted(mains.keys() & auxes.keys(), key=lambda rid: mains[rid]["line"])
    pairs, previous, gaps, event_records, states = [], None, [], [], []
    windows = [0] * 5
    runtime = [0] * 5
    runtime_sat = [0] * 5
    game_anchor = None
    start_pair = None
    boundary_verified = False
    for rid in complete:
        main, aux = mains[rid], auxes[rid]
        local = []
        for owner, fields, maximum in ((main, ("sum",), U64), (main, ("sat",), (1 << 32) - 1),
                                       (main, ("last", "max"), 65535), (aux, ("total",), U64),
                                       (aux, ("total_sat",), (1 << 32) - 1), (aux, ("event",), 65535)):
            for key in fields:
                if not ints(owner.get(key), 5, maximum):
                    local.append("malformed " + key)
        for owner, key in ((main, "t"), (aux, "et")):
            if not ints(owner.get(key), 2) or owner[key][1] < owner[key][0]:
                local.append("malformed 64-bit " + key)
        for owner, keys in ((main, ("n", "gb", "span", "rawstall", "ctrl", "q", "qor", "sm", "ov")),
                            (aux, ("tov", "game_start", "game_elapsed", "refused", "ev", "ep"))):
            for key in keys:
                if not isinstance(owner.get(key), int) or owner[key] < 0:
                    local.append("missing/integer " + key)
        if local:
            issues.append({"rid": rid, "line": main["line"], "errors": local})
            continue
        if main["rawstall"] != (main["last"][3] << 16) | main["last"][2]:
            local.append("packed last STALL mismatch")
        if main["sm"] != sum(1 << i for i, n in enumerate(main["sat"]) if n):
            local.append("saturation mask mismatch")
        if any((n == 0 and peak == 65535) or (n > 0 and peak != 65535) for n, peak in zip(main["sat"], main["max"])):
            local.append("saturation incidence/maximum mismatch")
        if any(last > peak or peak > summed for last, peak, summed in zip(main["last"], main["max"], main["sum"])):
            local.append("last/maximum/sum inconsistent")
        if main["ctrl"] not in (0, 4) or ((main["q"] >> 10) & 1) != (main["ctrl"] == 4):
            local.append("CTRL / sampled STATUS2 enable mismatch")
        if aux.get("snapshot_not_runtime") != 1 or aux.get("pred_not_D") != 1 or aux.get("clear_all_written") != 0:
            local.append("missing measurement boundary markers")
        if aux["game_start"]:
            if game_anchor is None:
                game_anchor = aux["game_start"]
            elif aux["game_start"] != game_anchor:
                local.append("GAME_START anchor changed inside one boot")
            expected = max(0, main["t"][1] - aux["game_start"])
            if aux["game_elapsed"] != expected:
                local.append("64-bit game_elapsed inconsistent")
        if previous:
            old_main, old_aux = previous
            if main["t"][1] < old_main["t"][1] or main["frame"] < old_main["frame"]:
                local.append("nonmonotonic CLINT/frame within boot")
            delta = [new - old for new, old in zip(aux["total"], old_aux["total"])]
            dsat = [new - old for new, old in zip(aux["total_sat"], old_aux["total_sat"])]
            if not (main["ov"] or aux["tov"] or old_aux["tov"]) and (delta != main["sum"] or dsat != main["sat"]):
                gaps.append({"rid": rid, "previous_rid": old_main["rid"], "total_delta": delta,
                             "window_sum": main["sum"], "sat_delta": dsat, "window_sat": main["sat"],
                             "uncovered_counts": [d - s for d, s in zip(delta, main["sum"])]})
        else:
            if aux["total"] != main["sum"] or aux["total_sat"] != main["sat"]:
                gaps.append({"rid": rid, "prior_capture_history": True, "total": aux["total"], "window_sum": main["sum"]})
        windows = [a + b for a, b in zip(windows, main["sum"])]
        after_start = start_pair is not None and main["t"][0] >= game_anchor
        if main.get("phase") == "GAME_START":
            start_pair = {"rid": rid, "main": main, "aux": aux}
            boundary_verified = True
            after_start = False
        if after_start:
            runtime = [a + b for a, b in zip(runtime, main["sum"])]
            runtime_sat = [a + b for a, b in zip(runtime_sat, main["sat"])]
        pair = {"rid": rid, "main": main, "aux": aux, "after_GAME_START_window": after_start,
                "device_seconds": aux["game_elapsed"] / HZ, "gap_bound_ms": main["gb"] / 100000,
                "sample_span_ms": main["span"] / 100000, "relock_enabled": bool(main["q"] & 0x400),
                "relock_seen_sampled": bool(main["q"] & 0x800), "relock_busy_sampled": bool(main["q"] & 0x1000)}
        if aux["ev"]:
            if aux["ep"] and not aux["event"][1]:
                local.append("PART event flag without PART")
            if aux.get("eraw") != (aux["event"][3] << 16) | aux["event"][2]:
                local.append("packed event STALL mismatch")
            if ints(aux.get("estate"), 4):
                event_records.append({"rid": rid, "ep": aux["ep"], "counts": aux["event"], "row": aux.get("erow"),
                                      "sample_t": aux["et"], "state_at_sample": aux["estate"], "after_start": after_start})
        if main.get("phase") == "STATE" or (previous and main.get("mode") != previous[0].get("mode")):
            states.append({"rid": rid, "mode": main.get("mode"), "attack": main.get("attack"), "tick": main.get("tick"), "frame": main.get("frame"),
                           "score": main.get("score"), "device_seconds": pair["device_seconds"],
                           "window": main["sum"], "ep": aux["ep"], "event": aux.get("estate")})
        if local:
            issues.append({"rid": rid, "line": main["line"], "errors": local})
        pairs.append(pair)
        previous = main, aux
    live = [r for r in rows if r["raw"].startswith("[LIVE]")]
    native_log_nonzero = [r for r in rows if any(isinstance(r.get(k), int) and r[k] for k in ("log_dropped", "log_truncated"))]
    native_faults = [r for r in rows if r["raw"].startswith("[LIVE]") and any(isinstance(r.get(k), int) and r[k] for k in
                     ("errors", "swap_timeout", "front_reject", "atlas_bounds", "missed"))]
    fatal = [r for r in rows if re.search(r"\bFAIL\b|\bTRAP\b", r["raw"])]
    results = [r for r in rows if r["raw"].startswith("RESULT: GARDEN 600S ")]
    first_result_line = results[0]["line"] if results else None
    all_running = [p for p in pairs if p["after_GAME_START_window"]]
    running = [p for p in all_running if first_result_line is None or p["main"]["line"] < first_result_line]
    post = [p for p in all_running if first_result_line is not None and p["main"]["line"] > first_result_line]
    runtime = [sum(p["main"]["sum"][i] for p in running) for i in range(5)]
    runtime_sat = [sum(p["main"]["sat"][i] for p in running) for i in range(5)]
    return {"source": {"inputs": inputs, "virtual_join_only": len(inputs) > 1,
                       "boot_line": start + 1 if boots else None, "input_order_from_command_line": True},
            "status": "FORMAT_FAIL" if issues or absent else "PAIRED" if pairs else "INCOMPLETE",
            "counter_order": NAMES, "complete_pairs": len(pairs), "incomplete_rids": sorted(mains.keys() ^ auxes.keys()),
            "duplicates": duplicates, "issues": issues, "counter_total_window_gaps": gaps,
            "window_observed_counts": windows, "runtime_window_observed_counts": runtime,
            "runtime_scope": "GAME_START through FIRST native 600S RESULT; pending captures use current end",
            "runtime_complete_pairs": len(running), "runtime_samples": sum(p["main"]["n"] for p in running),
            "runtime_window_saturation_incidence": runtime_sat,
            "runtime_counter_lower_bound_fields": [NAMES[i] for i in range(5) if runtime_sat[i] or
                any(p["main"]["ov"] & ((1 << i) | (1 << (5+i))) or p["aux"]["tov"] & ((1 << i) | (1 << (5+i))) for p in running)],
            "GAME_START_boundary_verified": boundary_verified,
            "GAME_START_pair": start_pair, "runtime_counter_duration_seconds": running[-1]["device_seconds"] if running else None,
            "runtime_all_sampled_ON": bool(running) and all(p["relock_enabled"] and p["main"]["ctrl"] == 4 for p in running),
            "runtime_all_sampled_GPU_Alpha16": bool(running) and all(p["main"].get("backend") == "GPU" and p["main"].get("alpha") == 1 and p["main"].get("panel") == 16 for p in running),
            "runtime_overflow_pairs": [p["rid"] for p in running if p["main"]["ov"] or p["aux"]["tov"]],
            "refused_max": max((p["aux"]["refused"] for p in pairs), default=None),
            "native_log_nonzero": native_log_nonzero, "native_faults": native_faults, "fatal": fatal,
            "native_cal": [r for r in rows if r["raw"].startswith("[CAL]")],
            "native_alpha": [r for r in rows if r["raw"].startswith("[ALPHA_PROBE]")],
            "native_color_key": [r for r in rows if r["raw"].startswith("[COLOR_KEY]")],
            "native_game_events": [r for r in rows if r["raw"].startswith("[GAME]")], "native_600s_results": results,
            "first_600s_result": results[0] if results else None,
            "post_result": {"pairs": len(post), "last_device_seconds": post[-1]["device_seconds"] if post else None,
                            "first_pair": post[0] if post else None,
                            "counter_observed_counts": [sum(p["main"]["sum"][i] for p in post) for i in range(5)],
                            "counter_sat_incidence": [sum(p["main"]["sat"][i] for p in post) for i in range(5)],
                            "live_records": [r for r in live if first_result_line is not None and r["line"] > first_result_line]},
            "live_samples": len(live), "states": states, "event_samples": event_records, "pairs": pairs,
            "limits": ["PAIRED certifies parsing only, never display PASS. Saturated/overflowed fields are lower bounds.",
                       "Lifecycle totals include startup/calibration/CPU and GPU benchmarks; GAME_START window excluded from runtime sum.",
                       "Sampling intervals, report modes and event estate are not exact bad-pixel times or causal frames.",
                       "0x80/0x84 are raw starvation modulo, not D; startup snapshots freeze and do not cover runtime.",
                       "Sequential APB reads are not atomic. Host receipt times are not video edges. CTRL/q only establish sampled state.",
                       "All pairs remain in the evidence list; summary runtime stops at first 600S RESULT and post-result is separate.",
                       "Last pre-result report can end before 600s; a subsequent report can straddle the boundary. Native RESULT and host timings establish duration separately."]}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("log", type=Path, nargs="+", help="Chronological UART segments; joined in memory, originals unchanged")
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    result = analyze(args.log)
    output = json.dumps(result, ensure_ascii=False, indent=2) + "\n"
    if args.output:
        args.output.write_bytes(output.encode("utf-8"))
        print(json.dumps({k: result[k] for k in ("status", "complete_pairs", "incomplete_rids", "issues", "runtime_window_observed_counts", "runtime_counter_duration_seconds")}, ensure_ascii=False))
    else:
        print(output, end="")


if __name__ == "__main__":
    main()
