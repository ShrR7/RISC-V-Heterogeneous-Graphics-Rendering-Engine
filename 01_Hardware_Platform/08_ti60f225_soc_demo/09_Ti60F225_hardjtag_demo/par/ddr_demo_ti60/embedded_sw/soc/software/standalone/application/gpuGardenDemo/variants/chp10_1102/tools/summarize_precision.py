#!/usr/bin/env python3
"""Audit the precision64 short60s capture on pinned RTL11.
Strict60FPS is separate; no identity, source, probe or counter failure is waived.
SPDX-License-Identifier: GPL-3.0-or-later
Read-only; native startup and visuals are separate from host pixel contracts.
"""
import argparse,hashlib,importlib.util,json,re,zlib
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
CASE=ROOT/"evidence/precision_smoke_20261015"
PINNED_MD5="7BF53A36220B756369371893B1814513"
PINNED_BYTES=2294091
ATLAS_BYTES=631954
ATLAS_CRC=0x6486F38E
ATLAS_STARTUP=b"[ATLAS] bytes=631954 crc32=0x6486f38e expected=0x6486f38e"
EXPECTED_WINDOW={"asset_base":0x5C4000,"asset_end":0x680000,
    "atlas_safe_end":0x67E000,"panel_base":0x67E000,
    "probe_base":0x67F800,"probe_end":0x680000}
def read(f):return json.loads(f.read_text(encoding="utf-8-sig"))
def sha(f):return hashlib.sha256(f.read_bytes()).hexdigest()
def put(f,v):f.write_bytes((json.dumps(v,ensure_ascii=False,indent=2)+"\n").encode())
def member(case,name):
    f=(case/name).resolve()
    if f==case or case not in f.parents:raise ValueError("Case path escapes its evidence closure: "+str(name))
    return f

def seal_review(case):
    path=case/"SHA256SUMS.json"
    if not path.exists():return {"present":False,"valid_if_present":True,"status":"UNSEALED_STAGING"}
    try:
        entries=read(path)["files"];names=[r["path"] for r in entries]
        listed={str(Path(n)).replace("\\","/").lower() for n in names}
        actual={f.relative_to(case).as_posix().lower() for f in case.rglob("*") if f.is_file() and f!=path}
        valid=len(names)==len(listed) and listed==actual
        valid&=all(member(case,r["path"]).stat().st_size==r["bytes"] and sha(member(case,r["path"]))==r["sha256"].lower() for r in entries)
        return {"present":True,"valid_if_present":bool(valid),"status":"CLOSED_VALID" if valid else "CLOSED_INVALID",
                "listed_count":len(entries),"unlisted":sorted(actual-listed),"missing":sorted(listed-actual)}
    except (KeyError,ValueError,OSError,TypeError) as e:return {"present":True,"valid_if_present":False,"status":"CLOSED_INVALID","error":str(e)}

def timing_review(case,m,raw):
    f=member(case,m.get("timing_log","host_receipt_timing.jsonl"));entries=[]
    try:
        data=f.read_bytes();closed=not data or data.endswith((b"\n",b"\r"))
        entries=[json.loads(line) for line in data.decode("utf-8-sig").splitlines() if line.strip()]
        native_lines=set(raw.decode("utf-8-sig",errors="replace").splitlines())
        valid=bool(entries) and closed and sha(f)==m["timing_sha256"].lower()
        valid&=all(isinstance(r.get("host_seconds"),(int,float)) and r["host_seconds"]>=0 and
                   (i==0 or r["host_seconds"]>=entries[i-1]["host_seconds"]) and r.get("line") in native_lines for i,r in enumerate(entries))
        return {"valid":bool(valid),"records":len(entries),"complete_JSONL":closed,"sha256":sha(f),
                "first_host_seconds":entries[0]["host_seconds"] if entries else None,
                "last_host_seconds":entries[-1]["host_seconds"] if entries else None}
    except (KeyError,ValueError,OSError,TypeError) as e:return {"valid":False,"error":str(e)}

