#!/usr/bin/env python3
"""Check real compiled ELF data, not a mock: C3 TFT pointers must hit SPI2 MMIO."""
import struct,sys
from pathlib import Path

def pointers(path):
 b=Path(path).read_bytes()
 if b[:6]!=b'\x7fELF\x01\x01':raise ValueError('Expected ELF32 little endian')
 table=struct.unpack_from('<I',b,32)[0];size,count=struct.unpack_from('<HH',b,46)
 sections=[struct.unpack_from('<10I',b,table+i*size) for i in range(count)]
 found={}
 for sec in sections:
  if sec[1]!=2:continue
  names=sections[sec[6]];strings=b[names[4]:names[4]+names[5]]
  for off in range(sec[4],sec[4]+sec[5],sec[9]):
   name,addr,_,_,_,idx=struct.unpack_from('<IIIBBH',b,off)
   end=strings.find(b'\0',name);name=strings[name:end].decode(errors='replace')
   if name not in ('_spi_user','_spi_cmd','_spi_mosi_dlen','_spi_w'):continue
   target=sections[idx];at=target[4]+addr-target[3]
   found[name]=0 if target[1]==8 else struct.unpack_from('<I',b,at)[0]
 return found

if __name__=='__main__':
 result=pointers(sys.argv[1]);expected={'_spi_user':0x60024010,'_spi_cmd':0x60024000,'_spi_mosi_dlen':0x6002401c,'_spi_w':0x60024098}
 print({k:hex(v) for k,v in result.items()})
 assert result==expected,'Invalid C3 SPI MMIO addresses; would crash at tft.init()'
 print('Actual C3 ELF: TFT SPI2 register pointers: PASS')
