"""Exercise actual C3 encoder gesture handler against UI/command doubles."""
from pathlib import Path
import subprocess,tempfile
base=Path(__file__).resolve().parents[1];source=(base/'display/src/main.cpp').read_text();code=source[source.index('void controls(uint32_t now)'):source.index('void serialTick()')]
stub=r'''
#include "link_protocol.h"
#include "player_controls.h"
#include "visualizer.h"
#include <atomic>
#include <vector>
#include <cassert>
#include <iostream>
#include "pins.h"
#define LOW 0
bool down=false;int digitalRead(int pin){assert(pin==ENC_SW);return down?0:1;}int constrain(int x,int a,int b){return std::max(a,std::min(x,b));}
std::atomic<int>rotations{0};bool inverted=false,menu=false,effectMenu=false;int selected=0,desiredVolume=55,toneEdit=0,desiredBass=2;uint32_t toneIntentAt=0;bool linkOnline=true;linkwire::StateData audioState;uint32_t volumeIntentAt=0;vinyl::OLEDMode oledMode;vinyl::VisualMode visualMode=vinyl::VisualMode::Auto;std::vector<int>icons;
std::vector<int>commands,values;void command(uint8_t code,int value=0){commands.push_back(code);values.push_back(value);}unsigned toneField=0;int toneValue=0;bool toneAvailable=false;void uiBassBoost(int v,bool a){toneField=1;toneValue=v;toneAvailable=a;}void uiMenu(int,bool,bool=false){}void uiTransport(int direction,uint32_t){icons.push_back(direction);}void uiDirty(){}void uiInfo(int,uint32_t){}void uiVolume(int,int,bool,uint32_t){}
struct{unsigned visual=99,mode=99;void putUChar(const char*key,unsigned v){if(std::string(key)=="visual")visual=v;else if(std::string(key)=="mode")mode=v;}void putBool(const char*,bool){}}prefs;struct{void restart(){}}ESP;
struct IP{int toString(){return 0;}};struct{IP localIP(){return {};}}WiFi;
'''
checks=r'''
int main(){controls(0);down=true;controls(10);controls(40);down=false;controls(100);controls(130);controls(431);assert(commands.size()==1&&commands.back()==linkwire::PlayPause);commands.clear();
 // Press + rotate skips once, then release must not toggle playback.
 down=true;controls(500);controls(531);rotations=1;controls(550);down=false;controls(580);controls(611);controls(1000);assert(commands.size()==1&&commands[0]==linkwire::Next);commands.clear();
 // Hold opens menu; rotation chooses; release cannot trigger PlayPause.
 down=true;controls(1100);controls(1131);controls(1782);assert(menu);rotations=1;controls(1800);assert(selected==1);down=false;controls(1810);controls(1841);controls(2200);assert(menu&&commands.empty());
 // Click selects previous and leaves menu.
 down=true;controls(2300);controls(2331);down=false;controls(2400);controls(2431);assert(!menu&&commands.size()==1&&commands.back()==linkwire::Previous);commands.clear();
 // Double click maps to next only.
 down=true;controls(2500);controls(2531);down=false;controls(2560);controls(2591);down=true;controls(2620);controls(2651);down=false;controls(2680);controls(2711);controls(3100);assert(commands.size()==1&&commands[0]==linkwire::Next);commands.clear();
 // Three clicks must send previous only, never a premature next.
 down=true;controls(3200);controls(3231);down=false;controls(3260);controls(3291);
 down=true;controls(3320);controls(3351);down=false;controls(3380);controls(3411);assert(commands.empty());
 down=true;controls(3440);controls(3471);down=false;controls(3500);controls(3531);controls(3900);assert(commands.size()==1&&commands[0]==linkwire::Previous&&icons.back()==-1);commands.clear();
 // Visual submenu remains open on entry; choosing saves effect and forces visuals.
 menu=true;effectMenu=false;selected=3;down=true;controls(4000);controls(4031);down=false;controls(4060);controls(4091);assert(menu&&effectMenu);
 rotations=3;controls(4120);assert(selected==3);down=true;controls(4200);controls(4231);down=false;controls(4260);controls(4291);assert(!menu&&!effectMenu&&visualMode==vinyl::VisualMode::Stars&&prefs.visual==3&&prefs.mode==1&&commands.empty());
 // Hold from effect menu returns to main; selecting BACK does likewise.
 menu=effectMenu=true;selected=2;down=true;controls(4400);controls(4431);controls(5100);assert(menu&&!effectMenu&&selected==3);down=false;controls(5120);controls(5151);
 effectMenu=true;selected=5;down=true;controls(5200);controls(5231);down=false;controls(5260);controls(5291);assert(menu&&!effectMenu&&selected==3);menu=false;
 // A pending single click followed by a hold must not toggle playback.
 down=true;controls(5400);controls(5431);down=false;controls(5460);controls(5491);down=true;controls(5520);controls(5551);controls(5900);assert(commands.empty());controls(6250);assert(menu);down=false;controls(6280);controls(6311);menu=false;
 desiredVolume=126;rotations=1;controls(6400);assert(desiredVolume==127&&commands.size()==2&&commands[0]==linkwire::Volume&&commands[1]==linkwire::Cue);
 commands.clear();menu=true;selected=7;
 down=true;controls(6500);controls(6531);down=false;controls(6560);controls(6591);assert(menu&&toneEdit==1&&toneField==1);
 rotations=10;controls(6620);assert(desiredBass==3&&commands.size()==1&&commands.back()==linkwire::Bass&&values.back()==3&&toneValue==3);rotations=1;controls(6640);assert(commands.size()==1);
 down=true;controls(6700);controls(6731);down=false;controls(6760);controls(6791);assert(menu&&!toneEdit&&selected==7);rotations=1;controls(6800);assert(selected==8);
 down=true;controls(6900);controls(6931);down=false;controls(6960);controls(6991);assert(!menu&&!toneEdit&&selected==8);
 menu=true;selected=7;down=true;controls(7100);controls(7131);down=false;controls(7160);controls(7191);assert(toneEdit==1);linkOnline=false;auto size=commands.size();rotations=-2;controls(7200);assert(commands.size()==size&&!toneAvailable&&desiredBass==3);
 down=true;controls(7300);controls(7331);controls(8000);assert(menu&&!toneEdit&&selected==7);down=false;controls(8020);controls(8051);menu=false;
 commands.clear();down=true;controls(UINT32_MAX-200);controls(UINT32_MAX-169);down=false;controls(UINT32_MAX-30);controls(0);controls(301);assert(commands.size()==1&&commands[0]==linkwire::PlayPause);
 std::cout<<"Actual encoder controls: click, press-turn without stray toggle, hold/menu/select, double/triple without premature next, matching transport icons, visual submenu/persistence/back, Bass Boost/clamps/no-audio/back, pending-click cancellation, volume clamp/limit cue: PASS\n";
}
'''
with tempfile.TemporaryDirectory() as tmp:
 p=Path(tmp);(p/'controls.cpp').write_text(stub+code+checks)
 subprocess.run(['c++','-std=c++17','-fsanitize=address,undefined','-I'+str(base/'shared'),'-I'+str(base/'display/include'),str(p/'controls.cpp'),'-o',str(p/'controls')],check=True);subprocess.run([str(p/'controls')],check=True)
