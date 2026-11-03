from pathlib import Path
import os,subprocess,json,time,re,shutil,argparse
args=argparse.ArgumentParser();args.add_argument('--project',type=Path);args.add_argument('--log-dir',type=Path,default=Path(__file__).resolve().parent);opt=args.parse_args()
w=opt.log_dir.resolve();w.mkdir(parents=True,exist_ok=True)
p=(opt.project or w.parent/'fpga-gpio6-uart/par/ddr_demo_ti60').resolve()
assert p.is_dir(),p
ef=Path('C:/Efinity');env=dict(os.environ,EFINITY_HOME=ef.as_posix(),EFXPT_HOME=(ef/'pt').as_posix(),EFXPGM_HOME=(ef/'pgm').as_posix(),EFXDBG_HOME=(ef/'debugger').as_posix(),PYTHONHOME=(ef/'python311').as_posix())
env['PATH']=str(ef/'bin')+';'+env['PATH']
for name in ['outflow','work_syn','work_dbg','work_pnr']:(p/name).mkdir(exist_ok=True)
assert not (p/'outflow/ddr_demo_ti60.bit').exists(),'Use a fresh build directory'
syn=[]
options={'allow-const-ram-index':0,'blackbox-error':1,'blast_const_operand_adders':1,'bram_output_regs_packing':1,'create-onehot-fsms':0,'fanout-limit':0,'hdl-compile-unit':1,'infer-clk-enable':3,'infer-sync-set-reset':1,'max_ram':-1,'max_mult':-1,'min-sr-fanout':0,'min-ce-fanout':0,'mult-decomp-retime':0,'mode':'speed','operator-sharing':0,'optimize-adder-tree':0,'optimize-zero-init-rom':1,'retiming':1,'seq_opt':1,'seq-opt-sync-only':0,'dsp-input-regs-packing':1,'dsp-output-regs-packing':1,'dsp-mac-packing':1,'insert-carry-skip':0,'pack-luts-to-comb4':0,'mult-auto-pipeline':0,'use-logic-for-small-mem':64,'use-logic-for-small-rom':64,'bram-push-tco-outreg':0,'hdl-loop-limit':20000,'enable-mark-debug':1,'max-bit-blast-mem-size':10240,'peri-syn-instantiation':0,'peri-syn-inference':0,'ram-decomp-mode':0,'max_threads':-1}
for key,value in options.items():syn+=['--'+key,str(value)]
for rel in ['.','ip/soc','src','src/hdmi']:syn+=['--I',str(p/rel)]
syn+=['--family','Titanium','--device','Ti60F225']
mapexe=str(ef/'bin/efx_map.exe');py=str(ef/'python311/bin/python.exe')
stages=[];started=time.time()
def run(name,args):
    print(name,flush=True);ts=time.time()
    cmd=list(map(str,args))
    with (w/'build.log').open('a') as f:
        f.write('\n===== '+name+' =====\n'+subprocess.list2cmdline(cmd)+'\n');f.flush()
        q=subprocess.run(cmd,cwd=p,env=env,stdout=f,stderr=subprocess.STDOUT)
    stages.append(dict(stage=name,exitcode=q.returncode,seconds=round(time.time()-ts,2)))
    (w/'build-status.json').write_text(json.dumps(stages,indent=2)+'\n')
    if q.returncode:raise RuntimeError((name,q.returncode))
