#pragma once
#include "app.h"
#include "player_controls.h"
#include "link_protocol.h"
#include "visualizer.h"
void uiInfo(const String&,uint32_t);void uiLcdOptions(uint8_t);void uiRestore();void uiBegin();void uiTick(uint32_t now);void uiDirty();void uiVolume(int old,int value,bool limit,uint32_t now);
void uiMenu(int selected,bool shown,bool effects=false);void uiTransport(int direction,uint32_t now);void uiTest();void uiUpdate(const char*,unsigned percent);
void uiBassBoost(int value,bool available);void uiConnectivity(bool connected);
void uiRoundBrightness(uint8_t value);void uiPrintStats();
extern Track track;extern vinyl::PackedLyrics lyrics;extern vinyl::Clock playClock;
extern uint32_t artworkWords[vinyl::ART_BYTES/4];extern bool haveArtwork,loadingLyrics,linkOnline;
extern char lyricStatus[40];extern linkwire::StateData audioState;extern vinyl::OLEDMode oledMode;extern int32_t offsetMs;

extern vinyl::VisualMode visualMode;
