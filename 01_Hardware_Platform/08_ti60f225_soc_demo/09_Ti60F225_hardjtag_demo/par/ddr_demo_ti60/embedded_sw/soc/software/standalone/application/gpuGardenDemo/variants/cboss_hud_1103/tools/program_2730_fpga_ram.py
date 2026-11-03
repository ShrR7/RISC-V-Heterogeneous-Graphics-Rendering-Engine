"""Volatile programming of physical USB port10 only; port7 is excluded.
No Flash operations and no enumeration that opens the protected device.
"""
from pathlib import Path
import argparse,hashlib,json,runpy,sys,os,subprocess
HERE=Path(__file__).resolve().parents[1]/'logs/volatile_fpga_2730'
HERE.mkdir(parents=True,exist_ok=True)
H=Path(r'D:/Efinity/2026.1')
# Efinity's native grpc extension must use its own Python ABI. Keep SDK
# configuration in this demo's workspace; don't edit user Efinity settings.
sdk_env=os.environ.copy()
sdk_env.update(PYTHONHOME=str(H/'python311'),EFINITY_HOME=H.as_posix(),
               EFXPGM_HOME=(H/'pgm').as_posix(),EFXDBG_HOME=(H/'debugger').as_posix(),
               EFXPT_HOME=(H/'pt').as_posix(),EFXIPM_HOME=(H/'ipm').as_posix(),
               EFXSVF_HOME=(H/'debugger/svf_player').as_posix(),
               EFXSERDESDBG_HOME=(H/'debugger/serdes_debug_tool').as_posix(),
               EFINITY_USER_DIR=str(Path(os.environ['USERPROFILE'])/'.efinity'))
sdk_env['PATH']=str(H/'python311/bin')+';'+str(H/'python311/DLLs')+';'+sdk_env.get('PATH','')
ini=HERE/'efinity_user_dir.ini'
ini.write_text('[General]\nEFINITY_USER_DIR='+sdk_env['EFINITY_USER_DIR']+'\n',encoding='utf-8')
sdk_env['EFINITY_USER_DIR_INI']=str(ini)
if sys.version_info[:2]!=(3,11):
    raise SystemExit(subprocess.call([str(H/'python311/bin/python.exe'),str(Path(__file__).resolve()),*sys.argv[1:]],env=sdk_env))
os.environ.update(sdk_env)
sys.path[:0]=[str(H/'python311/Lib/site-packages'),str(H/'pgm/bin'),str(H/'debugger/bin')]
import usb.backend.libusb1
backend=usb.backend.libusb1.get_backend(find_library=lambda _:str(H/'debugger/openocd/bin/libusb-1.0.dll'))
assert backend is not None
raw_devices=list(backend.enumerate_devices())
selected=[];summary=[]
for raw in raw_devices:
 d=backend.get_device_descriptor(raw)
 if (d.idVendor,d.idProduct)!=(0x0403,0x6011):continue
 ports=list(d.port_numbers or [])
 summary.append(dict(bus=d.bus,address=d.address,ports=ports))
 if ports==[10]:selected.append((raw,d))
assert len(selected)==1,'Game board physical USB10 not uniquely identified'
raw,d=selected[0]
assert list(d.port_numbers)==[10]
url=f'ftdi://0x0403:0x6011:{d.bus:x}:{d.address:x}/1'
record=dict(new_board_uart='COM11',new_board_usb_path=f'{d.bus}-10',url=url,
 protected_uart='COM25',protected_usb_port=7,enumerated=summary,
 persistent_flash_write=False,backend='libusb1 pinned DLL; subsequent enumeration restricted to USB10')
(HERE/'selected_target.json').write_text(json.dumps(record,indent=2),encoding='utf-8')
print(json.dumps(record),flush=True)
parser=argparse.ArgumentParser();parser.add_argument('--bit',type=Path);args=parser.parse_args()
if args.bit is None:raise SystemExit(0)
bit=args.bit.resolve()
assert hashlib.md5(bit.read_bytes()).hexdigest()=='d59183e81a58833b1d550a91ebbe5e23'
# Filtering happens before pyftdi/Efinity discovery can read string descriptors.
# The excluded original device is never opened by this process.
backend.enumerate_devices=lambda:iter([raw])
from pyftdi.usbtools import UsbTools
UsbTools.BACKENDS=('usb.backend.libusb1',)
from efx_pgm.usb_resolver import UsbResolver
resolved=UsbResolver().get_usb_connections()
print('WHITELISTED_TARGETS '+json.dumps([dict(bus=x.bus,address=x.address,urls=x.URLS) for x in resolved]),flush=True)
matches=[x for x in resolved if x.bus==d.bus and x.address==d.address and len(x.URLS)>1 and x.URLS[1]]
assert len(matches)==1,'No unique JTAG interface on the whitelisted new board'
url=matches[0].URLS[1]
assert url.startswith(f'ftdi://0x0403:0x6011:{d.bus:x}:{d.address:x}/')
record['url']=url
(HERE/'selected_target.json').write_text(json.dumps(record,indent=2),encoding='utf-8')
sys.argv=[str(H/'pgm/bin/efx_pgm/ftdi_program.py'),str(bit),'-m','jtag','-u',url,
          '--jtag_clock_freq','6000000']
runpy.run_path(sys.argv[0],run_name='__main__')