def periodic_groups(rows,raw_partial,native_pairs):
    prefixes=("[FX]","[STAGE]","[FEEDBACK]","[SCENE]","[COMBAT]","[ACTOR_UI]","[RHYTHM]","[ANIMATION]")
    groups=[];group=None
    for row in rows:
        if row["raw"].startswith("[LIVE]"):
            if group is not None:groups.append(group)
            group={"LIVE_line":row["line"],"LIVE_frame":row.get("frame"),"members":{},"duplicates":[],"coalescing_candidates":[],"phase8_evidence":None}
        elif group is not None:
            name=next((x for x in prefixes if row["raw"].startswith(x)),None)
            if row["raw"].startswith("[DISPLAY7]") and row.get("phase")=="GARDEN":name="[DISPLAY7_PHASE8]"
            if row["raw"].startswith("[DISPLAY7]") and row.get("phase") in ("STATE","SECOND") and row.get("frame")==group["LIVE_frame"]+7:
                group["coalescing_candidates"].append(row)
            if name:
                if name in group["members"]:group["duplicates"].append(name)
                group["members"][name]=row["line"]
    if group is not None:groups.append(group)
    required=set(prefixes)|{"[DISPLAY7_PHASE8]"}
    pairs={p["rid"]:p for p in native_pairs}
    for i,g in enumerate(groups):
        if "[DISPLAY7_PHASE8]" not in g["members"]:
            for candidate in g["coalescing_candidates"]:
                pair=pairs.get(candidate.get("rid"))
                if not pair:continue
                before=g["members"].get("[RHYTHM]",-1);after=g["members"].get("[ANIMATION]",-1)
                main,aux=pair["main"],pair["aux"]
                if before<main["line"]<aux["line"]<after and before>=0 and main["frame"]==g["LIVE_frame"]+7 and main.get("phase") in ("STATE","SECOND"):
                    g["members"]["[DISPLAY7_PHASE8]"]=main["line"]
                    g["phase8_evidence"]={"actual_phase":main["phase"],"rid":pair["rid"],"frame":main["frame"],
                        "RHYTHM_line":before,"MAIN_line":main["line"],"AUX_line":aux["line"],"ANIMATION_line":after,
                        "reason":"same-frame SECOND/STATE sample already consumed the phase8 sample; no synthetic GARDEN row"}
                    break
        g.pop("coalescing_candidates",None)
        g["missing"]=sorted(required-set(g["members"]))
        g["complete"]=not g["missing"] and not g["duplicates"]
        g["allowed_partial_tail"]=bool(i==len(groups)-1 and g["missing"]==["[ANIMATION]"] and
             not g["duplicates"] and raw_partial.startswith("[ANIMATION]") and not raw_partial.endswith(("\r","\n")))
    return groups

