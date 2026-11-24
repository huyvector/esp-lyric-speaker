#pragma once
#include <Preferences.h>
#include "setup_display.h"

// Runs before Bluetooth starts; never scans during an audio callback.
bool configureWiFi(Preferences& prefs, SetupDisplay& display, bool forcePortal);
uint8_t lastWiFiReason();
void readWiFiProfile(Preferences& prefs, String& ssid, String& password);
