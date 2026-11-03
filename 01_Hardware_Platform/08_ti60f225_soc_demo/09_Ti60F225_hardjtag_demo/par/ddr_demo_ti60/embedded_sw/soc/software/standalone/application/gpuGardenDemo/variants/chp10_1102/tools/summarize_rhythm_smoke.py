#!/usr/bin/env python3
"""Audit the revised banking/rhythm/overhead-HP/damage short board preview.
SPDX-License-Identifier: GPL-3.0-or-later
Read-only; native startup and visuals are separate from host pixel contracts.
"""
import argparse,hashlib,importlib.util,json
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
CASE=ROOT/"evidence/rhythm_smoke_20261014"
def read(f):return json.loads(f.read_text(encoding="utf-8-sig"))
def sha(f):return hashlib.sha256(f.read_bytes()).hexdigest()
def put(f,v):f.write_bytes((json.dumps(v,ensure_ascii=False,indent=2)+"\n").encode())
def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--case-dir",type=Path,default=CASE)
    ap.add_argument("--output-dir",type=Path,required=True)
    ap.add_argument("--visual",type=Path)
    a=ap.parse_args();case=a.case_dir.resolve();out=a.output_dir.resolve()
    if out.exists() or out==case or case in out.parents:ap.error("Use a new output directory outside the closed case.")
    m=read(case/"input_manifest.json");rawpath=case/m["raw_log"];raw=rawpath.read_bytes()
    start=raw.rfind(b"GARDEN Ti60 cropped port ")
    if start<0:ap.error("No fresh application boot banner in capture.")
    out.mkdir(parents=True);(out/"current_boot.log").write_bytes(raw[start:])
    spec=importlib.util.spec_from_file_location("rhythm_display7",ROOT/"tools/analyze_display7.py")
    native=importlib.util.module_from_spec(spec);spec.loader.exec_module(native)
    d=native.analyze(out/"current_boot.log");put(out/"native_display7.json",d)
    text=raw[start:].decode("utf-8-sig",errors="replace");lines=text.splitlines()
    if text and not text.endswith(("\n","\r")):lines=lines[:-1]
    rows=[{"line":i+1,"raw":s,**{k:native.value(v) for k,v in native.FIELDS.findall(s)}} for i,s in enumerate(lines)]
    def pick(prefix):return [r for r in rows if r["raw"].startswith(prefix)]
    live,combat,actor,rhythm,fx,stage=map(pick,("[LIVE]","[COMBAT]","[ACTOR_UI]","[RHYTHM]","[FX]","[STAGE]"))
    controls=pick("[RHYTHM_CTRL]");pairs=[p for p in d["pairs"] if p["after_GAME_START_window"]]
    def monotonic(rs,ks):return bool(rs) and all(isinstance(r.get(k),int) and r[k]>=0 and (i==0 or r[k]>=rs[i-1][k]) for i,r in enumerate(rs) for k in ks)
    reports={}
    bound=True
    for key,entry in m["host_reports"].items():
        f=case/entry["case_path"];v=read(f);reports[key]=v
        bound&=sha(f)==entry["sha256"] and v["status"]=="PASS"
        for field in ("source_sha256","integration_source_sha256","tool_sha256"):
            bound&=all(sha(ROOT/n)==h.lower() for n,h in v.get(field,{}).items())
    alpha=d["native_alpha"];wins=[];restarts=[];prev=None
    for r in d["native_game_events"]:
        if r.get("mode")==2 and prev!=2:wins.append(r)
        if r.get("mode")==0 and prev==2:restarts.append(r)
        prev=r.get("mode")
    zero=("errors","swap_timeout","front_reject","atlas_bounds","missed","dropped","log_dropped","log_truncated","report_scheduler_overruns")
    checks={
      "fresh_latest_boot":start>=0 and bool(pick("[READY]")),
      "raw_and_ELF_identity":sha(rawpath)==m["raw_UART_sha256"] and sha(ROOT/"build/neonRhythmGardenDemo.elf")==m["elf_sha256"].lower(),
      "runtime_sources_bound":all(sha(ROOT/n)==h.lower() for n,h in m["runtime_source_sha256"].items()),
      "atlas_unchanged_and_bound":sha(ROOT/"assets/atlas.bin")==m["atlas_sha256"].lower() and (ROOT/"assets/atlas.bin").stat().st_size==236954,
      "active_host_reports_bound":bool(reports) and bound,
      "successful_ELF_atlas_loader":m["elf_sha256"]==read(case/"build_manifest.json")["elf_sha256"] and "[HOST_RELOCK] verified=1 gpu=0x47505531 dbg=0x44424731 ctrl=0x4" in (case/"load.log").read_text(encoding="utf-8-sig"),
      "completed180s_capture":"Captured 180s -> " in (case/"capture_completion_tail.txt").read_text() and m["host_capture_seconds"]==180 and m["capture_exit_code"]==0,
      "display_paired":d["status"]=="PAIRED" and not d["duplicates"] and not d["issues"] and (not d["incomplete_rids"] or (d["source"]["inputs"][0]["partial_tail_excluded"] and d["incomplete_rids"]==[d["pairs"][-1]["rid"]+1])),
      "display_counter_conservation":not d["counter_total_window_gaps"] and d["GAME_START_boundary_verified"],
      "display_PX_PART_FULL_zero":bool(pairs) and all(d["runtime_window_observed_counts"][i]==0 for i in (0,1,4)) and all(p["main"][k][i]==0 for p in pairs for k in ("sum","sat","max","last") for i in (0,1,4)),
      "display_GPU_Alpha_relock_on":d["runtime_all_sampled_ON"] and d["runtime_all_sampled_GPU_Alpha16"],
      "display_overflow_refusal_zero":not d["runtime_overflow_pairs"] and d["refused_max"]==0,
      "alpha35_and_colorkey":len(alpha)==1 and alpha[0].get("rc")==0 and alpha[0].get("cases")==35 and all(alpha[0].get(k)==0 for k in ("pixel_errors","source_errors","guard_errors")) and len(d["native_color_key"])==1 and "verified PASS" in d["native_color_key"][0]["raw"],
      "LIVE_60FPS_and_faults_zero":bool(live) and all(r.get("fps10",0)>=600 and r.get("backend")=="GPU" and r.get("alpha")==1 and all(r.get(k)==0 for k in zero) for r in live) and not d["fatal"] and not d["native_faults"] and not d["native_log_nonzero"],
      "device_window_at_least170s":d["runtime_counter_duration_seconds"] is not None and d["runtime_counter_duration_seconds"]>=170,
      "one_real1959_win_restart":bool(wins) and bool(restarts) and all(r.get("score")==1959 for r in wins) and stage[-1].get("victories",0)>=1 and stage[-1].get("restarts",0)>=1,
      "damage_and_score_ledgers":bool(combat) and len(combat)==len(live) and all(r.get("score")==r.get("hit_points",-1)+r.get("kill_points",-1) and r.get("epoch_hits")==r.get("hit_points") and all(r.get(k)==0 for k in ("event_rejected","event_gaps","event_dropped","damage_display_clamped","essential_drop","decor_drop","gpu_over_budget")) for r in combat),
      "combat_lifetime_monotonic":monotonic(combat,("events","kills","score_observed","damage_observed","impact_frames","kill_frames","popup_frames","flash_frames","popups_merged","popups_updated","groups_replaced")),
      "real_damage_and_single_lane_updates_triggered":bool(combat) and all(combat[-1].get(k,0)>0 for k in ("damage_observed","popup_frames","popups_merged","popups_updated","kill_frames")),
      "progressive_banking_and_visible_bars":bool(actor) and len(actor)==len(live) and all(r.get("bar_drop")==0 and r.get("bars")==r.get("expected_bars") and r.get("bars")==r.get("enemy_bars",-1)+r.get("boss_bars",-1) for r in actor) and actor[-1].get("bank_changes",0)>10 and {-2,2}<={r.get("bank") for r in actor} and any(r.get("enemy_bars",0)>0 for r in actor) and any(r.get("boss_bars")==1 for r in actor),
      "whole_plane_ghosts_default_off":bool(fx) and all(r.get("flags")==5 and r.get("ghosts")==0 and r.get("trail_frames")==0 for r in fx),
      "new_rhythm_config_and_budget":len(controls)==1 and all(controls[0].get(k)==v for k,v in {"enabled":1,"actual_boss_timing":1,"player_bank_poses":5,"overhead_hp":1,"remote_hp":0,"bullet_budget":128,"popup_lanes":"one_per_target","popups":"HP_DAMAGE"}.items()) and max(r.get("eb",0) for r in live)<=128,
      "genuine_charge_burst_recovery":bool(rhythm) and {1,3}<={r.get("state") for r in rhythm if r.get("active")} and {1,2,3}<={r.get("attack") for r in rhythm if r.get("active")} and any(r.get("charge_windows",0)>0 and r.get("bursts",0)>0 and r.get("rests",0)>0 and r.get("shots",0)>r.get("bursts",0) for r in rhythm),
      "closed_seal_valid_if_present":not (case/"SHA256SUMS.json").exists() or all((case/r["path"]).stat().st_size==r["bytes"] and sha(case/r["path"])==r["sha256"] for r in read(case/"SHA256SUMS.json")["files"]),
    }
    observation=read(a.visual) if a.visual else None
    visual_bound=bool(observation and observation.get("elf_sha256","").lower()==m["elf_sha256"].lower() and observation.get("raw_UART_sha256")==m["raw_UART_sha256"])
    summary={
      "case":case.name,"machine_short_preview_pass":all(checks.values()),"guards":checks,
      "host_capture_seconds":m["host_capture_seconds"],"complete_device_window_seconds":d["runtime_counter_duration_seconds"],
      "runtime_frame_samples":d["runtime_samples"],"complete_pairs":d["complete_pairs"],
      "partial_tail_excluded":d["source"]["inputs"][0]["partial_tail_excluded"],
      "incomplete_boundary_rids_excluded":d["incomplete_rids"],
      "display_counter_order":d["counter_order"],"display_counts":d["runtime_window_observed_counts"],
      "fps10_range":[min(r["fps10"] for r in live),max(r["fps10"] for r in live)],
      "LIVE_records_below60fps":[r for r in live if r["fps10"]<600],
      "LIVE_faults_zero":bool(live) and all(all(r.get(k)==0 for k in zero) for r in live) and not d["fatal"] and not d["native_faults"] and not d["native_log_nonzero"],
      "worst_render_us_observed":max(r["worst_us"] for r in live),
      "render_us_periodic_average_range":[min(r["render_us"] for r in live),max(r["render_us"] for r in live)],
      "visible_sprite_max_in_LIVE":max(r["sprites"] for r in live),"actual_enemy_bullet_peak":max(r.get("peak",0) for r in d["native_game_events"]),
      "LIVE_records":len(live),"victories":wins,"automatic_restarts":restarts,"last_COMBAT":combat[-1],"last_ACTOR_UI":actor[-1],"last_STAGE":stage[-1],
      "visual_observation":observation,"visual_identity_bound":visual_bound,
      "visual_acceptance":"HASH_BOUND_USER_OBSERVATION_RECORDED" if visual_bound else "PENDING_USER_OBSERVATION",
      "limits":["180s preview is separate from the initial rejected dense attempt and earlier accepted combat version; no new600s run.",
        "Periodic telemetry groups are staggered over8frames; each uses its actual emission frame, not an atomic shared state snapshot.",
        "Frame timing excludes uncaptured time; period FPS/render averages are samples, not host mock performance.",
        "gpu_over_budget covers rendering, while FPS tests the full presented-frame loop.",
        "AR/R saturated counts are lower bounds; panel16 telemetry is configured diagnostic alpha, not every effect's alpha.",
        "Counter PASS is independent from the user appearance judgment; user reply is hash-bound without photo/video.",
        "PinnedRTL11 disk MD5 and loader GPU1/DBG1 support provenance; no fresh FPGA readback or Flash programming."]}
    put(out/"smoke_summary.json",summary)
    print(json.dumps({k:summary[k] for k in ("machine_short_preview_pass","complete_device_window_seconds","runtime_frame_samples","fps10_range","worst_render_us_observed","display_counts","visual_acceptance")}|{"failed_guards":[k for k,v in checks.items() if not v]},ensure_ascii=False))
    raise SystemExit(0 if summary["machine_short_preview_pass"] else 1)
if __name__=="__main__":main()
