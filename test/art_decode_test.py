"""Run actual JPEG tile callback at 96px, including partial edge MCUs."""
from pathlib import Path
import subprocess,tempfile
base=Path(__file__).resolve().parents[1];s=(base/'display/src/network.cpp').read_text();code=s[s.index('int drawJPEG('):s.index('int32_t jpegRead(')]
stub=r'''
#include "lyrics_core.h"
#include <cassert>
#include <vector>
#include <iostream>
int sourceWidth=0,sourceHeight=0;alignas(4)uint8_t staging[vinyl::TRANSFER_LIMIT+1]{};
struct JPEGDRAW{int x,y,iWidth,iHeight;uint16_t*pPixels;};
'''
checks=r'''
int main(){
 static_assert(vinyl::ART_SIZE==96&&vinyl::ART_BYTES==18432&&vinyl::JSON_LIMIT==8192,"Art/JSON budgets");
 for(int size:{64,96,100,128}){sourceWidth=sourceHeight=size;memset(staging,0xcc,sizeof staging);
  for(int y=0;y<size;y+=16)for(int x=0;x<size;x+=16){uint16_t pixels[256];for(int j=0;j<16;++j)for(int i=0;i<16;++i)pixels[j*16+i]=uint16_t((y+j)*131+(x+i)*17);JPEGDRAW tile{x,y,16,16,pixels};assert(drawJPEG(&tile)==1);}
  auto pixels=reinterpret_cast<const uint16_t*>(staging);for(int y=0;y<96;++y)for(int x=0;x<96;++x)assert(pixels[y*96+x]==uint16_t((y*size/96)*131+(x*size/96)*17));assert(staging[vinyl::ART_BYTES]==0xcc);
 }
 std::cout<<"Actual JPEG callback: 96px complete coverage, 64/96/100/128px source, partial edge MCU stride, arena canary: PASS\n";
}
'''
with tempfile.TemporaryDirectory() as tmp:
 p=Path(tmp);(p/'decode.cpp').write_text(stub+code+checks);subprocess.run(['c++','-std=c++17','-fsanitize=address,undefined','-I'+str(base/'shared'),str(p/'decode.cpp'),'-o',str(p/'decode')],check=True);subprocess.run([str(p/'decode')],check=True)
