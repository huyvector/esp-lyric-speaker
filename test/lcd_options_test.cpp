#include "lcd_options.h"
#include <cassert>
#include <map>
#include <string>
#include <iostream>
struct Prefs {
 std::map<std::string,uint8_t> values;unsigned writes=0;
 uint8_t getUChar(const char* name,uint8_t fallback){auto it=values.find(name);return it==values.end()?fallback:it->second;}
 void putUChar(const char* name,uint8_t value){values[name]=value;++writes;}
};
int main(){
 Prefs fresh;assert(vinyl::loadLcdOptions(fresh)==0&&fresh.values["lcdProfile"]==1);
 // Fixtures cover saved BGR/flip profiles with either old inversion state.
 constexpr uint8_t oldFlags[]={4,5,6,7,0,1,2,3},corrected[]={0,1,2,3,4,5,6,7};
 for(unsigned i=0;i<8;++i){Prefs prefs;prefs.values["lcd"]=oldFlags[i];assert(vinyl::loadLcdOptions(prefs)==corrected[i]);auto writes=prefs.writes;
  for(unsigned reboot=0;reboot<4;++reboot)assert(vinyl::loadLcdOptions(prefs)==corrected[i]);assert(prefs.writes==writes);
  // A later manual inversion choice is respected after subsequent flashes.
  prefs.putUChar("lcd",oldFlags[i]);assert(vinyl::loadLcdOptions(prefs)==oldFlags[i]);assert(prefs.writes==writes+1);
 }
 std::cout<<"ST7789 saved options: fresh default, one-time inversion correction, preserve RGB/BGR+flip, idempotent reboot and later manual override: PASS\n";
}
