#pragma once
#include <Update.h>
#include "link_protocol.h"
// Bounded prefix accumulation; reject wrong chip before accepting image data.
class ImageUpdate {
 size_t expected=0,received=0,prefixSize=0;uint8_t prefix[24]{};uint16_t chip=0;bool active=false,checked=false;
public:
 static bool valid(size_t size,const char*md5){if(size<24||size>0x180000||!md5||strlen(md5)!=32)return false;for(unsigned i=0;i<32;++i)if(!((md5[i]>='0'&&md5[i]<='9')||(md5[i]>='a'&&md5[i]<='f')||(md5[i]>='A'&&md5[i]<='F')))return false;return true;}
 bool begin(size_t size,const char*md5,uint16_t target){abort();if(!valid(size,md5))return false;expected=size;chip=target;active=Update.begin(size,U_FLASH);if(active)active=Update.setMD5(md5);if(!active)abort();return active;}
 bool write(const uint8_t*p,size_t n){if(!active||!p||n>expected-received)return false;received+=n;
 if(!checked){size_t take=std::min(n,sizeof prefix-prefixSize);memcpy(prefix+prefixSize,p,take);prefixSize+=take;p+=take;n-=take;if(prefixSize<sizeof prefix)return true;if(prefix[0]!=0xe9||linkwire::get16(prefix+12)!=chip){abort();return false;}checked=true;if(Update.write(prefix,sizeof prefix)!=sizeof prefix){abort();return false;}}
 if(n&&Update.write(const_cast<uint8_t*>(p),n)!=n){abort();return false;}return true;}
 bool finish(){bool ok=active&&checked&&received==expected&&Update.end(false);if(!ok)abort();active=false;return ok;}
 void abort(){if(active||Update.isRunning())Update.abort();active=checked=false;expected=received=prefixSize=0;}
 size_t offset()const{return received;}bool running()const{return active;}
};
