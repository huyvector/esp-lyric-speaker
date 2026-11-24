"""Exercise the actual bounded C3 command queue with rapid EQ changes."""
from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[1];source=(root/'display/src/main.cpp').read_text();code=source[source.index('void command(uint8_t code,int value)'):source.index('void networkResume()')]
stub=r'''
#include "link_protocol.h"
#include <cassert>
#include <iostream>
struct{unsigned warnings=0;void println(const char*){++warnings;}}Serial;
struct Queued{uint8_t code=0;int value=0;};Queued fifo[8];unsigned first=0,count=0;struct{bool active=false;uint8_t data[9]{};}pending;
'''
checks=r'''
int main(){first=6;command(linkwire::Bass,0);command(linkwire::Next,0);command(linkwire::Volume,10);command(linkwire::Previous,0);for(int n=0;n<30;++n){command(linkwire::Bass,n%4);command(linkwire::Volume,n%128);}assert(count==4&&!Serial.warnings);assert(fifo[6].code==linkwire::Bass&&fifo[6].value==1&&fifo[7].code==linkwire::Next&&fifo[0].code==linkwire::Volume&&fifo[0].value==29&&fifo[1].code==linkwire::Previous);
 assert(toneInFlight());count=0;pending.active=true;pending.data[4]=linkwire::Bass;assert(toneInFlight());pending.data[4]=linkwire::Next;assert(!toneInFlight());pending.active=false;count=4;
 for(int n=0;n<4;++n)command(linkwire::Next,0);assert(count==8);command(linkwire::Bass,3);assert(count==8&&fifo[6].value==3&&!Serial.warnings);command(linkwire::Cue,0);assert(count==8&&Serial.warnings==1);
 std::cout<<"Actual C3 queue: rapid Bass Boost/volume coalesce, preserves navigation order, circular wrap and full-queue bounds: PASS\n";
}
'''
with tempfile.TemporaryDirectory() as tmp:
 p=Path(tmp);(p/'q.cpp').write_text(stub+code+checks);subprocess.run(['c++','-std=c++17','-fsanitize=address,undefined','-I'+str(root/'shared'),str(p/'q.cpp'),'-o',str(p/'q')],check=True);subprocess.run([str(p/'q')],check=True)
