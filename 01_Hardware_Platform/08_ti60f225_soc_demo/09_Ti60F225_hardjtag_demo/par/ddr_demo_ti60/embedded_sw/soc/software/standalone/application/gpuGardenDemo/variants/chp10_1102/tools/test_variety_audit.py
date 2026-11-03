#!/usr/bin/env python3
"""Independent synthetic protocol tests. No hardware or game C compilation."""
import copy
import hashlib
import importlib.util
import json
import re
import unittest
import zlib
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import patch

VARIANT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("variety_audit_under_test", Path(__file__).with_name("summarize_variety.py"))
AUDIT = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(AUDIT)
RECORDS = AUDIT.module("variety_test_records", "summarize_hazard.py")
SEED1 = 0x12345678
# Independent published xorshift32 example; do not derive the expected successor
# by calling the implementation under test.
SEED2 = 0x87985AA5
GENERATION = json.loads((VARIANT / "assets/variety_generation.json").read_text(encoding="utf-8-sig"))


def control():
    return f"[RANDOM_CTRL] enabled=1 initial_source=fixed_override seed=0x{SEED1:x} boss_shuffle=3 repeat_adjacent=0 timing_bounded=1 warning_preserved=1"


def pattern(run, seed, train, attack, window=None, **changes):
    value = dict(round=run, seed=seed, tick=1000+train*200,
                 attack=attack, state=1, band=0, charge=20, rest=80,
                 gap=6 if attack == 2 else 12,
                 remaining={1:1,2:6,3:2}[attack], windows=window or train,
                 train=train, laser=0)
    value.update(changes)
    return "[PATTERN] " + " ".join(f"{key}={number}" for key,number in value.items())


def valid_lines():
    return [control(), f"[RUN_SEED] round=1 seed=0x{SEED1:x} tick=0",
            pattern(1,SEED1,1,1),pattern(1,SEED1,2,2),pattern(1,SEED1,3,3),
            f"[RUN_SEED] round=2 seed=0x{SEED2:x} tick=0",
            pattern(2,SEED2,1,3),pattern(2,SEED2,2,1),pattern(2,SEED2,3,2)]


def rows(lines):
    return RECORDS.parse("\n".join(lines)+"\n", False)[0]


def random(lines):
    return AUDIT.audit_random(rows(lines))


def art_line(**changes):
    values = dict(materials=13,bullet_styles=9,enemy_projectiles_all_new=1,
                  sprite_count=859,atlas_bytes=GENERATION["atlas_bytes"],
                  player_bank=1,actor_hp="above",damage_numbers=1,
                  laser_left_px=-42,laser_right_px=43,laser_y_px=19)
    values.update(changes)
    return "[ART_CTRL] "+" ".join(f"{key}={value}" for key,value in values.items())


def phase_lines():
    return ["[LIVE] secs=2 render_us=1450 backend=GPU errors=0",
            "[FRAME_COST] frames=121 steps=81 logic_avg_us=10 logic_peak_us=20 build_avg_us=30 build_peak_us=40 render_avg_us=1450 present_avg_us=50 debug_avg_us=60 log_avg_us=70 work_peak_us=8000 work_over_period=0 log_cost_one_window_delayed=1"]


def aggregate(lines, inherited="PASS"):
    # Exercise the real public analyze and real record parser. Only inherited
    # validation is isolated, so aggregate behavior cannot turn FAIL/PENDING
    # inherited guards into PASS. Those guard implementations stay unchanged.
    fake = VARIANT / "build/host/variety_audit/synthetic_only.log"
    text = "\n".join(lines)+"\n"
    original_module = AUDIT.module
    original_read = Path.read_text
    def read(path, *args, **kwargs):
        return text if path == fake else original_read(path,*args,**kwargs)
    def module(name, filename):
        if filename == "summarize_sortie.py":
            return SimpleNamespace(analyze=lambda path: {
                "status": inherited, "strict_60fps":{"status":"PASS"},
                "inherited_guard_sentinel":inherited})
        return original_module(name,filename)
    with patch.object(AUDIT,"module",module),patch.object(Path,"read_text",read):
        return AUDIT.analyze(fake)


