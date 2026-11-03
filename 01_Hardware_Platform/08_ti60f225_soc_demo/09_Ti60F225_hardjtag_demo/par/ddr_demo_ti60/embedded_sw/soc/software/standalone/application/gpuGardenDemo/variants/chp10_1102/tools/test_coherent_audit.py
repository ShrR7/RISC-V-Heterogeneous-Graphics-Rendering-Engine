#!/usr/bin/env python3
"""Synthetic coherent-volley protocol tests; not board acceptance evidence."""
import copy
import hashlib
import importlib.util
import json
from pathlib import Path
import re
from types import SimpleNamespace
import unittest
from unittest.mock import patch

VARIANT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("coherent_audit_under_test", Path(__file__).with_name("summarize_coherent.py"))
AUDIT = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(AUDIT)
RECORDS = AUDIT.module("coherent_test_records", "summarize_hazard.py")
VARIETY = AUDIT.module("coherent_test_variety", "summarize_variety.py")
FIXTURE = AUDIT.module("coherent_test_prior_fixture", "test_variety_audit.py")
BOOT = "GARDEN Ti60 cropped port coherent synthetic fixture"
CTRL = "[VOLLEY_CTRL] enemy_one_style=1 boss_train_one_style=1 style_scope=volley"


def valid_lines():
    values = FIXTURE.valid_lines()
    colours = iter((1, 2, 3, 5, 1, 1))
    return [BOOT, CTRL] + [row + f" colour={next(colours)}" if row.startswith("[PATTERN]") else row
                          for row in values]


def rows(lines, terminated=True):
    text = "\n".join(lines) + ("\n" if terminated else "")
    return RECORDS.parse(text, True)[0]


def audit(lines):
    return AUDIT.audit_coherent(rows(lines))


def mutate_first(lines, prefix, before, after):
    result = list(lines)
    pos = next(i for i, line in enumerate(result) if line.startswith(prefix))
    result[pos] = result[pos].replace(before, after)
    return result


def aggregate(lines, inherited="PASS", strict="PASS", terminated=True):
    """Real public wrapper and parser; only previous layered result is isolated."""
    fake = VARIANT / "build/host/coherent_audit/synthetic_only.log"
    text = "\n".join(lines) + ("\n" if terminated else "")
    original_module, original_read = AUDIT.module, Path.read_text
    base = {"status": inherited, "strict_60fps": {"status": strict},
            "random_schedule": {"status": inherited, "sentinel": ["keep all old guards"]},
            "ordnance": {"display": {"status": inherited}, "live": {"counter_maxima": {"errors": 0}}},
            "checks": {"inherited_sentinel": True}, "visual_acceptance": "PENDING_USER_OBSERVATION"}
    def read(path, *args, **kwargs):
        return text if path == fake else original_read(path, *args, **kwargs)
    def module(name, filename):
        if filename == "summarize_variety.py":
            return SimpleNamespace(analyze=lambda path: copy.deepcopy(base))
        return original_module(name, filename)
    with patch.object(AUDIT, "module", module), patch.object(Path, "read_text", read):
        return AUDIT.analyze(fake), base


