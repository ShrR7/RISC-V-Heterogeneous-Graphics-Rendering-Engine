#!/usr/bin/env python3
"""Coherent-volley UART audit layered on every existing variety guard."""
import argparse
import importlib.util
import json
from pathlib import Path
import re


def module(name, filename):
    spec = importlib.util.spec_from_file_location(name, Path(__file__).with_name(filename))
    result = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(result)
    return result


def field_count(row, field):
    return len(re.findall(r"\b" + re.escape(field) + r"=", row["raw"]))


def audit_coherent(rows):
    """Rows must already select the most recent boot, like inherited audits."""
    controls = [r for r in rows if r["raw"].startswith("[VOLLEY_CTRL]")]
    patterns = [r for r in rows if r["raw"].startswith("[PATTERN]")]
    seeds = [r for r in rows if r["raw"].startswith("[RUN_SEED]")]
    expected = {"enemy_one_style": 1, "boss_train_one_style": 1, "style_scope": "volley"}
    issues = []
    if len(controls) > 1:
        issues.append({"error": "duplicate volley startup controls in selected boot"})
    for row in controls:
        if any(row.get(key) != value or field_count(row, key) != 1
               for key, value in expected.items()):
            issues.append({"line": row["line"], "error": "volley control protocol mismatch"})
        if any(row["line"] >= item["line"] for item in seeds + patterns):
            issues.append({"line": row["line"], "error": "volley control appears after gameplay records"})

    trains = {}
    valid = []
    for row in patterns:
        colour = row.get("colour")
        if type(colour) is not int or not 1 <= colour <= 5 or field_count(row, "colour") != 1:
            issues.append({"line": row["line"], "error": "missing, repeated or invalid pattern colour"})
            continue
        if any(type(row.get(key)) is not int or row[key] <= 0 for key in ("round", "train")):
            issues.append({"line": row["line"], "error": "invalid pattern train identity"})
            continue
        key = (row["round"], row["train"])
        prior = trains.get(key)
        if prior is not None and prior["colour"] != colour:
            issues.append({"line": row["line"], "round": key[0], "train": key[1],
                           "first_line": prior["line"], "first_colour": prior["colour"],
                           "observed_colour": colour,
                           "error": "same announced train changed colour on capacity re-charge"})
        trains.setdefault(key, row)
        valid.append(row)

    checks = {"one_valid_volley_startup_control": len(controls) == 1 and not any(
                  row.get(key) != value or field_count(row, key) != 1
                  for row in controls for key, value in expected.items()),
              "actual_pattern_colours_observed": bool(valid),
              "every_pattern_has_valid_colour": bool(patterns) and len(valid) == len(patterns)}
    return {"status": "FAIL" if issues else "PASS" if all(checks.values()) else "PENDING",
            "checks": checks, "controls": controls, "issues": issues,
            "pattern_records_observed": len(patterns), "valid_pattern_records": len(valid),
            "unique_trains": len(trains),
            "train_colours": [{"round": run, "train": train, "colour": row["colour"],
                               "first_line": row["line"]}
                              for (run, train), row in sorted(trains.items())],
            "limits": "UART proves only consistency of announced train colours across observed charge windows. It does not enumerate every emitted projectile or prove enemy volley styles; independent core emission tests and visual observation cover those claims. Different trains may legitimately repeat a colour. Existing random-schedule guards still require complete two-round evidence for overall PASS."}


def analyze(path):
    base = module("coherent_variety", "summarize_variety.py").analyze(path)
    records = module("coherent_records", "summarize_hazard.py")
    rows, partial, boots = records.parse(path.read_text(encoding="utf-8-sig", errors="replace"), True)
    coherent = audit_coherent(rows)
    status = ("FAIL" if "FAIL" in (base["status"], coherent["status"]) else
              "PASS" if base["status"] == coherent["status"] == "PASS" else "PENDING")
    return {**base, "status": status, "inherited_variety_status": base["status"],
            "evidence_kind": "COHERENT_VOLLEYS_RANDOM_SCHEDULE_FRESH_CAPTURE",
            "coherent_volleys": coherent,
            "coherent_selected_boot_line": boots[-1]+1 if boots else None,
            "coherent_partial_tail_excluded": partial}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--log", type=Path, required=True)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--require-strict-60", action="store_true")
    args = parser.parse_args()
    result = analyze(args.log)
    text = json.dumps(result, ensure_ascii=False, indent=2) + "\n"
    if args.output:
        with args.output.open("x", encoding="utf-8") as stream:
            stream.write(text)
    print(text, end="")
    return 0 if result["status"] == "PASS" and (not args.require_strict_60 or
               result["strict_60fps"]["status"] == "PASS") else 1


if __name__ == "__main__":
    raise SystemExit(main())
