"""Run the real streaming image guard against a fake Arduino Update backend."""
from pathlib import Path
import subprocess,tempfile
base=Path(__file__).resolve().parents[1]
fake=r'''
#pragma once
#include <cstdint>
#include <cstring>
#include <vector>
#include <algorithm>
#define U_FLASH 0
struct FakeUpdate{bool active=false,md5ok=true,endok=true;unsigned aborts=0;std::vector<uint8_t> bytes;
 bool begin(size_t,int){active=true;bytes.clear();return true;}bool setMD5(const char*){return md5ok;}
 bool isRunning(){return active;}void abort(){active=false;++aborts;}size_t write(uint8_t*p,size_t n){bytes.insert(bytes.end(),p,p+n);return n;}
 bool end(bool){active=false;return endok;}
}Update;
'''
checks=r'''
#include "image_update.h"
#include <cassert>
#include <iostream>
int main(){ImageUpdate u;const char*md5="d41d8cd98f00b204e9800998ecf8427e";
 uint8_t data[513]{};data[0]=0xe9;data[12]=5;
 assert(!u.begin(23,md5,5)&&!u.begin(0x180001,md5,5)&&!u.begin(500,"xyz",5));
 assert(!u.begin(500,"Z41d8cd98f00b204e9800998ecf8427e",5));
 for(size_t block:{size_t(1),size_t(7),size_t(23),size_t(24),size_t(256),size_t(513)}){assert(u.begin(sizeof data,md5,5));for(size_t at=0;at<sizeof data;){size_t n=std::min(block,sizeof data-at);assert(u.write(data+at,n));at+=n;if(at<24)assert(Update.bytes.empty());}assert(u.finish());assert(Update.bytes.size()==513&&!memcmp(Update.bytes.data(),data,513));}
 assert(u.begin(sizeof data,md5,0));assert(!u.write(data,sizeof data));assert(Update.bytes.empty()&&!Update.active);
 assert(u.begin(sizeof data,md5,5));assert(u.write(data,23));assert(!u.finish()&&!Update.active);
 assert(u.begin(sizeof data,md5,5));assert(!u.write(data,sizeof data+1));u.abort();assert(!Update.active);
 Update.md5ok=false;assert(!u.begin(sizeof data,md5,5)&&!Update.active);Update.md5ok=true;
 assert(u.begin(sizeof data,md5,5));assert(u.write(data,sizeof data));Update.endok=false;assert(!u.finish());
 std::cout<<"Actual image updater: split 24-byte header/chip rejection/length/bounds/abort/MD5 failure: PASS\n";
}
'''
with tempfile.TemporaryDirectory() as temp:
 p=Path(temp);(p/'Update.h').write_text(fake);(p/'image.cpp').write_text(checks)
 subprocess.run(['c++','-std=c++17','-fsanitize=address,undefined','-I'+str(p),'-I'+str(base/'shared'),str(p/'image.cpp'),'-o',str(p/'image')],check=True)
 subprocess.run([str(p/'image')],check=True)
