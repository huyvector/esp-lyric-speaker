#pragma once
#include <Arduino.h>
#include "link_protocol.h"
class SerialLink {
 HardwareSerial& port;linkwire::Parser parser;uint16_t sequence=0;uint8_t tx[linkwire::MAX_PAYLOAD+10]{};
public:
 linkwire::Frame frame;uint32_t txDropped=0,lastRx=0;
 explicit SerialLink(HardwareSerial&p):port(p){}
 void begin(int rx,int txpin){port.setRxBufferSize(2048);port.setTxBufferSize(1024);port.begin(921600,SERIAL_8N1,rx,txpin);}
 uint16_t next(){return ++sequence;}
 bool send(uint8_t type,const uint8_t*data,size_t n,uint16_t seq){size_t bytes=linkwire::encode(tx,sizeof tx,type,seq,data,n);if(!bytes||port.availableForWrite()<int(bytes)){++txDropped;return false;}return port.write(tx,bytes)==bytes;}
 bool send(uint8_t type,const uint8_t*data,size_t n){return send(type,data,n,next());}
 bool receive(){unsigned budget=2048;while(budget--&&port.available()){if(parser.feed(uint8_t(port.read()),millis(),frame)){lastRx=millis();return true;}}return false;}
 uint32_t errors()const{return parser.errors;}
};
