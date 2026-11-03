from pathlib import Path
import hashlib
import json
import re
import shutil
import subprocess
import zipfile

repo = Path('/home/codex/efinix-rtl7-relock')
work = Path(__file__).resolve().parent
project = work.parent / 'fpga-gpio6-uart/par/ddr_demo_ti60'
prefix = '01_Hardware_Platform/08_ti60f225_soc_demo/09_Ti60F225_hardjtag_demo/'
evidence = repo / '02_FPGA_Accelerator/doc/evidence/A_gpio6_uart_rx_20261030'
evidence.mkdir(parents=True, exist_ok=True)
git = lambda *args: subprocess.check_output(['git', *args], cwd=repo, text=True).strip()
head = git('rev-parse', 'HEAD')
manifest = json.loads((work / 'source-manifest.json').read_text())
manifest['source_commit'] = head
for row in manifest['files']:
    current = repo / row['path']
    frozen = project.parents[1] / row['build_path']
    assert hashlib.sha256(current.read_bytes()).hexdigest() == row['sha256'], current
    assert frozen.read_bytes() == current.read_bytes(), frozen
stages = json.loads((work / 'build/build-status.json').read_text())
assert stages[-1]['stage'] == '6 bitstream' and all(row['exitcode'] == 0 for row in stages)
timing = json.loads((work / 'build/timing-summary.json').read_text())
assert min(timing['setup']) >= 0 and min(timing['hold']) >= 0
assert '882 checks' in (work / 'sim.log').read_text()
assert all(row['passed'] for row in json.loads((work / 'regressions/summary.json').read_text()))
log = (work / 'build/build.log').read_text(errors='replace')
assert 'Using default timing constraint of 1 ns' not in log
uart_warnings = [line for line in log.splitlines() if 'platform_uart_rx' in line and '[EFX-0657]' in line]
assert not uart_warnings
plain_map = project / 'outflow/ddr_demo_ti60.map.v'
assert re.search(r'EFX_RAM10\s+\\u_apb3_top/u_platform_uart_rx/fifo_mem\s', plain_map.read_text())
bit = project / 'outflow/ddr_demo_ti60.bit'
resources = {key: int(value) for key, value in re.findall(r'EFX_(LUT4|FF|RAM10|DSP24)\s*:\s*(\d+)', log)}
xlr = int(re.findall(r'XLR count after merging:\s*(\d+)', log)[-1])
result = dict(profile='RTL19-U6RX1', base=manifest['base'], source_commit=head,
              uart_id='55365258', uart_revision=1, uart_base='F8100900',
              BUL9_revision='00010007', clock_hz=100000000, baud=115200,
              fifo_bytes=256, uart_RAM10=1, uart_EFX_0657=0,
              source_inputs=len(manifest['files']), resources=resources, XLR=xlr,
              timing=timing, bit_bytes=bit.stat().st_size,
              bit_md5=hashlib.md5(bit.read_bytes()).hexdigest().upper(),
              bit_sha256=hashlib.sha256(bit.read_bytes()).hexdigest(),
              wrapper_sim_checks=882, board_test=False,
              B_C_canonical_changed=False)
