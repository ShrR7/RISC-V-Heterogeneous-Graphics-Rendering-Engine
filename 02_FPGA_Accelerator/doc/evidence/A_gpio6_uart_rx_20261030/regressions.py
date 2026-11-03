from pathlib import Path
import json
import shutil
import subprocess
import tempfile

repo = Path('/home/codex/efinix-rtl7-relock')
work = Path(__file__).resolve().parent
cases = ['stage3', 'stage4', 'stage5', 'stage6', 'stage7_alpha',
         'stage7_writeopt', 'stage8_batch', 'stage8_blitbatch']
results = []
with tempfile.TemporaryDirectory(prefix='A-uart-regression-') as temporary:
    target = Path(temporary)
    for directory in ['rtl', 'sim']:
        shutil.copytree(repo / '02_FPGA_Accelerator' / directory, target / directory)
    output = work / 'regressions'
    output.mkdir(exist_ok=True)
    for case in cases:
        with (output / (case + '.log')).open('w') as stream:
            result = subprocess.run(['bash', str(target / 'sim' / ('run_iverilog_' + case + '.sh'))],
                                    stdout=stream, stderr=subprocess.STDOUT, timeout=180)
        text = (output / (case + '.log')).read_text()
        expected = 'WRITEOPT EVAL DONE' if case == 'stage7_writeopt' else 'PASS'
        passed = result.returncode == 0 and (expected in text or 'All tests passed' in text)
        results.append(dict(case=case, exitcode=result.returncode, passed=passed))
        (output / 'summary.json').write_text(json.dumps(results, indent=2) + '\n')
        print(case, result.returncode, passed, flush=True)
        assert passed, case
