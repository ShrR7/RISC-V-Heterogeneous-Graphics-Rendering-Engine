"""Offline Efinity SPI HEX decoding, using the installed official converter."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys
from flash_ops import read_hex, vendor_environment, record


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--bit',required=True)
    p.add_argument('--hex',required=True)
    p.add_argument('--out',required=True)
    p.add_argument('--efinity',default='D:/Efinity/2026.1')
    p.add_argument('--vendor-worker',action='store_true')
    args=p.parse_args()
    home=Path(args.efinity).resolve()
    if not args.vendor_worker:
        return subprocess.run([str(home/'python311/bin/python.exe'),str(Path(__file__).resolve()),
                               *sys.argv[1:],'--vendor-worker'],env=vendor_environment(home)).returncode
    from efx_pgm.header_processor import HeaderProcessor
    from efx_pgm.util.bitstream_util import get_device_db,get_bitstream_num_bytes
    from efx_pgm.export_bitstream import BitstreamExport
    out=Path(args.out).resolve()
    out.mkdir(parents=True,exist_ok=False)
    entries={}
    db=get_device_db()
    for kind,path in (('jtag_bit',Path(args.bit).resolve()),('spi_hex',Path(args.hex).resolve())):
        hp=HeaderProcessor(str(path),device_db=db)
        hp.process_header()
        raw=read_hex(path)
        entry=dict(source=str(path),text_file_bytes=path.stat().st_size,
                   source_md5=hashlib.md5(path.read_bytes()).hexdigest(),
                   source_sha256=hashlib.sha256(path.read_bytes()).hexdigest(),
                   physical_bytes=len(raw),official_num_bytes=get_bitstream_num_bytes(str(path)),
                   die=hp.die_name,mode=hp.get_prog_mode(),width=hp.get_prog_width(),
                   padded_bits=hp.num_padded_bits,header=hp.header_text,
                   sync_pattern=hp.sync_pattern)
        raw_path=out/(kind+'.bin')
        BitstreamExport().convert_hex_to_bin(str(path),str(raw_path))
        assert raw_path.read_bytes()==raw
        assert len(raw)==entry['official_num_bytes']
        entry.update(raw_path=str(raw_path),raw_sha256=hashlib.sha256(raw).hexdigest())
        entries[kind]=entry
    spi=entries['spi_hex']
    end=(spi['physical_bytes']+4095)//4096*4096
    result=dict(hardware_access=False,configuration_entries=entries,
                configuration_flash_start=0,configuration_flash_end=spi['physical_bytes'],
                configuration_erase_end_4KiB=end,
                bootstrap_start=0x380000,bootstrap_reserve_end=0x39F000,
                payload_start=0x400000,
                config_does_not_overlap_bootstrap=end<=0x380000,
                notes='Use paired SPI HEX for persistent configuration; do not substitute JTAG BIT or its raw bytes.')
    record(out/'manifest.json',result)
    print(json.dumps(result,ensure_ascii=True,indent=2))
    return 0


if __name__=='__main__':
    raise SystemExit(main())