def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--case-dir",type=Path,default=CASE)
    ap.add_argument("--output-dir",type=Path,required=True)
    ap.add_argument("--visual",type=Path)
    a=ap.parse_args();case=a.case_dir.resolve();out=a.output_dir.resolve()
    if out.exists() or out==case or case in out.parents:ap.error("Use a new output directory outside the closed case.")
    m=read(case/"input_manifest.json");rawpath=member(case,m["raw_log"]);raw=rawpath.read_bytes()
    banner=raw.rfind(b"GARDEN Ti60 cropped port ")
    atlas=raw.rfind(ATLAS_STARTUP)
    full_banner=bool(banner>=0 and atlas>banner and b"[LIVE]" not in raw[banner:atlas] and b"[READY]" not in raw[banner:atlas])
    start=banner if full_banner else atlas
    if start<0:ap.error("No fresh verified atlas startup in capture.")
    # A debug reset can truncate the old UART line and first new banner.
    # Keep that loss explicit; accept only the complete structured startup
    # and native GAME_START baseline for the subsequent runtime audit.
    startup=raw[start:]
    needed=(ATLAS_STARTUP,b"[CAL] ticks_per_second=100000000",b"[ALPHA_PROBE] rc=0 present=1 cases=35",b"[PRECISION_PROBE] rc=0 sprites=20 cases=40",b"[ATLAS_POST_PROBE] bytes=631954",b"[DISPLAY7] phase=GAME_START",b"[READY] COM11",b"popup_lanes=per_hit popup_pool=32 popup_overlap=1")
    if not all(n in startup for n in needed):ap.error("Structured fresh startup is incomplete.")
    out.mkdir(parents=True);(out/"current_boot.log").write_bytes(raw[start:])
    spec=importlib.util.spec_from_file_location("precision_display7",ROOT/"tools/analyze_display7.py")
    native=importlib.util.module_from_spec(spec);spec.loader.exec_module(native)
    d=native.analyze(out/"current_boot.log");put(out/"native_display7.json",d)
    text=raw[start:].decode("utf-8-sig",errors="replace");lines=text.splitlines()
    if text and not text.endswith(("\n","\r")):lines=lines[:-1]
    rows=[{"line":i+1,"raw":s,**{k:native.value(v) for k,v in native.FIELDS.findall(s)}} for i,s in enumerate(lines)]
    def pick(prefix):return [r for r in rows if r["raw"].startswith(prefix)]
    live,combat,actor,rhythm,fx,stage,animation=map(pick,("[LIVE]","[COMBAT]","[ACTOR_UI]","[RHYTHM]","[FX]","[STAGE]","[ANIMATION]"))
    animation_controls=pick("[ANIMATION_CTRL]")
    raw_partial=text.splitlines()[-1] if text and not text.endswith(("\n","\r")) else ""
    groups=periodic_groups(rows,raw_partial,d["pairs"]);seal=seal_review(case);timing=timing_review(case,m,raw)
    controls=pick("[RHYTHM_CTRL]");pairs=[p for p in d["pairs"] if p["after_GAME_START_window"]]
    def monotonic(rs,ks):return bool(rs) and all(isinstance(r.get(k),int) and r[k]>=0 and (i==0 or r[k]>=rs[i-1][k]) for i,r in enumerate(rs) for k in ks)
    snapshot_root=case/"source_snapshot";snapshot_manifest=case/"source_snapshot_manifest.json"
    snapshot_valid=False
    if snapshot_root.exists() and snapshot_manifest.exists():
        sr=read(snapshot_manifest)["files"]
        snapshot_valid=all(member(case,"source_snapshot/"+r["path"]).stat().st_size==r["bytes"] and sha(member(case,"source_snapshot/"+r["path"]))==r["sha256"].lower() for r in sr)
        snapshot_valid&={r["path"] for r in sr}=={r.relative_to(snapshot_root).as_posix() for r in snapshot_root.rglob("*") if r.is_file()}
    def runtime_file(name):
        snap=member(case,"source_snapshot/"+name)
        return snap if snapshot_valid and snap.is_file() else ROOT/name
    captured_elf=member(case,m.get("elf_case_path","neonHeadingPrecisionDemo.elf"))
    elf_path=captured_elf if captured_elf.exists() else ROOT/"build/neonHeadingPrecisionDemo.elf"
    reports={}
    bound=True
    for key,entry in m["host_reports"].items():
        f=member(case,entry["case_path"]);v=read(f);reports[key]=v
        bound&=sha(f)==entry["sha256"] and v["status"]=="PASS"
        for field in ("source_sha256","integration_source_sha256","tool_sha256"):
            bound&=all(sha(runtime_file(n))==h.lower() for n,h in v.get(field,{}).items())
    prog=m.get("fpga_programming",{});program_ok=False;program_text=""
    try:
        pf=member(case,prog["case_path"]);program_text=pf.read_text(encoding="utf-8-sig")
        program_ok=sha(pf)==prog["sha256"].lower() and prog.get("expected_md5","").upper()==PINNED_MD5 and prog.get("expected_bytes")==PINNED_BYTES
        program_ok&=prog.get("mode")=="volatile_jtag" and prog.get("exit_code")==0 and prog.get("flash_programmed") is False and prog.get("readback_performed") is False
        program_ok&=m.get("FPGA_reprogrammed_during_task",m.get("FPGA_reprogrammed")) is True and m.get("Flash_written") is False and m.get("fpga_bit_md5","").upper()==PINNED_MD5
        program_ok&=(case/"fpga_md5.txt").read_text(encoding="utf-8-sig").strip().upper()==PINNED_MD5
        program_ok&="finished with JTAG programming" in program_text and "Device ID read from JTAG: 0x10660A79" in program_text and "ddr_demo_ti60_rtl11.bit" in program_text
    except (KeyError,ValueError,OSError,TypeError):pass
    source_manifest_path=case/"precision_source_manifest.json"
    source_manifest=read(source_manifest_path)
    alpha=d["native_alpha"];wins=[];restarts=[];prev=None
    for r in d["native_game_events"]:
        if r.get("mode")==2 and prev!=2:wins.append(r)
        if r.get("mode")==0 and prev==2:restarts.append(r)
        prev=r.get("mode")
    zero=("errors","swap_timeout","front_reject","atlas_bounds","missed","dropped","log_dropped","log_truncated","report_scheduler_overruns")
    palette=m.get("danger_palette","original5")
    animation_expected={"enabled":1,"core_unchanged":1,"collision_unchanged":1,"bullet_full_visibility":1,
        "boss_white_replace":0,"shell_ease":1,"wing_lag":[2,3],"backdrop_speed":[20,60],
        "popup_fixed_anchor":1,"popup_ease":1,"popup_overhead":1,"death_ticks":30,
        "palette":"original","bullet_heading":"velocity64","protected_camera_max":4}
    profiles=pick("[FRAME_PROFILE]")
    capture_seconds=m["host_capture_seconds"]
    precision=pick("[PRECISION_PROBE]");post_atlas=pick("[ATLAS_POST_PROBE]")
    atlas_file=runtime_file("assets/atlas.bin");atlas_data=atlas_file.read_bytes()
    loader_path=case/"load.log";loader_text=loader_path.read_text(encoding="utf-8-sig")
    live_faults_zero=bool(live) and all(r.get("backend")=="GPU" and r.get("alpha")==1 and
        all(r.get(k)==0 for k in zero) for r in live) and not d["fatal"] and not d["native_faults"] and not d["native_log_nonzero"]
    source_window_ok=all(source_manifest.get(k)==v and m.get(k)==v for k,v in EXPECTED_WINDOW.items())
    # Bounds are checked against both frozen C constants and native post-probe
    # register-independent output; this establishes software address intent,
    # not a DDR capacity inference from the device model.
    regs=runtime_file("src/gpu_regs.h").read_text(encoding="utf-8-sig")
    def define_uint(name):
        match=re.search(r"#define\s+"+name+r"\s+(0x[0-9A-Fa-f]+|[0-9]+)[Uu]?\b",regs)
        return int(match.group(1),0) if match else None
    source_window_ok&=define_uint("ASSET_BASE_ADDR")==EXPECTED_WINDOW["asset_base"] and define_uint("ASSET_SIZE_BYTES")==0xBC000
    precision_expected={"rc":0,"sprites":20,"cases":40,"commands":40,"pixels":2560,
        "key_pixels":1058,"high_region_sources":20,"opaque_cases":20,"alpha_cases":20,
        "pixel_errors":0,"guard_errors":0,"source_errors":0,"restore_skipped":0,
        "min_source":0x600004,"max_source":0x652D48}
    post_expected={"bytes":ATLAS_BYTES,"crc32":ATLAS_CRC,"expected":ATLAS_CRC,
        **{k:v for k,v in EXPECTED_WINDOW.items() if k in ("asset_end","panel_base","probe_base","probe_end")}}
    ledger_faults=("event_rejected","event_gaps","event_dropped","damage_display_clamped","essential_drop","decor_drop","gpu_over_budget")
    checks={
      "fresh_latest_boot":start>=0 and bool(pick("[READY]")),
      "complete_startup_capture":full_banner and bool(d["pairs"]) and d["pairs"][0]["rid"]==1 and d["pairs"][0]["main"].get("phase")=="FIRST_CPU_READ",
      "source_snapshot_closure_valid":bool(snapshot_valid) or (not snapshot_root.exists() and not snapshot_manifest.exists()),
      "source_manifest_identity":sha(source_manifest_path)==m.get("source_manifest_sha256","").lower() and source_manifest.get("runtime_source_sha256")==m["runtime_source_sha256"] and source_manifest.get("elf_sha256","").lower()==m["elf_sha256"].lower() and source_manifest.get("directions")==m.get("directions")==64,
      "host_receipt_timing_bound":timing["valid"],
      "pinned_RTL11_volatile_JTAG_provenance":bool(program_ok),
      "periodic_capture_all_groups_complete":bool(groups) and all(g["complete"] for g in groups),
      "periodic_runtime_groups_complete_except_documented_tail":bool(groups) and all(g["complete"] or g["allowed_partial_tail"] for g in groups),
      "raw_and_ELF_identity":sha(rawpath)==m["raw_UART_sha256"] and sha(elf_path)==m["elf_sha256"].lower(),
      "runtime_sources_bound":all(sha(runtime_file(n))==h.lower() for n,h in m["runtime_source_sha256"].items()),
      "precision_atlas_bound":sha(atlas_file)==m["atlas_sha256"].lower()==source_manifest.get("atlas_sha256","").lower() and len(atlas_data)==m.get("atlas_bytes")==source_manifest.get("atlas_bytes")==ATLAS_BYTES and zlib.crc32(atlas_data)&0xFFFFFFFF==ATLAS_CRC and source_manifest.get("atlas_crc32","").upper()=="6486F38E",
      "active_host_reports_bound":{"assets","heading","probe","preview"}<=set(reports) and bound,
      "successful_ELF_atlas_loader":m["elf_sha256"].lower()==read(case/"build_manifest.json")["elf_sha256"].lower() and sha(loader_path)==m.get("loader_sha256","").lower() and "[HOST_RELOCK] verified=1 gpu=0x47505531 dbg=0x44424731 ctrl=0x4" in loader_text,
      "completed_requested_capture":("Captured %ds -> " % capture_seconds) in (case/"capture_completion_tail.txt").read_text() and capture_seconds==60 and m["capture_exit_code"]==0 and m.get("new600s_run") is False,
      "display_paired":d["status"]=="PAIRED" and not d["duplicates"] and not d["issues"] and (not d["incomplete_rids"] or (d["source"]["inputs"][0]["partial_tail_excluded"] and d["incomplete_rids"]==[d["pairs"][-1]["rid"]+1])),
      "display_counter_conservation":not d["counter_total_window_gaps"] and d["GAME_START_boundary_verified"],
      "display_runtime_counter_conservation":d["GAME_START_boundary_verified"] and all(g.get("prior_capture_history") and g.get("rid")==d["pairs"][0]["rid"] and all(g["total"][i]==g["window_sum"][i] for i in (0,1,4)) for g in d["counter_total_window_gaps"]),
      "display_PX_PART_FULL_zero":bool(pairs) and all(d["runtime_window_observed_counts"][i]==0 for i in (0,1,4)) and all(p["main"][k][i]==0 for p in pairs for k in ("sum","sat","max","last") for i in (0,1,4)),
      "display_GPU_Alpha_relock_on":d["runtime_all_sampled_ON"] and d["runtime_all_sampled_GPU_Alpha16"],
      "display_overflow_refusal_zero":not d["runtime_overflow_pairs"] and d["refused_max"]==0,
      "alpha35_and_colorkey":len(alpha)==1 and alpha[0].get("rc")==0 and alpha[0].get("cases")==35 and all(alpha[0].get(k)==0 for k in ("pixel_errors","source_errors","guard_errors")) and len(d["native_color_key"])==1 and "verified PASS" in d["native_color_key"][0]["raw"],
      "precision_GPU40_physical_pixel_probe":len(precision)==1 and all(precision[0].get(k)==v for k,v in precision_expected.items()),
      "precision_post_probe_complete_atlas_CRC_and_DDR_bounds":len(post_atlas)==1 and all(post_atlas[0].get(k)==v for k,v in post_expected.items()) and source_window_ok,
      "precision_probe_startup_order":len(precision)==1 and len(post_atlas)==1 and len(alpha)==1 and alpha[0]["line"]<precision[0]["line"]<post_atlas[0]["line"]<next((r["line"] for r in rows if r["raw"].startswith("[READY]")),0),
      "LIVE_backend_and_faults_zero":live_faults_zero,
      "LIVE_60FPS_and_faults_zero":live_faults_zero and all(isinstance(r.get("fps10"),int) and r["fps10"]>=600 for r in live),
      "device_window_matches_scope":d["runtime_counter_duration_seconds"] is not None and d["runtime_counter_duration_seconds"]>=40,
      "precision64_short_scope":m.get("acceptance_scope")=="precision64_preview_60s" and m.get("enemy_bullet_heading")=="velocity_nearest64" and source_manifest.get("original_280_descriptors_unchanged") is True and source_manifest.get("original_atlas_prefix_bytes")==236954 and any(r.get("mode")==1 for r in rhythm),
      "damage_and_score_ledgers":bool(combat) and len(combat)==len(live) and all(r.get("score")==r.get("hit_points",-1)+r.get("kill_points",-1) and r.get("epoch_hits")==r.get("hit_points") and all(r.get(k)==0 for k in ledger_faults) for r in combat),
      "combat_lifetime_monotonic":monotonic(combat,("events","kills","score_observed","damage_observed","impact_frames","kill_frames","popup_frames","flash_frames","popup_spawned","popup_anchors_chosen","popup_anchor_failures","popup_stale_cleared")),
      "real_per_hit_independent_popups_triggered":bool(combat) and all(combat[-1].get(k,0)>0 for k in ("damage_observed","popup_frames","popup_spawned","kill_frames")) and 1<combat[-1].get("popup_peak_active",0)<=32 and all(r.get("popup_spawned")==r.get("events")==r.get("popup_anchors_chosen") and all(r.get(k)==0 for k in ("popups_merged","popups_updated","groups_replaced","popup_pool_exhausted","popup_replaced","popup_layout_dropped","popup_anchor_failures")) for r in combat),
      "progressive_banking_and_visible_bars":bool(actor) and len(actor)==len(live) and all(r.get("bar_drop")==0 and r.get("bars")==r.get("expected_bars") and r.get("bars")==r.get("enemy_bars",-1)+r.get("boss_bars",-1) for r in actor) and actor[-1].get("bank_changes",0)>10 and {-2,2}<={r.get("bank") for r in actor} and any(r.get("enemy_bars",0)>0 for r in actor) and any(r.get("boss_bars")==1 for r in actor),
      "whole_plane_ghosts_default_off":bool(fx) and all(r.get("flags")==5 and r.get("ghosts")==0 and r.get("trail_frames")==0 for r in fx),
      "new_rhythm_config_and_budget":len(controls)==1 and all(controls[0].get(k)==v for k,v in {"enabled":1,"actual_boss_timing":1,"player_bank_poses":5,"overhead_hp":1,"remote_hp":0,"bullet_budget":128,"popup_lanes":"per_hit","popup_pool":32,"popup_overlap":1,"popups":"HP_DAMAGE"}.items()) and max(r.get("eb",0) for r in live)<=128,
      "genuine_charge_burst_recovery":bool(rhythm) and {1,3}<={r.get("state") for r in rhythm if r.get("active")} and all(r.get("attack") in (1,2,3) for r in rhythm if r.get("active")) and any(r.get("charge_windows",0)>0 and r.get("bursts",0)>0 and r.get("rests",0)>0 and r.get("shots",0)>r.get("bursts",0) for r in rhythm),
      "animation_controls_exact":len(animation_controls)==1 and all(animation_controls[0].get(k)==v for k,v in animation_expected.items()),
      "animation_runtime_bounds":bool(animation) and all(r.get("fragment_drop")==0 and
          0<=r.get("fragments",-1)<=8 and 0<=r.get("enemy_bullets",-1)<=128 and
          abs(r.get("camera_x",999))<=4 and abs(r.get("camera_y",999))<=4 and
          0<=r.get("open_q8",-1)<=256 and 0<=r.get("core_alpha",-1)<=32 and 0<=r.get("cue_alpha",-1)<=32 and
          0<=r.get("peak",-1)<=240 and r.get("boss_pose") in (0,1) for r in animation),
      "animation_lifetime_monotonic":monotonic(animation,("deaths","death_frames","fragment_frames","fragment_drop","danger_frames","remapped","camera_frames")),
      "original_multicolour_danger_bullets_triggered":bool(animation) and animation[-1].get("danger_frames",0)>0 and all(r.get("remapped")==0 for r in animation) and palette in ("original","original5"),
      "frame_profile_collected_when_requested":not m.get("animation_profile_requested",False) or (len(profiles)==1 and profiles[0].get("frames",0)>1500 and profiles[0].get("span_ms",0)>=40000 and profiles[0].get("backend")=="GPU" and profiles[0].get("alpha")==1 and profiles[0].get("errors")==0),
      "shared_boss_pose_sampled":bool(animation) and any(r.get("boss_pose")==1 and r.get("open_q8",0)>0 for r in animation),
      "gameplay_core_score_head_animation_renderer_driver_unchanged":bool(m.get("unchanged_core_score_head_animation_renderer_driver_sha256")) and
          all(sha(runtime_file(n))==h.lower() for n,h in m.get("unchanged_core_score_head_animation_renderer_driver_sha256",{}).items()) and
          m.get("gameplay_collision_unchanged") is True and m.get("spawn_collision_grace_added") is False and m.get("global_hitstop_or_slowmotion_added") is False,
      "closed_seal_valid_if_present":seal["valid_if_present"],
    }
    observation=read(a.visual) if a.visual else None
    visual_bound=bool(observation and observation.get("elf_sha256","").lower()==m["elf_sha256"].lower() and observation.get("raw_UART_sha256")==m["raw_UART_sha256"])
    summary={
      "case":case.name,"machine_short_preview_pass":all(checks.values()),"guards":checks,
      "runtime_functional_preview_pass":all(v for k,v in checks.items() if k!="LIVE_60FPS_and_faults_zero"),
      "functional_guard_exclusions":["LIVE_60FPS_and_faults_zero"],
      "full_boot_banner_captured":full_banner,"startup_selection":"boot_banner" if full_banner else "verified_atlas_CAL_ALPHA_READY_GAME_START_chain",
      "uncaptured_startup_counter_history":d["counter_total_window_gaps"],
      "host_capture_seconds":m["host_capture_seconds"],"host_receipt_timing":timing,"seal":seal,
      "runtime_source_binding":"case/source_snapshot" if snapshot_valid else "current_runtime",
      "current_source_matches":all((ROOT/n).is_file() and sha(ROOT/n)==h.lower() for n,h in m["runtime_source_sha256"].items()),
      "ELF_binding":str(elf_path),
      "telemetry_group_frames":9,"telemetry_groups_atomic":False,"periodic_groups":groups,
      "presentation_policy":{"popup":m.get("popup_policy"),"palette":palette},"frame_profiles":profiles,
      "acceptance_scope":m.get("acceptance_scope","precision64_preview_60s"),"full_cycle_tested_in_this_case":False,"new600s_run":False,
      "precision_pixel_probe":precision,"post_probe_atlas":post_atlas,"asset_window":EXPECTED_WINDOW,
      "analysis_tool_sha256":sha(Path(__file__)),"analyze_display7_sha256":sha(ROOT/"tools/analyze_display7.py"),
      "periodic_complete_groups":sum(g["complete"] for g in groups),
      "boundary_partial_groups":[g for g in groups if g["allowed_partial_tail"]],
      "coalesced_phase8_samples":[g["phase8_evidence"] for g in groups if g["phase8_evidence"]],
      "excluded_partial_UART_tail":raw_partial,
      "FPGA_provenance":{"reprogrammed":m.get("FPGA_reprogrammed"),"mode":prog.get("mode"),"pinned_MD5":PINNED_MD5,
          "pinned_bytes":PINNED_BYTES,"programmer_log_sha256":prog.get("sha256"),"Flash_written":False,
          "readback_performed":False,"limit":"Successful same-turn volatile JTAG and pinned input hash; no FPGA configuration readback."},"complete_device_window_seconds":d["runtime_counter_duration_seconds"],
      "runtime_frame_samples":d["runtime_samples"],"complete_pairs":d["complete_pairs"],
      "partial_tail_excluded":d["source"]["inputs"][0]["partial_tail_excluded"],
      "incomplete_boundary_rids_excluded":d["incomplete_rids"],
      "display_counter_order":d["counter_order"],"display_counts":d["runtime_window_observed_counts"],
      "fps10_range":[min((r.get("fps10",0) for r in live),default=0),max((r.get("fps10",0) for r in live),default=0)],
      "LIVE_records_below60fps":[r for r in live if r.get("fps10",0)<600],
      "LIVE_faults_zero":live_faults_zero,
      "worst_render_us_observed":max((r.get("worst_us",0) for r in live),default=0),
      "render_us_periodic_average_range":[min((r.get("render_us",0) for r in live),default=0),max((r.get("render_us",0) for r in live),default=0)],
      "visible_sprite_max_in_LIVE":max((r.get("sprites",0) for r in live),default=0),"actual_enemy_bullet_peak":max((r.get("peak",0) for r in d["native_game_events"]),default=0),
      "LIVE_records":len(live),"victories":wins,"automatic_restarts":restarts,"last_COMBAT":combat[-1] if combat else None,"last_ACTOR_UI":actor[-1] if actor else None,"last_STAGE":stage[-1] if stage else None,"last_complete_ANIMATION":animation[-1] if animation else None,
      "visual_observation":observation,"visual_identity_bound":visual_bound,
      "visual_acceptance":"HASH_BOUND_USER_OBSERVATION_RECORDED" if visual_bound else "PENDING_USER_OBSERVATION",
      "limits":["This60s precision64 capture belongs only to its exact policy/source/ELF; at least40native seconds are required, with no full victory or new600s requirement.",
        "Functional preview excludes only60FPS; identity, raw hashes, loader, source, ELF, native pixel probe and display counter failures remain mandatory failures.",
        "40native keyed blits cover20selected high-region fine sprites, not every atlas angle. The post-probe full atlas CRC and guard readback cover overwrite risk; no physical DDR capacity sweep is claimed.",
        "RHYTHM_CTRL atlas_unchanged=1 is an inherited label, not evidence that the complete atlas is unchanged. Only the original236954B prefix and280descriptors are preserved; the complete atlas is now631954B.",
        "Missing/truncated banner or FIRST_CPU_READ is not repaired; full startup guard fails. GAME_START totals provide the measured runtime baseline, with any startup counter-history gap retained.",
        "Periodic telemetry groups are staggered over9frames; each uses its actual emission frame, not an atomic shared state snapshot.",
        "Frame timing excludes uncaptured time; period FPS/render averages are samples, not host mock performance.",
        "gpu_over_budget covers rendering, while FPS tests the full presented-frame loop.",
        "AR/R saturated counts are lower bounds; panel16 telemetry is configured diagnostic alpha, not every effect's alpha.",
        "Counter/function PASS is independent from appearance judgments; a normal-picture/transition reply does not prove each pose, charge or popup animation detail.",
        "PinnedRTL11 MD5, successful same-turn volatile JTAG programming and loader GPU1/DBG1/CTRL4 support provenance; FPGA configuration readback was not performed, and Flash was not written."]}
    put(out/"smoke_summary.json",summary)
    print(json.dumps({k:summary[k] for k in ("machine_short_preview_pass","complete_device_window_seconds","runtime_frame_samples","fps10_range","worst_render_us_observed","display_counts","visual_acceptance","runtime_functional_preview_pass","periodic_complete_groups")}|{"failed_guards":[k for k,v in checks.items() if not v]},ensure_ascii=False))
    raise SystemExit(0 if summary["machine_short_preview_pass"] else 1)
if __name__=="__main__":main()
