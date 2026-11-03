"""Build the ROM-compatible two-stage loader; never opens a board."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import zlib

HERE=Path(__file__).resolve().parent
TOOLCHAIN=Path('D:/Efinity/efinity-riscv-ide-2026.1/toolchain/bin')
def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--manifest-offset',type=lambda x:int(x,0),default=0x400000)
    p.add_argument('--out',type=Path,default=HERE/'build')
    p.add_argument('--toolchain',type=Path,default=TOOLCHAIN)
    a=p.parse_args();out=a.out.resolve();out.mkdir(parents=True,exist_ok=True)
    if a.manifest_offset&0xFFF or not 0x39F000<=a.manifest_offset<0x800000:
        p.error('manifest-offset must be a 4KB-aligned Flash page after the ROM slot')
    cc=a.toolchain/'riscv-none-elf-gcc.exe'
    objcopy=a.toolchain/'riscv-none-elf-objcopy.exe'
    objdump=a.toolchain/'riscv-none-elf-objdump.exe'
    common=[str(cc),'-march=rv32im_zicsr_zifencei','-mabi=ilp32','-Os','-g',
            '-ffreestanding','-fno-builtin','-fno-stack-protector','-fno-pic',
            '-fno-unwind-tables','-fno-asynchronous-unwind-tables','-msmall-data-limit=0',
            '-Wall','-Wextra','-Werror','-nostdlib','-nostartfiles','-Wl,--no-relax',
            '-Wl,--gc-sections','-ffunction-sections','-fdata-sections']
    commands=[]
    def run(cmd,**kwargs):
        commands.append([str(x) for x in cmd]);r=subprocess.run(cmd,cwd=out,text=True,capture_output=True,**kwargs)
        (out/'build.log').open('a',encoding='utf-8').write('$ '+' '.join(map(str,cmd))+'\n'+r.stdout+r.stderr)
        r.check_returncode();return r
    (out/'build.log').write_text('',encoding='utf-8')
    run(common+[f'-DMANIFEST_FLASH_OFFSET={a.manifest_offset}U',str(HERE/'stage2_start.S'),
        str(HERE/'stage2.c'),str(HERE/'package.c'),'-T',str(HERE/'stage2.ld'),
        '-Wl,-Map='+str(out/'stage2.map'),'-o',str(out/'stage2.elf')])
    run([objcopy,'-O','binary',out/'stage2.elf',out/'stage2.bin'])
    stage2=(out/'stage2.bin').read_bytes();crc=zlib.crc32(stage2)&0xFFFFFFFF
    run(common+[f'-DSTAGE2_BYTES={len(stage2)}',f'-DSTAGE2_CRC32=0x{crc:08X}',
        str(HERE/'stage1.S'),'-T',str(HERE/'stage1.ld'),'-Wl,-Map='+str(out/'stage1.map'),
        '-o',str(out/'stage1.elf')])
    run([objcopy,'-O','binary',out/'stage1.elf',out/'stage1.bin'])
    for stage in ('stage1','stage2'):
        dis=run([objdump,'-d','-h',out/(stage+'.elf')])
        (out/(stage+'.asm')).write_text(dis.stdout,encoding='utf-8')
    stage1=(out/'stage1.bin').read_bytes()
    assert len(stage1)<=0x1F000
    padded=stage1+b'\xFF'*(0x1F000-len(stage1))
    (out/'boot_slot.bin').write_bytes(padded)
    files={name:{'bytes':(out/name).stat().st_size,'sha256':hashlib.sha256((out/name).read_bytes()).hexdigest()}
           for name in ('stage1.elf','stage1.bin','stage2.elf','stage2.bin','boot_slot.bin')}
    result={'schema':1,'architecture':'immutable_ROM_124KB_to_DDR_stage1_relocates_stage2',
        'manifest_flash_offset':a.manifest_offset,'bootstrap_flash_offset':0x380000,
        'bootstrap_load_address':0x1000,'bootstrap_slot_bytes':0x1F000,
        'stage2_address':0xC00000,'stage2_stack_top':0xC10000,'probe_address':0xC1F000,
        'stage2_crc32':crc,'no_rtl_or_original_sources_modified':True,
        'files':files,'commands':commands}
    (out/'build_manifest.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
    print(json.dumps({'out':str(out),'stage1_bytes':len(stage1),'stage2_bytes':len(stage2),
                      'stage2_crc32':hex(crc),'boot_slot_bytes':len(padded)}))
if __name__=='__main__':main()
