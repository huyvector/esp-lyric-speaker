#pragma once
#include <Arduino.h>
enum class SetupStage:uint8_t{Boot,Joining,Portal,Scanning,Failed,Connected,Audio};
void uiSetup(SetupStage,const String&,uint32_t);
class SetupDisplay{SetupStage stage=SetupStage::Boot;String message;uint32_t last=0;public:void show(SetupStage s,const String&m=""){stage=s;message=m;uiSetup(stage,message,millis());last=millis();}void tick(uint32_t now){if(now-last>=200){last=now;uiSetup(stage,message,now);}}};