class RandomAuditTests(unittest.TestCase):
    def test_two_round_complete_bags_pass(self):
        result=random(valid_lines())
        self.assertEqual(result["status"],"PASS")
        self.assertEqual(result["unique_trains"],6)
        self.assertEqual(result["actual_attack_sequences"],{1:[1,2,3],2:[3,1,2]})

    def test_independent_seed_successor_vector(self):
        self.assertEqual(AUDIT.seed_successor(SEED1),SEED2)

    def test_seed_domain_and_successor_fail(self):
        for value in (0,-1,0x100000000,SEED1,0x12345679):
            with self.subTest(seed=value):
                data=valid_lines();data[5]=f"[RUN_SEED] round=2 seed={value} tick=0"
                self.assertEqual(random(data)["status"],"FAIL")

    def test_wrong_control_seed_and_protocol_fail(self):
        for field,value in (("seed",0),("seed",SEED2),("enabled",0),("boss_shuffle",2),
                            ("repeat_adjacent",1),("timing_bounded",0),("warning_preserved",0),
                            ("initial_source","unknown")):
            with self.subTest(field=field):
                data=valid_lines();data[0]=re.sub(rf"\b{field}=\S+",f"{field}={value}",data[0])
                self.assertEqual(random(data)["status"],"FAIL")

    def test_wrong_round_or_pattern_seed_fail(self):
        for changed in ({"round":3},{"seed":SEED2},{"tick":-1},{"laser":4},{"windows":0}):
            with self.subTest(changed=changed):
                data=valid_lines();value=rows([data[2]])[0]
                for key,number in changed.items():
                    data[2]=re.sub(rf"\b{key}=\S+",f"{key}={number}",data[2])
                self.assertEqual(random(data)["status"],"FAIL")

    def test_charge_rest_gap_band_bounds_fail(self):
        for band in range(3):
            for attack in (1,2,3):
                gap0=6-band if attack==2 else 12-2*band
                for changed in ({"charge":19-4*band},{"charge":29-4*band},
                                {"rest":79-16*band},{"rest":105-16*band},
                                {"gap":gap0-1},{"gap":gap0+(3 if attack==2 else 5)}):
                    with self.subTest(band=band,attack=attack,changed=changed):
                        data=valid_lines();fields=dict(band=band,charge=20-4*band,
                                                     rest=80-16*band,gap=gap0)
                        fields.update(changed);data[2]=pattern(1,SEED1,1,attack,**fields)
                        self.assertEqual(random(data)["status"],"FAIL")

    def test_capacity_recharge_preserves_parameters(self):
        data=valid_lines();data.insert(3,pattern(1,SEED1,1,1,window=2,tick=1300))
        data[4]=pattern(1,SEED1,2,2,window=3,tick=1500)
        data[5]=pattern(1,SEED1,3,3,window=4,tick=1700)
        self.assertEqual(random(data)["status"],"PASS")
        for field,value in (("charge",21),("rest",81),("gap",13),("attack",3),("band",1)):
            with self.subTest(field=field):
                mutated=copy.deepcopy(data)
                mutated[3]=re.sub(rf"\b{field}=\S+",f"{field}={value}",mutated[3])
                self.assertEqual(random(mutated)["status"],"FAIL")

    def test_recharge_cannot_replenish_consumed_pulses(self):
        data=valid_lines()
        data[4:4]=[pattern(1,SEED1,2,2,window=3,tick=1500,remaining=4),
                   pattern(1,SEED1,2,2,window=4,tick=1600,remaining=5)]
        data[6]=pattern(1,SEED1,3,3,window=5,tick=1800)
        self.assertEqual(random(data)["status"],"FAIL")
        data[5]=pattern(1,SEED1,2,2,window=4,tick=1600,remaining=3)
        self.assertEqual(random(data)["status"],"PASS")

    def test_legal_bounds_in_all_bands_pass(self):
        for band in range(3):
            for attack in (1,2,3):
                for high in (False,True):
                    with self.subTest(band=band,attack=attack,high=high):
                        data=valid_lines();gap0=6-band if attack==2 else 12-2*band
                        data[attack+1]=pattern(1,SEED1,attack,attack,band=band,
                            charge=20-4*band+(8 if high else 0),
                            rest=80-16*band+(24 if high else 0),
                            gap=gap0+((2 if attack==2 else 4) if high else 0))
                        self.assertEqual(random(data)["status"],"PASS")

    def test_adjacent_repeat_inside_and_across_bag_fail(self):
        for seq in ([1,1,3],[1,2,3,3,1,2],[1,2,1]):
            with self.subTest(seq=seq):
                data=valid_lines()[:2]+[pattern(1,SEED1,i,a) for i,a in enumerate(seq,1)]+valid_lines()[5:]
                self.assertEqual(random(data)["status"],"FAIL")

    def test_complete_second_bag_valid(self):
        data=valid_lines()[:5]+[pattern(1,SEED1,4,1),pattern(1,SEED1,5,3),pattern(1,SEED1,6,2)]+valid_lines()[5:]
        self.assertEqual(random(data)["status"],"PASS")

    def test_empty_one_round_partial_type_pending(self):
        for data in ([],valid_lines()[:5],valid_lines()[:4]+valid_lines()[5:7]):
            with self.subTest(lines=len(data)):
                self.assertEqual(random(data)["status"],"PENDING")

    def test_missing_train_is_pending_not_fabricated_bag(self):
        data=valid_lines();data[3]=pattern(1,SEED1,3,2,window=2)
        data[4]=pattern(1,SEED1,4,3,window=3)
        self.assertEqual(random(data)["status"],"PENDING")
        data=valid_lines();del data[3]
        data[3]=pattern(1,SEED1,3,1,window=2)
        # Same attacks at1/3 are not adjacent evidence when train2 is missing.
        self.assertEqual(random(data)["status"],"PENDING")

    def test_missing_charge_window_pending(self):
        data=valid_lines();data[3]=pattern(1,SEED1,2,2,window=3)
        data[4]=pattern(1,SEED1,3,3,window=4)
        self.assertEqual(random(data)["status"],"PENDING")

    def test_pattern_chronology_fail(self):
        for changed in ({"windows":1},{"tick":1199},{"train":1}):
            with self.subTest(changed=changed):
                data=valid_lines();data[3]=pattern(1,SEED1,2,2,**changed) if "train" not in changed else pattern(1,SEED1,1,2,window=2,tick=1400)
                self.assertEqual(random(data)["status"],"FAIL")
        data=valid_lines();data[1],data[2]=data[2],data[1]
        self.assertEqual(random(data)["status"],"FAIL")

    def test_missing_field_fail(self):
        data=valid_lines();data[2]=re.sub(r"\brest=\S+ ?","",data[2])
        self.assertEqual(random(data)["status"],"FAIL")


