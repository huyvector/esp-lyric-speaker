from pathlib import Path
import importlib.util
root=Path(__file__).resolve().parents[1];spec=importlib.util.spec_from_file_location('flash',root/'flash.py');f=importlib.util.module_from_spec(spec);spec.loader.exec_module(f)
for role,chip in [('audio','esp32'),('display','esp32c3')]:
 f.ROLE=role;f.verify_images(root/'firmware'/role)
 for major in (4,5):
  args=f.flash_arguments('/dev/not-opened',460800,False,False,major);assert(args[args.index('--chip')+1]==chip and args[-2]=='0x0' and args[-1].endswith(role+'/factory.bin'))
  args=f.flash_arguments('/dev/not-opened',115200,True,True,major);assert(args[-4]=='0xe000' and args[-2]=='0x10000' and args[-3].endswith(role+'/ota-data.bin'))
print('Flash images/checksums, classic/C3 role separation, esptool 4/5 syntax, app0+OTA selection offsets: PASS; no serial opened')
