#!/usr/bin/env python3
"""Read-only audit of combat continuation; never extend the original210s case.
SPDX-License-Identifier: GPL-3.0-or-later
"""
import argparse, hashlib, importlib.util, json
from pathlib import Path
ROOT = Path(__file__).resolve().parents[1]

def read(p): return json.loads(p.read_text(encoding="utf-8-sig"))
def sha(p): return hashlib.sha256(p.read_bytes()).hexdigest()
def put(p, o): p.write_bytes((json.dumps(o, ensure_ascii=False, indent=2)+"\n").encode("utf-8"))
def sealed(p):
    rows=read(p/"SHA256SUMS.json")["files"]
    return bool(rows) and all((p/r["path"]).stat().st_size==r["bytes"] and sha(p/r["path"])==r["sha256"].lower() for r in rows)

def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--case-dir",type=Path,default=ROOT/"evidence/combat_transition_20261014")
    ap.add_argument("--output-dir",type=Path,required=True)
    a=ap.parse_args();case=a.case_dir.resolve();out=a.output_dir.resolve()
    if out.exists() or out==case or case in out.parents: ap.error("Use a new output directory outside the closed case.")
    m=read(case/"input_manifest.json");v=read(case/"visual_observation.json");done=read(case/"capture_completion.json")
    prior=ROOT/m["prior_machine_case"];pin=read(prior/"input_manifest.json")
    spec=importlib.util.spec_from_file_location("transition_display7",ROOT/"tools/analyze_display7.py")
    native=importlib.util.module_from_spec(spec);spec.loader.exec_module(native)
    rawpath=case/m["raw_log"];d=native.analyze(rawpath);old=native.analyze(prior/"combat_smoke_20261014.log")
    text=rawpath.read_text(encoding="utf-8-sig");lines=text.splitlines()
    if text and not text.endswith(("\n","\r")): lines=lines[:-1]
    rows=[{"line":i+1,"raw":s,**{k:native.value(t) for k,t in native.FIELDS.findall(s)}} for i,s in enumerate(lines)]
    def selected(prefix): return [r for r in rows if r["raw"].startswith(prefix)]
    live,combat,stage=map(selected,("[LIVE]","[COMBAT]","[STAGE]"))
    pairs=d["pairs"];first,last=pairs[0],pairs[-1]
    observed=pairs[1:] # The first report can cover time before UART was opened.
    counts=[sum(p["main"]["sum"][i] for p in observed) for i in range(5)]
    sat=[sum(p["main"]["sat"][i] for p in observed) for i in range(5)]
    history=[g for g in d["counter_total_window_gaps"] if g.get("prior_capture_history")]
    gaps=[g for g in d["counter_total_window_gaps"] if not g.get("prior_capture_history")]
    anchors={(p["aux"].get("first"),p["aux"]["game_start"]) for p in pairs}
    oldanchors={(p["aux"].get("first"),p["aux"]["game_start"]) for p in old["pairs"] if p["aux"]["game_start"]}
    keys=("events","kills","score_observed","impact_frames","kill_frames","popup_frames","flash_frames","popups_merged","score_hud_calls")
    def mono(rs,ks): return bool(rs) and all(isinstance(r.get(k),int) and r[k]>=0 and (i==0 or r[k]>=rs[i-1][k]) for i,r in enumerate(rs) for k in ks)
    delta={k:combat[-1][k]-combat[0][k] for k in keys}
    sk=("boss_entries","victories","restarts");sd={k:stage[-1][k]-stage[0][k] for k in sk}
    cycles=[];win=None
    for r in d["native_game_events"]:
        if r.get("mode")==2: win=r
        elif r.get("mode")==0 and win is not None: cycles.append({"victory":win,"restart":r});win=None
    timing=[json.loads(s) for s in (case/m["timing_log"]).read_text(encoding="utf-8-sig").splitlines()]
    z=("errors","swap_timeout","front_reject","atlas_bounds","missed","dropped","log_dropped","log_truncated")
    checks={
      "no_boot_or_input_in_segment":d["source"]["boot_line"] is None and not selected("[INPUT]") and not selected("[COMBAT_CTRL]") and not selected("[CAL]"),
      "prior_closed_case_valid":sealed(prior),
      "unchanged_runtime_sources":all(sha(ROOT/n)==h.lower() for n,h in pin["runtime_source_sha256"].items()),
      "matching_ELF_and_atlas":sha(ROOT/"build/neonCombatGardenDemo.elf")==pin["captured_ELF_sha256"].lower()==m["elf_sha256"].lower() and sha(ROOT/"assets/atlas.bin")==pin["captured_atlas_sha256"].lower()==m["atlas_sha256"].lower(),
      "raw_timing_and_analyzer_bound":sha(rawpath)==m["raw_log_sha256"] and sha(case/m["timing_log"])==m["timing_log_sha256"] and sha(Path(__file__))==m["analyzer_sha256"],
      "same_first_and_GAME_START_anchors":len(anchors)==1 and anchors==oldanchors and first["main"]["frame"]>old["pairs"][-1]["main"]["frame"] and first["main"]["t"][1]>old["pairs"][-1]["main"]["t"][1],
      "completed_readonly_120s":done.get("exit_code")==0 and done.get("seconds")==120 and done.get("capture_only") is True and done.get("UART_input_written") is False and "Captured 120s -> " in (case/"capture_completion_tail.txt").read_text(),
      "paired_contiguous_records":d["status"]=="PAIRED" and not d["issues"] and not d["incomplete_rids"] and not d["duplicates"] and all(b["rid"]==a["rid"]+1 for a,b in zip(pairs,pairs[1:])),
      "counter_conservation_after_boundary":not gaps,
      "PX_PART_FULL_zero":bool(observed) and all(counts[i]==0 and sat[i]==0 for i in (0,1,4)) and all(p["main"][k][i]==0 for p in pairs for k in ("sum","sat","max","last") for i in (0,1,4)),
      "GPU_Alpha16_relock_sampled_on":all(p["main"].get("backend")=="GPU" and p["main"].get("alpha")==1 and p["main"].get("panel")==16 and p["main"]["ctrl"]==4 and p["relock_enabled"] for p in pairs),
      "overflow_refused_zero":all(not p["main"]["ov"] and not p["aux"]["tov"] and p["aux"]["refused"]==0 for p in pairs),
      "LIVE_60FPS_faults_zero":bool(live) and all(r.get("fps10",0)>=600 and r.get("backend")=="GPU" and r.get("alpha")==1 and all(r.get(k)==0 for k in z) for r in live) and not d["native_faults"] and not d["native_log_nonzero"] and not d["fatal"],
      "one_true_1959_victory_restart":bool(cycles) and all(c["victory"].get("score")==1959 and c["restart"].get("score")==0 for c in cycles) and sd["victories"]>=1 and sd["restarts"]>=1,
      "lifetime_deltas_monotonic":mono(combat,keys) and mono(stage,sk),
      "effects_triggered_in_segment":all(delta[k]>0 for k in ("events","kills","impact_frames","kill_frames","popup_frames","flash_frames","score_hud_calls")),
      "score_ledger_event_budget":bool(combat) and len(combat)==len(live) and all(r.get("enabled")==1 and r.get("score")==r.get("hit_points",-1)+r.get("kill_points",-1) and r.get("epoch_hits")==r.get("hit_points") and 0<=r.get("peak",-1)<=240 and all(r.get(k)==0 for k in ("event_rejected","event_gaps","event_dropped","essential_drop","decor_drop","gpu_over_budget")) for r in combat),
      "host_receipt_timing_bound":bool(timing) and all(r["line"] in lines for r in timing) and all(b["host_seconds"]>=a["host_seconds"] for a,b in zip(timing,timing[1:])) and 118<=timing[-1]["host_seconds"]<=122,
      "exact_user_transition_reply_bound":v.get("source")=="user observation" and v.get("question_item_id")=='["request_user_input_async","call_4d9cdcf09db74348bdbbdf0b6825ef29",0]' and v.get("answer")=="击杀、胜利和重开均正常，画面对齐" and v.get("applies_to_elf_sha256","").lower()==m["elf_sha256"].lower() and v.get("captured_raw_UART_sha256")==m["raw_log_sha256"],
      "closed_case_valid_if_present":not (case/"SHA256SUMS.json").exists() or sealed(case),
    }
    result={
      "case":case.name,"machine_segment_pass":all(checks.values()),"guards":checks,"host_capture_seconds":done["seconds"],
      "complete_device_window_seconds":(last["main"]["t"][1]-first["main"]["t"][1])/native.HZ,
      "frames_between_complete_report_endpoints":last["main"]["frame"]-first["main"]["frame"],
      "complete_pairs":len(pairs),"included_counter_windows":len(observed),"counter_order":list(native.NAMES),
      "segment_counts":counts,"segment_saturation_incidence":sat,"first_capture_history_excluded":history,
      "first_identity":{"first":hex(next(iter(anchors))[0]),"game_start":hex(next(iter(anchors))[1])},
      "victory_restart_cycles":cycles,"COMBAT_first":combat[0],"COMBAT_last":combat[-1],"COMBAT_delta":delta,"STAGE_delta":sd,
      "LIVE_records":len(live),"fps10_range":[min(r["fps10"] for r in live),max(r["fps10"] for r in live)],
      "worst_us_lifetime_max_observed":max(r["worst_us"] for r in live),
      "render_us_range_sampled":[min(r["render_us"] for r in live),max(r["render_us"] for r in live)],
      "timing_host_receipt_range_seconds":[timing[0]["host_seconds"],timing[-1]["host_seconds"]],
      "visual_observation":v,"transition_visual_pass":checks["exact_user_transition_reply_bound"],
      "limits":[
        "Continuation only; startup Alpha/ColorKey verification remains in the earlier sealed210s loader/capture.",
        "Matching boot anchors and recorded operations support continuity; no new FPGA/RAM readback was performed.",
        "First window can precede capture and is excluded; later counter windows conserve lifetime deltas.",
        "COMBAT/STAGE counters use first-to-last deltas; the uncaptured interval is not certified.",
        "worst_us is a carried lifetime maximum; render_us is sampled, not an exhaustive segment maximum.",
        "AR/R saturation means lower bounds; APB reads and UART receipt do not locate exact video events.",
        "Visual acceptance comes from the exact user reply; no photo/video was captured.",
        "This120s continuation supplements210s smoke; no new600s acceptance was started."
      ]}
    out.mkdir(parents=True);put(out/"native_display7.json",d);put(out/"transition_summary.json",result)
    names=("machine_segment_pass","complete_device_window_seconds","frames_between_complete_report_endpoints","complete_pairs","segment_counts","COMBAT_delta","STAGE_delta","transition_visual_pass")
    print(json.dumps({k:result[k] for k in names}|{"failed_guards":[k for k,v in checks.items() if not v]},ensure_ascii=False))
    raise SystemExit(0 if result["machine_segment_pass"] else 1)

if __name__=="__main__": main()
