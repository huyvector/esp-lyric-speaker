#pragma once
#include <algorithm>
#include <cstdint>
#include <cmath>
namespace vinyl {
constexpr uint16_t INK = 0x0841, CREAM = 0xF75B, GOLD = 0xD568, MUTED = 0x8C71, TEAL = 0x67B5;
inline uint16_t rgb(unsigned r, unsigned g, unsigned b) { return uint16_t((r >> 3) << 11 | (g >> 2) << 5 | (b >> 3)); }
// Static radial grooves and broad, fixed specular lobes. Render once with one
// 480-byte scanline, never retain a 240x240 RGB framebuffer in internal RAM.
inline uint16_t recordPixel(int x, int y) {
  int squared = x*x + y*y;
  if (squared > 110*110) return 0;
  int radius = int(std::sqrt(float(squared)));
  int beam = radius ? std::abs(x-y)*181/radius : 0;
  int shine = std::max(0, beam-210);
  int value = 14 + shine*2 + (radius % 3 == 0 ? 7 : 0);
  if (radius < 62) value = 9 + shine/3;
  if (radius >= 108) value = 40 + shine*2;
  return rgb(std::min(255,value), std::min(255,value+2), std::min(255,value+4));
}
inline uint16_t labelLight(uint16_t c, int x, int y) {
  // Fixed lighting stays in screen space while album art rotates underneath.
  unsigned a = unsigned(std::max(0, x-y+15))/8;
  unsigned r = std::min(31u, unsigned(c>>11) + a/2);
  unsigned g = std::min(63u, unsigned((c>>5)&63) + a);
  unsigned b = std::min(31u, unsigned(c&31) + a/2);
  return uint16_t(r<<11 | g<<5 | b);
}
template<class Display> void bluetoothIcon(Display& d,int x,int y,uint16_t c) {
  d.drawLine(x,y-6,x,y+6,c);d.drawLine(x,y-6,x+5,y-2,c);d.drawLine(x+5,y-2,x-4,y+4,c);
  d.drawLine(x-4,y-4,x+5,y+2,c);d.drawLine(x+5,y+2,x,y+6,c);
}
template<class Display> void wifiIcon(Display& d,int x,int y,uint16_t c) {
  d.drawLine(x-6,y-3,x-3,y-5,c);d.drawLine(x-3,y-5,x+3,y-5,c);d.drawLine(x+3,y-5,x+6,y-3,c);
  d.drawLine(x-3,y,x,y-2,c);d.drawLine(x,y-2,x+3,y,c);d.fillCircle(x,y+3,1,c);
}
}
