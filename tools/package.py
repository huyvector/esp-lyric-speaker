#!/usr/bin/env python3
"""Package built images; requires esptool 4.x and the pinned Arduino framework."""
import argparse,hashlib,shutil,subprocess,sys
from pathlib import Path
from bundle import pack,inspect
root=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser();p.add_argument('--audio-build',type=Path,required=True);p.add_argument('--display-build',type=Path,required=True);p.add_argument('--arduino',type=Path,required=True);args=p.parse_args()
for role,build,chip,bootat in [('audio',args.audio_build,'esp32','0x1000'),('display',args.display_build,'esp32c3','0x0')]:
 out=root/'firmware'/role;out.mkdir(parents=True,exist_ok=True)
 boot=build/'bootloader.bin'
 ota=args.arduino/'tools/partitions/boot_app0.bin'
 for src,dest in [(build/'firmware.bin','app.bin'),(boot,'bootloader.bin'),(build/'partitions.bin','partitions.bin'),(ota,'ota-data.bin')]:shutil.copyfile(src,out/dest)
 subprocess.run([sys.executable,'-m','esptool','--chip',chip,'merge_bin','--output',str(out/'factory.bin'),'--flash_mode','dio','--flash_freq','40m','--flash_size','4MB',bootat,str(boot),'0x8000',str(build/'partitions.bin'),'0xe000',str(ota),'0x10000',str(build/'firmware.bin')],check=True)
 files=sorted(out.glob('*.bin'));(out/'SHA256SUMS.txt').write_text(''.join(hashlib.sha256(f.read_bytes()).hexdigest()+'  '+f.name+'\n' for f in files))
bundle=pack((root/'firmware/audio/app.bin').read_bytes(),(root/'firmware/display/app.bin').read_bytes())
(root/'firmware/vinyl-dual-0.4.11.vup').write_bytes(bundle);print(inspect(bundle))
files=sorted((root/'firmware').glob('*/*.bin'))+[root/'firmware/vinyl-dual-0.4.11.vup']
(root/'firmware/SHA256SUMS.txt').write_text(''.join(hashlib.sha256(f.read_bytes()).hexdigest()+'  '+str(f.relative_to(root/'firmware'))+'\n' for f in files))