class AggregateAuditTests(unittest.TestCase):
    def test_valid_dynamic_art_and_phases_pass(self):
        result=aggregate([art_line()]+phase_lines()+valid_lines())
        self.assertEqual(result["status"],"PASS")
        self.assertEqual(result["art"]["phase_samples"],1)

    def test_inherited_guards_not_relaxed(self):
        for status in ("FAIL","PENDING"):
            with self.subTest(status=status):
                result=aggregate([art_line()]+phase_lines()+valid_lines(),status)
                self.assertEqual(result["status"],status)
                self.assertEqual(result["inherited_guard_sentinel"],status)

    def test_wrong_art_declarations_fail(self):
        for field,value in (("materials",5),("bullet_styles",8),("enemy_projectiles_all_new",0),
                            ("atlas_bytes",472732),("laser_left_px",-43)):
            with self.subTest(field=field):
                result=aggregate([art_line(**{field:value})]+phase_lines()+valid_lines())
                self.assertEqual(result["status"],"FAIL")

    def test_missing_art_or_phases_pending(self):
        for lines in (phase_lines()+valid_lines(),[art_line()]+valid_lines()):
            self.assertEqual(aggregate(lines)["status"],"PENDING")

    def test_bad_phase_and_pair_fail(self):
        for before,after in (("frames=121","frames=0"),("logic_avg_us=10","logic_avg_us=-1"),
                             ("render_avg_us=1450","render_avg_us=1451"),
                             ("log_cost_one_window_delayed=1","log_cost_one_window_delayed=0")):
            with self.subTest(before=before):
                lines=[art_line()]+[x.replace(before,after) for x in phase_lines()]+valid_lines()
                self.assertEqual(aggregate(lines)["status"],"FAIL")

    def test_runtime_declared_source_assets_agree(self):
        self.assertEqual(GENERATION["source_bullet_pngs"],9)
        self.assertEqual(len(GENERATION["sources"]),9)
        self.assertEqual(len({x["name"] for x in GENERATION["sources"]}),9)
        self.assertEqual(GENERATION["source_style_families"],10)
        self.assertEqual(len(GENERATION["styles"]),10)
        self.assertEqual(GENERATION["new_enemy_projectile_ids"],640)
        for source in GENERATION["sources"]:
            path=VARIANT/source["copied_path"]
            self.assertEqual(hashlib.sha256(path.read_bytes()).hexdigest(),source["original_sha256"])
        originals=list((VARIANT/"assets/source/user_20261016").glob("*.png"))
        self.assertEqual(len(originals),13)
        atlas=(VARIANT/"assets/atlas.bin").read_bytes()
        self.assertEqual(len(atlas),GENERATION["atlas_bytes"])
        self.assertEqual(hashlib.sha256(atlas).hexdigest(),GENERATION["atlas_sha256"])
        self.assertEqual(zlib.crc32(atlas),int(GENERATION["atlas_crc32"],16))
        main=(VARIANT/"src/main.c").read_text(encoding="utf-8-sig")
        self.assertIn("[ART_CTRL] materials=13 bullet_styles=9 enemy_projectiles_all_new=1",main)
        self.assertIn("(unsigned)GARDEN_ATLAS_BYTES",main)


