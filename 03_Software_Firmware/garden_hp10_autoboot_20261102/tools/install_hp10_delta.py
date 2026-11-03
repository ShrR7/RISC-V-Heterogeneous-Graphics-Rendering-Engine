"""Install the reviewed HP10 package over the immediately preceding CBoss image.

No chip erase. Before writing: validate frozen bundle, save two matching full
Flash reads, check exact predecessor, derive complete 4KiB changed sectors.
After writing: byte-verify all 8MiB and restart only the unchanged FPGA image.
"""
from pathlib import Path
import argparse, hashlib, json, os, subprocess, sys
from datetime import datetime, timezone

ROOT = Path(r'E:/SUAT/FoCM/Efinix')
BUNDLE = ROOT/'03_Software_Firmware/garden_hp10_autoboot_20261102'
PREVIOUS = ROOT/'03_Software_Firmware/garden_cboss_autoboot_20261102'
HOME = Path(r'D:/Efinity/2026.1')
PREVIOUS_FULL_SHA = '77dc6dd844362b4f8ed11a86c8b9ffcaba20511ddea43fd647a946cf83456d94'
CAPACITY = 0x800000
START = 0x400000

def digest(data): return hashlib.sha256(data).hexdigest()
def read_json(path): return json.loads(path.read_text(encoding='utf-8-sig'))

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out',type=Path,required=True)
    parser.add_argument('--execute',action='store_true')
    args=parser.parse_args()
    sys.path.insert(0,str(BUNDLE/'tools'))
    import install_autoboot,flash_ops
    validation=install_autoboot.validate_bundle(BUNDLE)
    old=(PREVIOUS/'package/payload.bin').read_bytes()
    new=(BUNDLE/'package/payload.bin').read_bytes()
    delta=read_json(BUNDLE/'package/delta/manifest.json')
    assert len(old)==len(new) and len(new)%4096==0
    assert delta['delta_requires_current_flash_payload_sha256']==digest(old)
    assert delta['sha256']==digest(new)
    changed=[i for i in range(0,len(new),4096) if old[i:i+4096]!=new[i:i+4096]]
    ranges=[]
    for offset in changed:
        if ranges and ranges[-1][1]==offset:ranges[-1][1]+=4096
        else:ranges.append([offset,offset+4096])
    assert changed and sum(e-s for s,e in ranges)==delta['changed_sector_bytes']
    assert [dict(address=START+s,bytes=e-s,sha256=digest(new[s:e])) for s,e in ranges]==[
        {k:r[k] for k in ('address','bytes','sha256')} for r in delta['changed_ranges']]
    plan=dict(bundle=str(BUNDLE),variant=validation['source_variant'],
        expected_previous_full_flash_sha256=PREVIOUS_FULL_SHA,
        expected_previous_payload_sha256=digest(old),new_payload_sha256=digest(new),
        changed_sectors=len(changed),changed_bytes=len(changed)*4096,
        ranges=[dict(address=START+s,bytes=e-s) for s,e in ranges],
        configuration_and_bootloader_preserved=True,chip_erase=False,
        game_elf_downloaded=False,ddr_assets_downloaded=False)
    if not args.execute:
        print(json.dumps(dict(status='PLAN_ONLY',**plan),indent=2));return
    install_autoboot.refuse_debugger()
    out=args.out.resolve()
    assert out.is_relative_to(ROOT/'06_Controller_Dependencies') and not out.exists()
    out.mkdir(parents=True)
    state=dict(status='INSTALLING',started_utc=datetime.now(timezone.utc).isoformat(),
               **plan,steps=[],program_chunks=[],flash_verified=False)
    def save():flash_ops.record(out/'manifest.json',state)
    def run(name,argv,strict=True,env=None):
        rec=dict(name=name,argv=list(map(str,argv)),status='RUNNING')
        state['steps'].append(rec);save();print('STEP '+name,flush=True)
        child_env=dict(os.environ if env is None else env)
        child_env.update(PYTHONUTF8='1',PYTHONIOENCODING='utf-8')
        with (out/(name+'.log')).open('w',encoding='utf-8') as log:
            rc=subprocess.run(rec['argv'],stdout=log,stderr=subprocess.STDOUT,env=child_env).returncode
        rec.update(status='COMPLETE' if rc==0 else 'FAILED',returncode=rc);save()
        if rc and strict:raise RuntimeError(name+' failed; preserved '+str(out/(name+'.log')))
        return rc==0
    common=['--url','ftdi://0x0403:0x6011:0:ff/1','--efinity',str(HOME),
            '--frequency','1000000','--execute']
    def command(operation,address,destination,extra):
        return [sys.executable,str(BUNDLE/'tools/flash_ops.py'),'--operation',operation,
                '--address',hex(address),'--out',str(destination),*extra,*common]
    try:
        backup=out/'backup'
        run('backup_two_full_reads',command('backup',0,backup,['--backup-passes','2']))
        metadata=read_json(backup/'manifest.json')
        original=Path(metadata['backup_binary']).read_bytes()
        assert metadata['status']=='BACKUP_COMPLETE' and metadata['passes_matching']==2
        assert metadata['jedec']=='ef6017' and len(original)==CAPACITY
        assert digest(original)==metadata['backup_sha256']==PREVIOUS_FULL_SHA
        assert original[START:START+len(old)]==old
        expected=bytearray(original);expected[START:START+len(new)]=new;expected=bytes(expected)
        assert expected[:START]==original[:START]
        assert expected[START+len(new):]==original[START+len(new):]
        expected_path=out/'expected_flash_8MiB.bin';expected_path.write_bytes(expected)
        state.update(original_flash_sha256=digest(original),expected_flash_sha256=digest(expected),
                     backup_manifest=str(backup/'manifest.json'));save()
        chunks=out/'delta_chunks';chunks.mkdir()
        for begin,end in ranges:
            for offset in range(begin,end,65536):
                address=START+offset;blob=new[offset:min(end,offset+65536)]
                assert address%4096==0 and len(blob)%4096==0
                assert START<=address<address+len(blob)<=START+len(new)
                source=chunks/('%06X.bin'%address);source.write_bytes(blob)
                rec=dict(address=address,bytes=len(blob),sha256=digest(blob))
                state['program_chunks'].append(rec);save()
                complete=False
                for attempt in (1,2,3):
                    assert install_autoboot.validate_bundle(BUNDLE)==validation
                    assert digest(source.read_bytes())==rec['sha256']
                    tag='program_%06X_%d'%(address,attempt);dest=out/tag
                    ok=run(tag,command('program',address,dest,
                        ['--input',str(source),'--backup-manifest',str(backup/'manifest.json')]),strict=False)
                    if ok:
                        m=read_json(dest/'manifest.json')
                        assert m['status']=='PROGRAM_VERIFIED' and m['readback_sha256']==rec['sha256']
                        rec.update(status='PROGRAM_VERIFIED',attempt=attempt);complete=True;save();break
                    # Vendor SR restore can time out after a write. Verify independently;
                    # do not interpret an error message as proof that data are complete.
                    tag='verify_failed_%06X_%d'%(address,attempt);verify_dir=out/tag
                    if run(tag,command('verify',address,verify_dir,['--input',str(source)]),strict=False):
                        assert (verify_dir/'readback.bin').read_bytes()==blob
                        rec.update(status='INDEPENDENT_VERIFY_AFTER_PROGRAM_ERROR',attempt=attempt)
                        complete=True;save();break
                if not complete:raise RuntimeError('Write/verify failed at '+hex(address))
        run('verify_all_8MiB',command('verify',0,out/'verify_all',['--input',str(expected_path)]))
        verified=read_json(out/'verify_all/manifest.json')
        assert verified['status']=='VERIFY_COMPLETE' and verified['readback_sha256']==digest(expected)
        assert (out/'verify_all/readback.bin').read_bytes()==expected
        state.update(flash_verified=True,actual_full_flash_sha256=digest(expected));save()
        assert install_autoboot.validate_bundle(BUNDLE)==validation
        restart=out/'restart';restart.mkdir();user=restart/'efinity_user';user.mkdir()
        ini=restart/'efinity_user_dir.ini'
        ini.write_text('[General]\nEFINITY_USER_DIR = '+user.as_posix()+'\n',encoding='utf-8')
        env=flash_ops.vendor_environment(HOME)
        env.update(EFINITY_USER_DIR=str(user),EFINITY_USER_DIR_INI=str(ini))
        run('restart_only_FPGA',[str(HOME/'python311/bin/python.exe'),
            str(HOME/'pgm/bin/efx_pgm/ftdi_program.py'),str(BUNDLE/'inputs/fpga.bit'),
            '-m','jtag','-u','ftdi://0x0403:0x6011:0:ff/1','--jtag_clock_freq','1000000'],env=env)
        state.update(status='FLASH_VERIFIED_FPGA_RESTARTED',
                     completed_utc=datetime.now(timezone.utc).isoformat(),true_cold_boot_verified=False)
        save();print(json.dumps(dict(status=state['status'],changed_bytes=state['changed_bytes'],
                                     full_flash_sha256=state['actual_full_flash_sha256'])),flush=True)
    except BaseException as error:
        state.update(status='FAILED',error=repr(error));save();raise

if __name__=='__main__':main()