assert xlr <= 60800 and resources['RAM10'] <= 256
(evidence / 'RESULTS.json').write_text(json.dumps(result, indent=2) + '\n')
(evidence / 'source-manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
for name in ['prepare.py', 'build.py', 'regressions.py', 'package.py', 'sim.log', 'compile.log']:
    shutil.copyfile(work / name, evidence / name)
for directory in ['build', 'regressions']:
    shutil.copytree(work / directory, evidence / directory, dirs_exist_ok=True)
for source in (project / 'outflow').glob('*.rpt'):
    shutil.copyfile(source, evidence / source.name)
for name in ['ddr_demo_ti60.bit', 'ddr_demo_ti60.hex']:
    shutil.copyfile(project / 'outflow' / name, evidence / name)
with zipfile.ZipFile(evidence / 'maps.zip', 'w', zipfile.ZIP_DEFLATED) as archive:
    for source in [plain_map, project / 'work_dbg/ddr_demo_ti60.dbg.map.v']:
        archive.write(source, source.name)
with zipfile.ZipFile(evidence / 'source-inputs.zip', 'w', zipfile.ZIP_DEFLATED) as archive:
    for row in manifest['files']:
        archive.write(project.parents[1] / row['build_path'], row['build_path'])
    archive.write(evidence / 'source-manifest.json', 'SOURCE_INPUTS.json')
    for name in ['tb_platform_uart_rx.v', 'run_iverilog_platform_uart_rx.sh']:
        archive.write(repo / '02_FPGA_Accelerator/sim' / name, 'sim/' + name)
production = repo / prefix / 'par/ddr_demo_ti60'
for source in [bit, project / 'outflow/ddr_demo_ti60.hex', plain_map,
               project / 'work_dbg/ddr_demo_ti60.dbg.map.v',
               project / 'outflow/ddr_demo_ti60.timing.rpt']:
    shutil.copyfile(source, production / 'outflow' / source.name)
note = f'''# A GPIO6 UART RX 同批交付（2026-10-30）

基线 `{manifest['base']}`，输入源码提交 `{head}`，共99份实际构建输入。配置为生产 RTL19 + GPIO6 UART，未切换 Stage10 候选；BUL9 revision仍为00010007，旧GPU/C软件接口不变。新增U6RX版本1，寄存器合同见 [A_gpio6_uart_rx.md](A_gpio6_uart_rx.md)。

bit **RTL19-U6RX1**：MD5 **`{result['bit_md5']}`**，{result['bit_bytes']}字节，SHA256 `{result['bit_sha256']}`。

同批交付目录 [evidence/A_gpio6_uart_rx_20261030](evidence/A_gpio6_uart_rx_20261030)：bit、hex、maps.zip内plain/debug两份map、source-inputs.zip内99份实际输入及测试、完整构建/回归日志、最终STA报告、输入清单和SHA256SUMS。生产outflow的bit/hex/plain-map/debug-map/timing也同步本批。C variants中的历史冻结bit/ELF不修改，需C显式选择新bit。

Efinity2026.1.132、Ti60F225/I3，沿用RTL19 speed/retiming1/COMB4关闭、seed1配置及SDC，重新综合/布局/布线/时序后生成bit。XLR **{xlr}/60800**；映射LUT4 {resources['LUT4']}、FF {resources['FF']}、RAM10 **{resources['RAM10']}/256**、DSP24 {resources['DSP24']}。FIFO独占1 RAM10、无相关EFX-0657；工程原有小型控制阵列告警保留。

全部六个期望时钟/约束核对通过，无1ns默认约束回退，最终setup/hold均通过（最小setup {min(timing['setup']):+.3f}ns，最小hold {min(timing['hold']):+.3f}ns）。具体core与其他时钟结果见完整timing.rpt，未继承旧版结论。

真实APB wrapper联合仿真882项通过；Stage3/4/5/6/7Alpha/7writeopt/8FillBatch/8BlitBatch八配置回归通过。未修改B/C原始RTL、游戏或默认入口。

@B：新增A独立外设，不占GPU寄存器/AXI或DDR带宽；B引擎不变。

@C：GPIO6/J7第40脚接外部3.3V TX并共地，115200 8N1。先读0xF8100900应为55365258，再读0xF8100904（bit31有效，低8位数据，读弹出），COUNT/OVERFLOW/FRAME_ERROR分别在0908/090C/0910。FIFO256字节，满时丢新保旧，建议每帧排空并监测错误计数。

本轮未操作板卡、未接外部UART发送器；没有新增实板UART/600秒/显示0/0/0结论。
'''
(repo / '02_FPGA_Accelerator/doc/A_gpio6_uart_delivery_20261030.md').write_text(note)
(evidence / 'SHA256SUMS.txt').write_text(''.join(
    hashlib.sha256(source.read_bytes()).hexdigest() + '  ' + source.relative_to(evidence).as_posix() + '\n'
    for source in sorted(evidence.rglob('*')) if source.is_file() and source.name != 'SHA256SUMS.txt'))
(work / 'RESULTS.json').write_text(json.dumps(result, indent=2) + '\n')
print(json.dumps(result, indent=2), flush=True)