run('1 synthesis',[mapexe,*syn,'--root','ddr3_example_top','--vdb',p/'outflow/ddr_demo_ti60.elab.vdb','--write-efx-verilog',p/'outflow/ddr_demo_ti60.map.v','--write-premap-module',p/'outflow/ddr_demo_ti60.elab.vdb','--gen-mark-debug-json','0','--work-dir',p/'work_syn','--output-dir',p/'outflow','--project-xml',p/'ddr_demo_ti60.xml','--binary-db',p/'outflow/ddr_demo_ti60.vdb'])
run('2 debugger map',[mapexe,*syn,'--keep-gclk-in-module','1','--root','edb_top','--v',p/'work_dbg/debug_top.v','--write-postmap-module',p/'work_dbg/debug_top.post.vdb','--gen-mark-debug-json','0','--work-dir',p/'work_dbg','--output-dir',p/'outflow'])
run('3 debugger merge',[mapexe,*syn,'--gen-mark-debug-json','0','--root','ddr3_example_top','--vdb',p/'outflow/ddr_demo_ti60.elab.vdb','--vdb',p/'work_dbg/debug_top.post.vdb','--write-efx-verilog',p/'work_dbg/ddr_demo_ti60.dbg.map.v','--binary-db',p/'work_dbg/ddr_demo_ti60.dbg.vdb','--merge_vdbs=1','--conn',p/'debug_profile.wizard.json','--work-dir',p/'work_dbg','--output-dir',p/'outflow'])
shutil.copyfile(p/'work_dbg/ddr_demo_ti60.dbg.vdb',p/'outflow/ddr_demo_ti60.dbg.vdb')
run('4a interface jtag',[py,ef/'scripts/efx_pt_jtag_util.py','--device','Ti60F225','--project','ddr_demo_ti60','--design_file',p/'ddr_demo_ti60.peri.xml','--action','add','--jtag_user','JTAG_USER2','--output',p/'ddr_demo_ti60.dbg.peri.xml'])
run('4b interface designer',[py,ef/'scripts/efx_run_pt_unified.py','ddr_demo_ti60.dbg','Titanium','Ti60F225','--peri_file','ddr_demo_ti60.dbg.peri.xml'])
assert (p/'outflow/ddr_demo_ti60.interface.csv').exists()
run('5 place route and timing',[ef/'bin/efx_pnr.exe','--prj',p/'ddr_demo_ti60.xml','--circuit','ddr_demo_ti60','--family','Titanium','--device','Ti60F225','--operating_conditions','I3','--pack','--place','--route','--vdb_file',p/'outflow/ddr_demo_ti60.dbg.vdb','--use_vdb_file','on','--place_file',p/'outflow/ddr_demo_ti60.place','--route_file',p/'outflow/ddr_demo_ti60.troutingtraces','--sdc_file',p/'sdc/ddr3.sdc','--sync_file',p/'outflow/ddr_demo_ti60.interface.csv','--seed','1','--placer_effort_level','2','--max_threads','-1','--beneficial_skew','on','--print_critical_path','10','--work_dir',p/'work_pnr','--output_dir',p/'outflow','--timing_analysis','on'])
timing=(p/'outflow/ddr_demo_ti60.timing.rpt').read_text()
assert 'Using default timing constraint of 1 ns' not in (w/'build.log').read_text(errors='replace')
for clock,period in [('core_clk','10.000'),('hdmi_tx_slow_clk','13.445'),('sdram_clk','2.500'),('rx_cal_clk','2.500'),('tx_cal_clk','2.500'),('tx_cal_clk_90edge','2.500')]:
    assert re.search(r'^'+clock+r'\s+'+re.escape(period)+r'\s+',timing,re.M),clock
setup=timing.split('Setup (Max) Clock Relationship',1)[1].split('Hold (Min) Clock Relationship',1)[0]
hold=timing.split('Hold (Min) Clock Relationship',1)[1].split('----------',1)[0]
def slacks(section):return [float(x) for x in re.findall(r'^\S+\s+\S+\s+[-\d.]+\s+([-\d.]+)\s+\(',section,re.M)]
values=dict(setup=slacks(setup),hold=slacks(hold));assert values['setup'] and values['hold'],values
(w/'timing-summary.json').write_text(json.dumps(values,indent=2)+'\n')
assert min(values['setup'])>=0 and min(values['hold'])>=0,values
run('6 bitstream',[ef/'bin/efx_pgm.exe','--source',p/'work_pnr/ddr_demo_ti60.lbf','--dest',p/'outflow/ddr_demo_ti60.hex','--device','Ti60F225','--family','Titanium','--periph',p/'outflow/ddr_demo_ti60.lpf','--interface_designer_settings',p/'outflow/ddr_demo_ti60_or.ini','--enable_external_master_clock','off','--oscillator_clock_divider','DIV8','--active_capture_clk_edge','negedge','--spi_low_power_mode','on','--io_weak_pullup','on','--enable_roms','smart','--mode','active','--width','1','--release_tri_then_reset','on','--bitstream_compression','on','--jtag_usercode','0xFFFFFFFF'])
assert (p/'outflow/ddr_demo_ti60.bit').exists()
(w/'build-exitcode.txt').write_text('0\n');print('COMPLETE',round(time.time()-started,2),'seconds',flush=True)