if __name__ == "__main__":
    suite=unittest.defaultTestLoader.loadTestsFromModule(__import__(__name__))
    result=unittest.TextTestRunner(verbosity=2).run(suite)
    report={"status":"PASS" if result.wasSuccessful() else "FAIL","host_only":True,
            "synthetic_protocol_evidence_not_board_acceptance":True,
            "tests_run":result.testsRun,"failures":len(result.failures),"errors":len(result.errors),
            "auditor_sha256":hashlib.sha256(Path(__file__).with_name("summarize_variety.py").read_bytes()).hexdigest().upper(),
            "tests_sha256":hashlib.sha256(Path(__file__).read_bytes()).hexdigest().upper(),
            "guard_scope":"Real public aggregate and record parser; inherited status isolated by stub to prove FAIL/PENDING cannot be promoted; inherited validators unchanged.",
            "source_contract":"13 user PNGs =4 aircraft+9 bullet sources,10 styled bullet families; actual atlas length/CRC/SHA and copied source hashes checked.",
            "runtime_modified":False,"hardware_access":False}
    out=VARIANT/"build/host/variety_audit/verification.json"
    out.parent.mkdir(parents=True,exist_ok=True)
    out.write_text(json.dumps(report,ensure_ascii=False,indent=2)+"\n",encoding="utf-8")
    print(json.dumps(report,ensure_ascii=False))
    raise SystemExit(0 if result.wasSuccessful() else 1)
