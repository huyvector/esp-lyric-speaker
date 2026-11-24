#pragma once
#include <cstdint>
namespace vinyl {
struct Rotary {uint8_t previous=3;int partial=0;int update(uint8_t value){value&=3;static const int8_t steps[16]={0,-1,1,0,1,0,0,-1,-1,0,0,1,0,1,-1,0};if((previous^value)==3)partial=0;else partial+=steps[(previous<<2)|value];previous=value;if(partial>=4){partial=0;return 1;}if(partial<=-4){partial=0;return -1;}return 0;}};
}
