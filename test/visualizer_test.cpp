#include "visualizer.h"
#include "ui_canvas.h"
#include <array>
#include <cassert>
#include <iostream>
uint32_t frameHash(const vinyl::VisualScene& scene){uint32_t hash=2166136261u;for(int y=vinyl::VISUAL_TOP;y<=vinyl::VISUAL_BOTTOM;++y){struct{uint32_t before=0x1234abcd;uint8_t pixels[284]{};uint32_t after=0x5678fedc;}row;scene.row(y,row.pixels);assert(row.before==0x1234abcd&&row.after==0x5678fedc);for(auto p:row.pixels)hash=(hash^p)*16777619u;}return hash;}
int main(){
 std::array<uint32_t,4> hashes{};
 for(unsigned mode=1;mode<=4;++mode){vinyl::VisualScene scene;for(unsigned now=0;now<=2200;now+=40)scene.prepare(vinyl::VisualMode(mode),now,22000,true);hashes[mode-1]=frameHash(scene);assert(scene.style==mode-1&&scene.level>240);auto before=hashes[mode-1];scene.prepare(vinyl::VisualMode(mode),2240,22000,true);assert(frameHash(scene)!=before);for(unsigned now=2300;now<6000;now+=50)scene.prepare(vinyl::VisualMode(mode),now,0,true);assert(scene.level<5);frameHash(scene);}
 for(unsigned a=0;a<4;++a)for(unsigned b=a+1;b<4;++b)assert(hashes[a]!=hashes[b]);
 vinyl::VisualScene scene;for(unsigned now=0;now<=64000;now+=100){scene.prepare(vinyl::VisualMode::Auto,now,65535,true);assert(scene.style==(now/16000)%4);if(now%16000==0)frameHash(scene);}
 auto frozen=scene.animationTime;for(unsigned now=64100;now<67000;now+=100)scene.prepare(vinyl::VisualMode::Auto,now,65535,false);assert(scene.animationTime==frozen&&scene.level==0);
 // Unsigned clock wrap and long stalls are bounded; no catch-up render queue.
 scene.prepare(vinyl::VisualMode::Stars,UINT32_MAX-20,32000,true);frozen=scene.animationTime;scene.prepare(vinyl::VisualMode::Stars,20,32000,true);assert(scene.animationTime-frozen==41);frameHash(scene);
 vinyl::MenuMotion menu;menu.select(5,0,true,6);menu.select(0,300,false,6);assert(menu.to-menu.from==256);assert(menu.position(700)==6*256);
 std::cout<<"Visual scenes: four distinct moving frames, peak attack/release, no row overflow, auto16s while playing, paused phase, uint32 wrap/stall cap; six-item carousel shortest wrap: PASS\n";
}
