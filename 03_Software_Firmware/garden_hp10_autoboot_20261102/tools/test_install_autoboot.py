"""Host-only installer checks; all native execution and sockets are mocked."""
import contextlib
import io
import json
from pathlib import Path
import shutil
import socket
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch
import install_autoboot as install


class InstallerChecks(unittest.TestCase):
    def setUp(self):
        self.bundle=Path(__file__).resolve().parent.parent
        self.temp=tempfile.TemporaryDirectory(prefix='garden_installer_host_',dir=self.bundle.parent)
        self.addCleanup(self.temp.cleanup)
        self.base=Path(self.temp.name)
        self.out=self.base/'install'

    def args(self):
        return SimpleNamespace(bundle=str(self.bundle),efinity='D:/Efinity/2026.1',
            url='ftdi://fixture/1',frequency=3000000)

    def test_real_frozen_bundle_validates(self):
        result=install.validate_bundle(self.bundle)
        self.assertEqual(result['default_mode'],'AUTO_game')
        self.assertTrue(result['retain_T_test'])
        self.assertGreater(len(result['files']),30)

    def test_default_dryrun_opens_no_socket_or_process_and_creates_no_log(self):
        with patch('subprocess.Popen',side_effect=AssertionError('No process')),patch('subprocess.run',side_effect=AssertionError('No child')),patch('socket.create_connection',side_effect=AssertionError('No sockets')),contextlib.redirect_stdout(io.StringIO()) as output:
            self.assertEqual(install.main(['--out',str(self.out)]),0)
        self.assertEqual(json.loads(output.getvalue())['status'],'DRY_RUN_NO_HARDWARE')
        self.assertFalse(self.out.exists())

    def test_open_debugger_refused_without_killing(self):
        class Connection:
            def __enter__(self):return self
            def __exit__(self,*args):return False
        with patch('socket.create_connection',return_value=Connection()):
            with self.assertRaisesRegex(RuntimeError,'no process was killed'):
                install.refuse_debugger()

    def test_closed_debugger_is_allowed(self):
        with patch('socket.create_connection',side_effect=ConnectionRefusedError()):
            install.refuse_debugger()

    def test_manifest_path_escape_rejected(self):
        with self.assertRaises(ValueError):install.inside(self.base,'../escape.bin')

    def test_relocation_and_tamper_guard(self):
        copied=self.base/'relocated_bundle'
        shutil.copytree(self.bundle,copied,ignore=shutil.ignore_patterns('logs','__pycache__'))
        with patch.object(install,'__file__',str(copied/'tools/install_autoboot.py')):
            self.assertEqual(install.validate_bundle(copied)['bundle'],str(copied.resolve()))
            game=copied/'inputs/game.bin'
            data=bytearray(game.read_bytes());data[0]^=1;game.write_bytes(data)
            with self.assertRaisesRegex(ValueError,'Frozen input changed: game.bin'):
                install.validate_bundle(copied)

    def mock_pipeline(self,fail=None,unaligned=False):
        calls=[]
        validation={'fixture':'immutable'}
        def step(state,out,name,argv,env=None):
            calls.append((name,[str(x) for x in argv]))
            state['steps'].append(dict(name=name,status='COMPLETE'))
            if name==fail:raise RuntimeError('fixture failure '+name)
            if name=='01_backup':
                (out/'backup').mkdir()
                install.flash_ops.record(out/'backup/manifest.json',dict(status='BACKUP_COMPLETE'))
            if name=='02_assemble':
                assembled=out/'assembled';assembled.mkdir()
                group=b'\xa5'*(4097 if unaligned else 4096)
                (assembled/'region.bin').write_bytes(group)
                expected=assembled/'expected_flash_8MiB.bin'
                expected.write_bytes(b'\xff'*install.CAPACITY)
                install.flash_ops.record(assembled/'manifest.json',dict(capacity=install.CAPACITY,
                    all_other_bytes_preserved=True,flash_chip_erase=False,expected_jedec='ef6017',
                    expected_flash_sha256=install.digest(expected),program_regions=[dict(
                        address=0x380000,file='region.bin',bytes=len(group),sha256=install.digest(assembled/'region.bin'))]))
        with patch.object(install,'validate_bundle',return_value=validation),patch.object(install,'refuse_debugger'),patch.object(install,'run_step',side_effect=step),patch('subprocess.Popen',side_effect=AssertionError('Hardware subprocess forbidden')),patch('socket.create_connection',side_effect=AssertionError('Hardware sockets forbidden')),contextlib.redirect_stdout(io.StringIO()):
            if fail or unaligned:
                with self.assertRaises(RuntimeError):install.execute(self.args(),validation,self.out)
            else:
                self.assertEqual(install.execute(self.args(),validation,self.out),0)
        return calls,install.read_json(self.out/'install_manifest.json')

    def test_mock_pipeline_order_and_no_game_download(self):
        calls,result=self.mock_pipeline()
        self.assertEqual([n for n,_ in calls],['01_backup','02_assemble','03_program_1','04_verify_all','05_restart_FPGA'])
        backup=calls[0][1]
        self.assertEqual(backup[backup.index('--backup-passes')+1],'2')
        restart=calls[-1][1]
        self.assertEqual(restart[restart.index('-m')+1],'jtag')
        self.assertIn(str(self.bundle/'inputs/fpga.bit'),restart)
        self.assertFalse(any('gdb' in arg.lower() or arg.endswith('.elf') for _,argv in calls for arg in argv))
        self.assertEqual(result['status'],'FLASH_VERIFIED_FPGA_RESTARTED')
        self.assertTrue(result['flash_verified'])
        self.assertFalse(result['true_cold_boot_verified'])
        self.assertFalse(result['DDR_assets_downloaded'])

    def test_mock_backup_failure_never_programs(self):
        calls,result=self.mock_pipeline(fail='01_backup')
        self.assertEqual(len(calls),1)
        self.assertEqual(result['status'],'FAILED')
        self.assertFalse(result['flash_verified'])

    def test_mock_misaligned_region_never_programs(self):
        calls,result=self.mock_pipeline(unaligned=True)
        self.assertEqual([n for n,_ in calls],['01_backup','02_assemble'])
        self.assertEqual(result['status'],'FAILED')

    def test_mock_verify_failure_never_restarts(self):
        calls,result=self.mock_pipeline(fail='04_verify_all')
        self.assertNotIn('05_restart_FPGA',[n for n,_ in calls])
        self.assertFalse(result['flash_verified'])

    def test_mock_restart_failure_keeps_verified_flash_state(self):
        _,result=self.mock_pipeline(fail='05_restart_FPGA')
        self.assertEqual(result['status'],'FLASH_VERIFIED_RESTART_FAILED')
        self.assertTrue(result['flash_verified'])
        self.assertFalse(result['true_cold_boot_verified'])


if __name__=='__main__':
    unittest.main(verbosity=2)