class CoherentAuditTests(unittest.TestCase):
    def test_valid_two_round_train_colours(self):
        result = audit(valid_lines())
        self.assertEqual(result["status"], "PASS")
        self.assertEqual(result["unique_trains"], 6)
        self.assertEqual([r["colour"] for r in result["train_colours"]], [1,2,3,5,1,1])
        self.assertEqual(VARIETY.audit_random(rows(valid_lines()))["status"], "PASS")

    def test_all_five_colours_valid(self):
        for colour in range(1, 6):
            with self.subTest(colour=colour):
                data = mutate_first(valid_lines(), "[PATTERN]", "colour=1", f"colour={colour}")
                self.assertEqual(audit(data)["status"], "PASS")

    def test_colour_missing_fails(self):
        data = mutate_first(valid_lines(), "[PATTERN]", " colour=1", "")
        self.assertEqual(audit(data)["status"], "FAIL")

    def test_colour_malformed_and_out_of_range_fail(self):
        for value in ("0", "6", "-1", "1.0", "red", "1garbage", "", "4294967295"):
            with self.subTest(value=value):
                data = mutate_first(valid_lines(), "[PATTERN]", "colour=1", f"colour={value}")
                self.assertEqual(audit(data)["status"], "FAIL")

    def test_duplicate_colour_does_not_hide_bad_first_value(self):
        for extra in (" colour=1", " colour=0", " colour=2"):
            data = mutate_first(valid_lines(), "[PATTERN]", "colour=1", "colour=1" + extra)
            self.assertEqual(audit(data)["status"], "FAIL")

    def test_valid_capacity_wait_recharge_keeps_colour(self):
        data = valid_lines()
        i = next(i for i, x in enumerate(data) if x.startswith("[PATTERN]"))
        extra = FIXTURE.pattern(1, FIXTURE.SEED1, 1, 1, window=2, tick=1300) + " colour=1"
        data.insert(i+1, extra)
        data[i+2] = data[i+2].replace("windows=2", "windows=3")
        data[i+3] = data[i+3].replace("windows=3", "windows=4")
        self.assertEqual(audit(data)["status"], "PASS")
        self.assertEqual(VARIETY.audit_random(rows(data))["status"], "PASS")

    def test_same_train_colour_change_fails(self):
        data = valid_lines()
        i = next(i for i, x in enumerate(data) if x.startswith("[PATTERN]"))
        data.insert(i+1, FIXTURE.pattern(1, FIXTURE.SEED1, 1, 1, window=2, tick=1300) + " colour=2")
        result = audit(data)
        self.assertEqual(result["status"], "FAIL")
        self.assertEqual(result["issues"][0]["first_colour"], 1)
        self.assertEqual(result["issues"][0]["observed_colour"], 2)

    def test_restart_can_reuse_train_number_with_new_colour(self):
        result = audit(valid_lines())
        first = [r for r in result["train_colours"] if r["train"] == 1]
        self.assertEqual([(r["round"], r["colour"]) for r in first], [(1,1),(2,5)])
        self.assertEqual(result["status"], "PASS")

    def test_next_train_can_change_or_repeat_colour(self):
        data = [re.sub(r"colour=\d+", "colour=3", line) for line in valid_lines()]
        self.assertEqual(audit(data)["status"], "PASS")
        self.assertEqual(audit(valid_lines())["status"], "PASS")

    def test_invalid_pattern_identity_fails(self):
        for field, value in (("round", "0"), ("train", "-1"), ("train", "bad")):
            data = mutate_first(valid_lines(), "[PATTERN]", f"{field}=1", f"{field}={value}")
            self.assertEqual(audit(data)["status"], "FAIL")

    def test_empty_missing_control_or_pattern_stays_pending(self):
        for data in ([], [BOOT, CTRL], [x for x in valid_lines() if x != CTRL]):
            self.assertEqual(audit(data)["status"], "PENDING")

    def test_bad_control_protocol_fails(self):
        for before, after in (("enemy_one_style=1", "enemy_one_style=0"),
                              ("boss_train_one_style=1", "boss_train_one_style=0"),
                              ("style_scope=volley", "style_scope=projectile"),
                              ("style_scope=volley", "")):
            data = [line.replace(before, after) if line == CTRL else line for line in valid_lines()]
            self.assertEqual(audit(data)["status"], "FAIL")

    def test_duplicate_control_and_duplicate_control_fields_fail(self):
        data = valid_lines(); data.insert(2, CTRL)
        self.assertEqual(audit(data)["status"], "FAIL")
        data = valid_lines(); data[1] += " enemy_one_style=1"
        self.assertEqual(audit(data)["status"], "FAIL")

    def test_late_control_fails(self):
        data = [line for line in valid_lines() if line != CTRL] + [CTRL]
        self.assertEqual(audit(data)["status"], "FAIL")

    def test_previous_boot_corruption_does_not_poison_new_boot(self):
        stale = [BOOT, CTRL.replace("enemy_one_style=1", "enemy_one_style=0"),
                 FIXTURE.pattern(1, FIXTURE.SEED1, 1, 1) + " colour=0"]
        self.assertEqual(audit(stale + valid_lines())["status"], "PASS")

    def test_old_queued_startup_does_not_satisfy_new_boot(self):
        stale = valid_lines()
        fresh_without_control = [line for line in valid_lines() if line != CTRL]
        self.assertEqual(audit(stale + fresh_without_control)["status"], "PENDING")


