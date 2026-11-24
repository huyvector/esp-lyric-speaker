#pragma once
#include <algorithm>
namespace vinyl {
enum class ConnectionIcon { WiFi, Bluetooth, Phone, Browser, Music, Check, Warning, Link };
template<class Canvas>void connectionIcon(Canvas& c,ConnectionIcon kind,int x,int y,int size,unsigned color){
 auto line=[&](int a,int b,int d,int e){c.line(x+a*size/32,y+b*size/32,x+d*size/32,y+e*size/32,color,std::max(1,size/16));};
 switch(kind){
 case ConnectionIcon::WiFi:
  line(-14,-4,-10,-8);line(-10,-8,-4,-11);line(-4,-11,4,-11);line(4,-11,10,-8);line(10,-8,14,-4);
  line(-9,1,-5,-3);line(-5,-3,5,-3);line(5,-3,9,1);line(-4,6,0,3);line(0,3,4,6);c.dot(x,y+size/3,std::max(1,size/12),color);break;
 case ConnectionIcon::Bluetooth:
  line(0,-14,0,14);line(0,-14,9,-6);line(9,-6,-8,8);line(-8,-8,9,6);line(9,6,0,14);break;
 case ConnectionIcon::Phone:
  line(-9,-14,9,-14);line(9,-14,9,14);line(9,14,-9,14);line(-9,14,-9,-14);line(-3,-10,3,-10);line(-2,10,2,10);break;
 case ConnectionIcon::Browser:
  line(-14,-11,14,-11);line(14,-11,14,11);line(14,11,-14,11);line(-14,11,-14,-11);line(-14,-5,14,-5);line(-9,-8,-9,-8);line(-5,-8,-5,-8);line(-6,3,6,3);break;
 case ConnectionIcon::Music:
  line(-3,-10,10,-13);line(10,-13,10,7);line(-3,-10,-3,10);line(-3,-5,10,-8);c.dot(x-7*size/32,y+10*size/32,std::max(2,size/7),color);c.dot(x+6*size/32,y+7*size/32,std::max(2,size/7),color);break;
 case ConnectionIcon::Check:
  c.circle(x,y,size*7/16,color,std::max(1,size/16));line(-8,0,-2,6);line(-2,6,9,-6);break;
 case ConnectionIcon::Warning:
  line(0,-14,15,12);line(15,12,-15,12);line(-15,12,0,-14);line(0,-5,0,3);c.dot(x,y+8*size/32,std::max(1,size/16),color);break;
 case ConnectionIcon::Link:
  line(-14,-8,-4,-8);line(-14,-8,-14,8);line(-14,8,-4,8);line(14,-8,4,-8);line(14,-8,14,8);line(14,8,4,8);line(-7,0,7,0);break;
 }
}
}
