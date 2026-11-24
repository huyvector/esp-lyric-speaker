#pragma once
#include "link_protocol.h"
void command(uint8_t code,int value=0);
void transportTick();
bool relayRequest(uint8_t type,const uint8_t*data,size_t n,uint32_t*offset=nullptr);
void networkCancel();void networkResume();void handleResults();
void otaBeginServer();void otaTick();bool otaBusy();
