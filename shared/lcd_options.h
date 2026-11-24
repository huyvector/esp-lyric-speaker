#pragma once
#include <cstdint>
namespace vinyl {
constexpr uint8_t LCD_PROFILE=1,DEFAULT_LCD_FLAGS=0;
// The user's ST7789 is negative with the previous inversion setting. Convert
// its saved profile once; preserve orientation/BGR and later manual changes.
template<class Preferences>uint8_t loadLcdOptions(Preferences& prefs){
 uint8_t flags=prefs.getUChar("lcd",4)&7;
 if(prefs.getUChar("lcdProfile",0)<LCD_PROFILE){
  flags^=4;
  prefs.putUChar("lcd",flags);
  prefs.putUChar("lcdProfile",LCD_PROFILE);
 }
 return flags;
}
}
