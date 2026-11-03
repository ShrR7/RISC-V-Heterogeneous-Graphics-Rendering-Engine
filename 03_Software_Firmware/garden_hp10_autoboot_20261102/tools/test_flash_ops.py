"""Host-only checks for the Flash wrapper's destructive-action guards."""
import contextlib
import io
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
import flash_ops as ops


class FlashGuardChecks(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory(prefix='flash_host_',dir=Path(__file__).resolve().parent)
        self.addCleanup(self.temp.cleanup)
        self.root=Path(self.temp.name)
        self.out=self.root/'new_evidence'

    def args(self,operation='backup',extra=()):
        return ops.parser().parse_args(['--operation',operation,'--url','ftdi://fixture/1','--out',str(self.out),*extra])

    def write_backup(self):
        original=self.root/'original.bin'
        original.write_bytes(b'\xff'*ops.CAPACITY)
        manifest=self.root/'backup.json'
        ops.record(manifest,dict(status='BACKUP_COMPLETE',jedec=ops.EXPECTED_JEDEC,
            capacity=ops.CAPACITY,backup_binary=str(original),backup_sha256=ops.sha(original.read_bytes())))
        return original,manifest

    def test_default_backup_plan_is_nonpersistent_and_does_not_import_vendor(self):
        p=ops.make_plan(self.args())
        self.assertFalse(p['hardware_access'])
        self.assertFalse(p['flash_persistent_write'])
        self.assertFalse(p['flash_erase_entire'])
        self.assertEqual(p['backup_passes'],2)
        self.assertEqual(p['expected_capacity'],8388608)

    def test_dryrun_creates_no_output_and_spawns_no_process(self):
        with patch('subprocess.run',side_effect=AssertionError('No child in dryrun')), contextlib.redirect_stdout(io.StringIO()) as captured:
            self.assertEqual(ops.main(['--operation','backup','--url','ftdi://fixture/1','--out',str(self.out)]),0)
        self.assertFalse(self.out.exists())
        self.assertFalse(json.loads(captured.getvalue())['hardware_access'])

    def test_strict_hex_and_roundtrip(self):
        p=self.root/'payload.hex'
        data=bytes(range(256))
        ops.write_hex(p,data)
        self.assertEqual(ops.read_hex(p),data)
        self.assertEqual(p.stat().st_size,768)

    def test_intel_hex_rejected(self):
        p=self.root/'intel.hex';p.write_text(':00000001FF\n',encoding='ascii')
        with self.assertRaises(ValueError):ops.read_hex(p)

    def test_multibyte_line_rejected(self):
        p=self.root/'bad.hex';p.write_text('1234\n',encoding='ascii')
        with self.assertRaises(ValueError):ops.read_hex(p)

    def test_empty_hex_rejected(self):
        p=self.root/'empty.hex';p.write_text('',encoding='ascii')
        with self.assertRaises(ValueError):ops.read_hex(p)

    def test_read_past_capacity_rejected(self):
        with self.assertRaises(ValueError):ops.make_plan(self.args('read',['--address','0x7fff00','--length','257']))

    def test_read_negative_rejected(self):
        with self.assertRaises(ValueError):ops.make_plan(self.args('read',['--address','-1']))

    def test_read_zero_rejected(self):
        with self.assertRaises(ValueError):ops.make_plan(self.args('read',['--length','0']))

    def test_program_requires_backup(self):
        p=self.root/'input.bin';p.write_bytes(b'a'*4096)
        with self.assertRaisesRegex(ValueError,'backup-manifest'):ops.make_plan(self.args('program',['--input',str(p)]))

    def test_program_rejects_unaligned_address(self):
        _,b=self.write_backup();p=self.root/'input.bin';p.write_bytes(b'a'*4096)
        with self.assertRaisesRegex(ValueError,'address must'):ops.make_plan(self.args('program',['--input',str(p),'--address','1','--backup-manifest',str(b)]))

    def test_program_rejects_overflow(self):
        _,b=self.write_backup();p=self.root/'input.bin';p.write_bytes(b'a'*8192)
        with self.assertRaises(ValueError):ops.make_plan(self.args('program',['--input',str(p),'--address','0x7ff000','--backup-manifest',str(b)]))

    def test_program_rejects_modified_original_backup(self):
        original,b=self.write_backup();p=self.root/'input.bin';p.write_bytes(b'a'*4096)
        with original.open('r+b') as f:f.write(b'\x00')
        with self.assertRaises(ValueError):ops.make_plan(self.args('program',['--input',str(p),'--backup-manifest',str(b)]))

    def test_program_exact_sector_envelope(self):
        _,b=self.write_backup();p=self.root/'input.bin';p.write_bytes(b'a'*8192)
        r=ops.make_plan(self.args('program',['--input',str(p),'--address','0x380000','--backup-manifest',str(b)]))
        self.assertEqual((r['erase_start'],r['erase_end']),(0x380000,0x382000))
        self.assertEqual(r['input_bytes'],8192)
        self.assertFalse(r['flash_persistent_write'])

    def test_program_partial_tail_sector_rejected(self):
        _,b=self.write_backup();p=self.root/'input.bin';p.write_bytes(b'a'*4097)
        with self.assertRaisesRegex(ValueError,'complete 4 KiB sectors'):ops.make_plan(self.args('program',['--input',str(p),'--address','0x380000','--backup-manifest',str(b)]))

    def test_program_jtag_bit_rejected(self):
        _,b=self.write_backup();p=self.root/'input.bit';ops.write_hex(p,b'a'*4096)
        with self.assertRaisesRegex(ValueError,'never a JTAG'):ops.make_plan(self.args('program',['--input',str(p),'--backup-manifest',str(b)]))

    def test_missing_input_rejected(self):
        with self.assertRaises(ValueError):ops.make_plan(self.args('verify'))

    def test_backup_must_be_complete(self):
        _,b=self.write_backup();p=self.root/'input.bin';p.write_bytes(b'a'*4096)
        m=json.loads(b.read_text());m['status']='FAILED';ops.record(b,m)
        with self.assertRaises(ValueError):ops.make_plan(self.args('program',['--input',str(p),'--backup-manifest',str(b)]))

    def test_backup_must_be_same_jedec(self):
        _,b=self.write_backup();p=self.root/'input.bin';p.write_bytes(b'a'*4096)
        m=json.loads(b.read_text());m['jedec']='c84017';ops.record(b,m)
        with self.assertRaises(ValueError):ops.make_plan(self.args('program',['--input',str(p),'--backup-manifest',str(b)]))

    def test_child_environment_is_not_global_mutation(self):
        old=dict(ops.os.environ)
        env=ops.vendor_environment(Path('D:/Efinity/2026.1'))
        self.assertEqual(dict(ops.os.environ),old)
        self.assertIn('python311',env['PYTHONHOME'])

    def test_chunks_cover_exact_range_and_partial_read_tail(self):
        calls=[]
        class FakeProgram:
            def jtag2spi_read_flash(self,address,length,path,setup,selected):
                calls.append((address,length))
                ops.write_hex(path,bytes(((address+i)%251 for i in range(length))))
                return bytes.fromhex(ops.EXPECTED_JEDEC)
        self.out.mkdir()
        with contextlib.redirect_stdout(io.StringIO()):
            data,entries=ops.read_range(FakeProgram(),0x380000,8192+123,self.out,'chunks',4096)
        self.assertEqual(calls,[(0x380000,4096),(0x381000,4096),(0x382000,123)])
        self.assertEqual(data,bytes(((0x380000+i)%251 for i in range(8192+123))))
        self.assertEqual(len(entries),3)
        self.assertEqual(json.loads((self.out/'chunks/progress.json').read_text())['status'],'READ_COMPLETE')

    def test_chunk_identity_change_preserves_only_completed_evidence(self):
        class FakeProgram:
            calls=0
            def jtag2spi_read_flash(self,address,length,path,setup,selected):
                self.calls+=1
                ops.write_hex(path,b'\xff'*length)
                return bytes.fromhex(ops.EXPECTED_JEDEC if self.calls==1 else 'c86017')
        self.out.mkdir()
        with contextlib.redirect_stdout(io.StringIO()):
            with self.assertRaisesRegex(RuntimeError,'ID changed'):
                ops.read_range(FakeProgram(),0,8192,self.out,'chunks',4096)
        progress=json.loads((self.out/'chunks/progress.json').read_text())
        self.assertEqual(progress['status'],'READING')
        self.assertEqual(progress['completed_bytes'],4096)

    def test_invalid_chunk_size_rejected(self):
        for size in (0,4095,65537,131072):
            with self.subTest(size=size),self.assertRaises(ValueError):
                ops.make_plan(self.args('backup',['--chunk-bytes',str(size)]))

    def program_worker_fixture(self,corrupt=False):
        _,backup=self.write_backup()
        payload=self.root/'program.bin'
        content=bytes((i%251 for i in range(8192)))
        payload.write_bytes(content)
        calls=[]
        class FakeProgram:
            input_file=''
            memory=bytearray(b'\xff'*ops.CAPACITY)
            def jtag2spi_read_flash(self,address,length,path,setup,selected):
                data=bytearray(self.memory[address:address+length])
                if corrupt and address==0x380000:data[0]^=1
                ops.write_hex(path,data)
                return bytes.fromhex(ops.EXPECTED_JEDEC)
            def jtag2spi_program(self,setup,address,entire,erase,selected,method):
                calls.append((setup,address,entire,erase,selected,method))
                data=ops.load_input(self.input_file)
                self.memory[address:address+len(data)]=data
                return bytes.fromhex(ops.EXPECTED_JEDEC)
        args=self.args('program',['--input',str(payload),'--address','0x380000',
            '--backup-manifest',str(backup),'--chunk-bytes','4096','--execute','--vendor-worker'])
        with patch.dict(ops.os.environ,{},clear=False),patch.object(ops,'get_vendor_program',return_value=FakeProgram()),patch('subprocess.run',return_value=type('Finished',(),{'returncode':0})()),contextlib.redirect_stdout(io.StringIO()):
            if corrupt:
                with self.assertRaisesRegex(RuntimeError,'byte comparison'):ops.vendor_worker(args,ops.make_plan(args))
            else:
                self.assertEqual(ops.vendor_worker(args,ops.make_plan(args)),0)
        return calls,json.loads((self.out/'manifest.json').read_text()),content

    def test_mocked_program_uses_bounded_whole_sectors_and_full_readback(self):
        calls,m,content=self.program_worker_fixture()
        self.assertEqual(calls,[(True,0x380000,False,True,'lower','hostx1'),(True,0x381000,False,True,'lower','hostx1')])
        self.assertEqual(m['status'],'PROGRAM_VERIFIED')
        self.assertEqual((self.out/'readback.bin').read_bytes(),content)

    def test_mocked_program_independent_byte_mismatch_cannot_pass(self):
        _,m,_=self.program_worker_fixture(corrupt=True)
        self.assertEqual(m['status'],'FAILED')

    def test_mocked_backup_validates_two_reads_and_isolates_vendor_settings(self):
        class FakeProgram:
            def jtag2spi_read_flash(self,address,length,path,setup,selected):
                self_args=(address,setup,selected)
                if self_args != (0,True,'lower'):raise AssertionError(self_args)
                ops.write_hex(path,bytes((i%256 for i in range(length))))
                return bytes.fromhex(ops.EXPECTED_JEDEC)
        def fake_cli(command,**kwargs):
            self.assertIn('ftdi_program.py',command[1])
            ini=Path(ops.os.environ['EFINITY_USER_DIR_INI'])
            self.assertTrue(ini.is_relative_to(self.out))
            self.assertIn('EFINITY_USER_DIR = ',ini.read_text())
            return type('Finished',(),{'returncode':0})()
        with patch.dict(ops.os.environ,{},clear=False),patch.object(ops,'CAPACITY',4096),patch.object(ops,'get_vendor_program',return_value=FakeProgram()),patch('subprocess.run',side_effect=fake_cli),contextlib.redirect_stdout(io.StringIO()):
            args=self.args('backup',['--execute','--vendor-worker'])
            self.assertEqual(ops.vendor_worker(args,ops.make_plan(args)),0)
        m=json.loads((self.out/'manifest.json').read_text())
        self.assertEqual(m['status'],'BACKUP_COMPLETE')
        self.assertEqual(m['passes_matching'],2)
        self.assertEqual(Path(m['backup_binary']).stat().st_size,4096)

    def test_mocked_backup_mismatch_never_marks_complete(self):
        class FakeProgram:
            calls=0
            def jtag2spi_read_flash(self,address,length,path,setup,selected):
                self.calls+=1
                data=bytearray(b'\xff'*length)
                if self.calls==3:data[-1]=0
                ops.write_hex(path,data)
                return bytes.fromhex(ops.EXPECTED_JEDEC)
        with patch.dict(ops.os.environ,{},clear=False),patch.object(ops,'CAPACITY',4096),patch.object(ops,'get_vendor_program',return_value=FakeProgram()),patch('subprocess.run',return_value=type('Finished',(),{'returncode':0})()),contextlib.redirect_stdout(io.StringIO()):
            args=self.args('backup',['--execute','--vendor-worker'])
            with self.assertRaises(RuntimeError):ops.vendor_worker(args,ops.make_plan(args))
        m=json.loads((self.out/'manifest.json').read_text())
        self.assertEqual(m['status'],'FAILED')
        self.assertFalse((self.out/'original_flash_8MiB.bin').exists())


if __name__=='__main__':
    unittest.main(verbosity=2)
