#!/usr/bin/env python3
"""Own procedural UI palette/ring geometry. Generated tables live in flash."""
import math
from pathlib import Path
import numpy as np
root=Path(__file__).resolve().parents[1]
def rgb(r,g,b):return (r>>3)<<11|(g>>2)<<5|(b>>3)
def unpack(c):return [(c>>11)*255//31,((c>>5)&63)*255//63,(c&31)*255//31]
pal=[rgb(7+y//28,10+y//24,16+y//20) for y in range(0,76,5)][:16]
for target in [0xff59,0xe58c,0x8c91,0x3d9f,0xf1b4]:
 t=unpack(target)
 pal += [rgb(*[round(a+(b-a)*i/47) for a,b in zip((8,12,18),t)]) for i in range(48)]
assert len(pal)==256
allc=np.arange(65536,dtype=np.int32)
rgbc=np.stack(((allc>>11)*255//31,((allc>>5)&63)*255//63,(allc&31)*255//31),axis=1)
p=np.array([unpack(c) for c in pal],dtype=np.int32)
lookup=[]
for part in np.array_split(rgbc,64):lookup.extend(((part[:,None,:]-p[None,:,:])**2).sum(axis=2).argmin(axis=1).tolist())
def rim(inner,outer):
 spans=[];angles=[];cover=[]
 for y in range(240):
  x=0
  while x<240:
   r=math.hypot(x-119.5,y-119.5)
   if not inner<=r<=outer:x+=1;continue
   start=x;offset=len(angles)
   while x<240:
    dx=x-119.5;dy=y-119.5;r=math.hypot(dx,dy)
    if not inner<=r<=outer:break
    angles.append(round(((math.atan2(dx,-dy)+2*math.pi)%(2*math.pi))/(2*math.pi)*65535))
    cover.append(round(min(1,max(0,min(r-inner,outer-r)))*255));x+=1
   spans.append((start,y,x-start,offset))
 return spans,angles,cover
def array(name,typ,data,columns=24):return f'constexpr {typ} {name}[{len(data)}]={{\n'+''.join(','.join(map(str,data[i:i+columns]))+',\n' for i in range(0,len(data),columns))+'};\n'
s='#pragma once\n#include <cstdint>\nnamespace vinyl {\n'
s+=array('LYRIC_PALETTE','uint16_t',pal)+array('LYRIC_INDEX','uint8_t',lookup)
s+='struct RimSpan {uint8_t x,y,count;uint16_t offset;};\n'
for name,inner,outer in [('RIM',113,119),('PLAY_RING',107,110)]:
 spans,angles,cover=rim(inner,outer)
 s+='constexpr RimSpan '+name+'_SPANS[]={\n'+''.join('{%s},\n'%(','.join(map(str,r))) for r in spans)+'};\n'
 s+=array(name+'_ANGLE','uint16_t',angles)+array(name+'_COVER','uint8_t',cover)
 print(name,'pixels',len(angles),'spans',len(spans))
s+='}\n'
(root/'shared/ui_assets.h').write_text(s)
