#pragma once
// ESP32-C3 Super Mini: native USB remains GPIO18/19.
constexpr int LINK_RX=6,LINK_TX=5;
constexpr int ENC_CLK=7,ENC_DT=10,ENC_SW=20;
constexpr int ROUND_CS=21,LYRIC_CS=4,DISPLAY_DC=8,DISPLAY_RST=3;
constexpr int DISPLAY_SCLK=0,DISPLAY_MOSI=1;
// GPIO8 is output DC, not a weakly pulled-up button; board LED may flicker.
// GPIO2 and GPIO9/BOOT are left unused. User verified ST7789 BL goes to GND; no MCU pin.