class AggregateTests(unittest.TestCase):
    def test_public_wrapper_keeps_every_inherited_section(self):
        result, base = aggregate(valid_lines())
        self.assertEqual(result["status"], "PASS")
        for key, value in base.items():
            self.assertEqual(result[key], value)
        self.assertEqual(result["inherited_variety_status"], "PASS")

    def test_inherited_fail_or_pending_never_promoted(self):
        for status in ("FAIL", "PENDING"):
            result, base = aggregate(valid_lines(), inherited=status)
            self.assertEqual(result["status"], status)
            self.assertEqual(result["ordnance"], base["ordnance"])
            self.assertEqual(result["random_schedule"], base["random_schedule"])

    def test_new_protocol_failure_overrides_base_pass_or_pending(self):
        data = mutate_first(valid_lines(), "[PATTERN]", "colour=1", "colour=6")
        for inherited in ("PASS", "PENDING", "FAIL"):
            self.assertEqual(aggregate(data, inherited=inherited)[0]["status"], "FAIL")

    def test_new_missing_control_cannot_be_promoted_by_base_pass(self):
        result, _ = aggregate([line for line in valid_lines() if line != CTRL])
        self.assertEqual(result["status"], "PENDING")

    def test_strict60_failure_and_visual_pending_preserved(self):
        result, _ = aggregate(valid_lines(), strict="FAIL")
        self.assertEqual(result["strict_60fps"]["status"], "FAIL")
        self.assertEqual(result["visual_acceptance"], "PENDING_USER_OBSERVATION")

    def test_last_boot_selected_in_public_analyze(self):
        prefix = [BOOT, CTRL.replace("style_scope=volley", "style_scope=bad")]
        result, _ = aggregate(prefix + valid_lines())
        self.assertEqual(result["status"], "PASS")
        self.assertEqual(result["coherent_selected_boot_line"], 3)
        self.assertEqual(len(result["coherent_volleys"]["controls"]), 1)

    def test_partial_tail_excluded_explicitly(self):
        result, _ = aggregate(valid_lines()+["[PATTERN] colour=broken"], terminated=False)
        self.assertEqual(result["status"], "PASS")
        self.assertTrue(result["coherent_partial_tail_excluded"])
        self.assertEqual(result["coherent_volleys"]["pattern_records_observed"], 6)

    def test_fully_received_malformed_tail_is_failure(self):
        result, _ = aggregate(valid_lines()+["[PATTERN] colour=broken"])
        self.assertEqual(result["status"], "FAIL")


if __name__ == "__main__":
    suite = unittest.defaultTestLoader.loadTestsFromModule(__import__(__name__))
    result = unittest.TextTestRunner(verbosity=2).run(suite)
    report = {"status": "PASS" if result.wasSuccessful() else "FAIL", "host_only": True,
              "synthetic_protocol_evidence_not_board_acceptance": True,
              "tests_run": result.testsRun, "failures": len(result.failures), "errors": len(result.errors),
              "auditor_sha256": hashlib.sha256(Path(__file__).with_name("summarize_coherent.py").read_bytes()).hexdigest().upper(),
              "tests_sha256": hashlib.sha256(Path(__file__).read_bytes()).hexdigest().upper(),
              "guard_scope": "Real public wrapper and record parser; inherited result isolated to ensure FAIL/PENDING cannot be promoted. Existing random schedule validator exercised on two valid rounds and a WAIT recharge. Inherited validators unchanged.",
              "limits": "Synthetic tests check parser rejection and status composition only. UART colour announcements do not prove every emitted bullet; direct core tests and board observations remain separate.",
              "runtime_modified": False, "hardware_access": False}
    out = VARIANT / "build/host/coherent_audit/verification.json"
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(json.dumps(report, ensure_ascii=False, indent=2)+"\n", encoding="utf-8")
    print(json.dumps(report, ensure_ascii=False))
    raise SystemExit(0 if result.wasSuccessful() else 1)
