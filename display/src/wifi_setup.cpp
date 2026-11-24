#include "wifi_setup.h"
#include "wifi_policy.h"

#include "portal_page.h"
#include "device_identity.h"
#include <ArduinoJson.h>
#include <DNSServer.h>
#include <WebServer.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <atomic>
#include <algorithm>

namespace {
constexpr uint32_t JOIN_MS = 20000;
std::atomic<uint8_t> reason{0};
bool eventInstalled = false;
const char* failureTitle(uint8_t code) {
  switch (vinyl::classifyWiFiReason(code)) {
    case vinyl::WiFiFailure::Password: return "Password / security error";
    case vinyl::WiFiFailure::NotFound: return "Wi-Fi not found";
    case vinyl::WiFiFailure::Rejected: return "Router rejected connection";
    case vinyl::WiFiFailure::Lost: return "Weak signal / link lost";
    default: return "Connection timed out";
  }
}
const char* failureMessage(uint8_t code) {
  switch (vinyl::classifyWiFiReason(code)) {
    case vinyl::WiFiFailure::Password: return "Check the Wi-Fi password and router security settings.";
    case vinyl::WiFiFailure::NotFound: return "Network not found. Check the name, signal and 2.4 GHz band.";
    case vinyl::WiFiFailure::Rejected: return "The router refused the connection. Check its security and device limit.";
    case vinyl::WiFiFailure::Lost: return "Connection lost. Move the speaker closer to the router and try again.";
    default: return "Connection timed out. Check the password and router, then try again.";
  }
}
bool hasIP() { return vinyl::wifiJoinSucceeded(WiFi.status() == WL_CONNECTED, uint32_t(WiFi.localIP())); }
bool saveProfile(Preferences& prefs, const String& ssid, const String& password) {
  StaticJsonDocument<384> doc; doc["ssid"] = ssid; doc["pass"] = password;
  String value; serializeJson(doc, value);
  // One NVS value prevents a reset between separate SSID/password writes.
  return prefs.putString("wifi_v2", value) == value.length();
}
void beginJoin(const String& ssid, const String& password) {
  WiFi.setAutoReconnect(false);
  WiFi.disconnect(false, false);
  // Clear a previous failure only after the explicit disconnect has been queued.
  reason.store(0);
  Serial.printf("[wifi] connecting ssid=%s timeout=20s\n", ssid.c_str());
  WiFi.begin(ssid.c_str(), password.c_str());
}
struct Network {
  char ssid[33]{};
  int32_t rssi = -127;
  uint8_t auth = 0, channel = 0;
};
const char* authName(uint8_t auth) {
  switch (auth) {
    case WIFI_AUTH_OPEN: return "OPEN";
    case WIFI_AUTH_WEP: return "WEP";
    case WIFI_AUTH_WPA_PSK: return "WPA";
    case WIFI_AUTH_WPA2_PSK: return "WPA2";
    case WIFI_AUTH_WPA_WPA2_PSK: return "WPA / WPA2";
    case WIFI_AUTH_WPA2_ENTERPRISE: return "Enterprise";
    case WIFI_AUTH_WPA3_PSK: return "WPA3";
    case WIFI_AUTH_WPA2_WPA3_PSK: return "WPA2 / WPA3";
    default: return "Protected";
  }
}
bool supported(uint8_t auth) {
  return auth == WIFI_AUTH_OPEN || auth == WIFI_AUTH_WPA2_PSK || auth == WIFI_AUTH_WPA_WPA2_PSK || auth == WIFI_AUTH_WPA3_PSK || auth == WIFI_AUTH_WPA2_WPA3_PSK;
}

class Portal {
  Preferences& prefs;
  SetupDisplay& display;
  DNSServer dns;
  WebServer server{80};
  Network networks[16];
  size_t count = 0;
  bool scanPrepared = false, scanning = false, wantScan = true, joining = false, pending = false, success = false, offline = false;
  uint8_t failureCode = 0;
  uint32_t prepareAt = 0, scanAt = 0, scanRequested = 0, joinAt = 0, connectedAt = 0;
  String ssid, password, message;
  const char* state = "ready";
  String serialInput;

  void json(int code, const char* message) {
    StaticJsonDocument<512> doc; doc["message"] = message;
    String body; serializeJson(doc, body);
    server.sendHeader("Cache-Control", "no-store"); server.send(code, "application/json; charset=utf-8", body);
  }
  void page() {
    server.sendHeader("Cache-Control", "no-store, no-cache, must-revalidate");
    server.sendHeader("Pragma", "no-cache");
    server.sendHeader("X-Content-Type-Options", "nosniff");
    server.send_P(200, "text/html; charset=utf-8", PORTAL_PAGE);
  }
  void status() {
    StaticJsonDocument<768> doc;
    doc["state"] = state; doc["ssid"] = ssid; doc["message"] = message;
    doc["reason"] = !strcmp(state, "failed") ? failureCode : reason.load(); doc["elapsed"] = joining ? (millis() - joinAt) / 1000 : 0;
    doc["ip"] = success ? WiFi.localIP().toString() : "";
    String body; serializeJson(doc, body); server.sendHeader("Cache-Control", "no-store");
    server.send(200, "application/json; charset=utf-8", body);
  }
  void listNetworks() {
    DynamicJsonDocument doc(4096);
    doc["scanning"] = scanning || wantScan;
    JsonArray list = doc.createNestedArray("networks");
    for (size_t i = 0; i < count; ++i) {
      JsonObject row = list.createNestedObject(); row["ssid"] = networks[i].ssid;
      row["rssi"] = networks[i].rssi; row["channel"] = networks[i].channel;
      row["secure"] = networks[i].auth != WIFI_AUTH_OPEN; row["auth"] = authName(networks[i].auth);
      row["supported"] = supported(networks[i].auth);
    }
    String body; serializeJson(doc, body); server.sendHeader("Cache-Control", "no-store");
    server.send(200, "application/json; charset=utf-8", body);
  }
  void submit() {
    if (joining || success || pending) { json(409, "Connection in progress. Please wait."); return; }
    if (scanning || wantScan) { json(409, "Wait for the Wi-Fi scan to finish."); return; }
    String nextSSID = server.arg("ssid"), nextPassword = server.arg("password");
    bool secure = false;
    for (size_t i = 0; i < count; ++i) if (nextSSID == networks[i].ssid) {
      secure = networks[i].auth != WIFI_AUTH_OPEN;
      if (!supported(networks[i].auth)) { json(400, "Unsupported security. Choose a personal WPA2/WPA3 network."); return; }
      break;
    }
    const char* error = vinyl::credentialError(nextSSID.c_str(), nextSSID.length(), nextPassword.c_str(), nextPassword.length(), secure);
    if (error) {
      json(400, !strcmp(error, "ssid") ? "Network name must contain 1-32 UTF-8 bytes without NUL." : "Use an 8-63 byte password or a 64-character hex key. Leave open networks blank."); return;
    }
    ssid = nextSSID; password = nextPassword; pending = true; failureCode = 0;
    state = "connecting"; message = "Connecting and waiting for an IP address."; joinAt = millis();
    json(202, "Connecting. Settings are saved after a successful connection.");
  }
  void scanTick(uint32_t now) {
    if (wantScan && !scanning && !joining && !pending && now - scanAt >= 700) {
      if (!scanPrepared) {
        WiFi.disconnect(false, false); // Let the queued STA disconnect finish first.
        prepareAt = now; scanPrepared = true;
        return;
      }
      if (now - prepareAt < 200) return;
      wantScan = false; scanPrepared = false;
      // Arduino 2.0.17 derives its own timeout as dwell * 20. 120 ms
      // allowed only 2.4 s, too short for an all-channel scan with AP clients.
      int result = WiFi.scanNetworks(true, true, false, 400);
      scanning = result == WIFI_SCAN_RUNNING; scanAt = now; scanRequested = now;
      if (scanning) { state = "scanning"; display.show(SetupStage::Scanning); Serial.println("[wifi] scan started"); }
      else { esp_wifi_scan_stop(); WiFi.scanDelete(); state = "ready"; message = "Could not scan. Tap Scan to try again."; Serial.printf("[wifi] scan start failed=%d\n", result); }
    }
    if (!scanning) return;
    int found = WiFi.scanComplete();
    if (found == WIFI_SCAN_RUNNING && now - scanAt < 12000) return;
    scanning = false; count = 0;
    if (found >= 0) {
      for (int i = 0; i < found; ++i) {
        String name = WiFi.SSID(i); if (name.isEmpty() || name.length() > 32) continue;
        int32_t signal = WiFi.RSSI(i);
        int existing = -1;
        for (size_t j = 0; j < count; ++j) if (name == networks[j].ssid) existing = j;
        if (existing >= 0 && signal <= networks[existing].rssi) continue;
        if (existing < 0) {
          if (count < 16) existing = count++;
          else {
            existing = 0;
            for (size_t j = 1; j < count; ++j) if (networks[j].rssi < networks[existing].rssi) existing = j;
            if (signal <= networks[existing].rssi) continue;
          }
        }
        strlcpy(networks[existing].ssid, name.c_str(), sizeof networks[existing].ssid);
        networks[existing].rssi = signal; networks[existing].auth = WiFi.encryptionType(i); networks[existing].channel = WiFi.channel(i);
      }
      std::sort(networks, networks + count, [](const Network& a, const Network& b) { return a.rssi > b.rssi; });
    } else {
      // scanComplete() also returns -2 on its internal timeout while the radio
      // scan can still be active. Always stop before retrying or connecting.
      esp_wifi_scan_stop();
    }
    message = found < 0 ? "Scan timed out. Scan again or enter a network name." : "";
    WiFi.scanDelete(); state = "ready";
    display.show(SetupStage::Portal);
    Serial.printf("[wifi] scan finished result=%d unique=%u elapsed=%ums\n", found, unsigned(count), unsigned(millis() - scanAt));
  }
  void joinTick(uint32_t now) {
    if (pending) {
      pending = false; joining = true; joinAt = now;
      display.show(SetupStage::Joining, ssid); beginJoin(ssid, password);
    }
    if (!joining) return;
    if (hasIP()) {
      // Successful association + DHCP, not merely accepting a web form.
      if (!saveProfile(prefs, ssid, password)) {
        message = "Connected, but settings could not be saved. Please try again.";
        joining = false; state = "failed"; password = "";
        display.show(SetupStage::Failed, "Could not save settings"); return;
      }
      password = ""; joining = false; success = true; state = "connected";
      reason.store(0);
      message = "Connected. Settings saved."; connectedAt = now;
      display.show(SetupStage::Connected, WiFi.localIP().toString());
      Serial.printf("[wifi] connected ssid=%s ip=%s rssi=%d settings=saved\n", ssid.c_str(), WiFi.localIP().toString().c_str(), WiFi.RSSI());
      return;
    }
    // A disconnect may be transient; give the driver the full bounded attempt.
    if (now - joinAt < JOIN_MS) return;
    joining = false; state = "failed"; password = "";
    uint8_t code = reason.load(); failureCode = code; message = failureMessage(code);
    display.show(SetupStage::Failed, failureTitle(code));
    Serial.printf("[wifi] join failed reason=%u wifi_status=%d; portal remains open\n", code, WiFi.status());
    WiFi.disconnect(false, false);
  }
  void serialTick() {
    while (Serial.available()) {
      char c = Serial.read(); if (c == '\r') continue;
      if (c != '\n') { if (serialInput.length() < 100) serialInput += c; continue; }
      if (serialInput == "status") Serial.printf("[wifi] setup state=%s ip=%s clients=%u reason=%u networks=%u heap=%u\n", state, WiFi.softAPIP().toString().c_str(), WiFi.softAPgetStationNum(), reason.load(), unsigned(count), ESP.getFreeHeap());
      else if (serialInput == "rescan" && !joining && !success) wantScan = true;
      else if (serialInput == "offline") offline = true;
      else Serial.println("[wifi] setup commands: status, rescan, offline");
      serialInput = "";
    }
  }
public:
  Portal(Preferences& storage, SetupDisplay& screens) : prefs(storage), display(screens) {}
  bool run() {
    WiFi.disconnect(false, false); WiFi.mode(WIFI_AP_STA); WiFi.setSleep(false);
    IPAddress address(192, 168, 4, 1);
    WiFi.softAPConfig(address, address, IPAddress(255, 255, 255, 0));
    if (!WiFi.softAP(vinyl::SETUP_SSID, vinyl::SETUP_PASSWORD, 1, false, 2)) {
      Serial.println("[wifi] setup AP failed; offline playback available"); display.show(SetupStage::Failed, "Could not start setup AP");
      WiFi.mode(WIFI_STA);
      return false;
    }
    dns.setErrorReplyCode(DNSReplyCode::NoError);
    if (!dns.start(53, "*", address)) Serial.println("[wifi] captive DNS could not bind; open http://192.168.4.1 manually");
    server.on("/", HTTP_GET, [this]() { page(); });
    server.on("/api/status", HTTP_GET, [this]() { status(); });
    server.on("/api/networks", HTTP_GET, [this]() { listNetworks(); });
    server.on("/api/connect", HTTP_POST, [this]() { submit(); });
    server.on("/api/scan", HTTP_POST, [this]() {
      if (joining || success || pending) { json(409, "Connection in progress. Please wait before scanning."); return; }
      if (!scanning && !wantScan && millis() - scanRequested >= 4000) wantScan = true;
      json(202, "Scanning for Wi-Fi.");
    });
    // iOS, Android, macOS and Windows connectivity probes resolve via our DNS.
    for (const char* path : {"/hotspot-detect.html", "/library/test/success.html", "/generate_204", "/gen_204", "/connecttest.txt", "/ncsi.txt", "/fwlink"})
      server.on(path, HTTP_GET, [this]() { page(); });
    server.onNotFound([this]() { if (server.uri().startsWith("/api/")) json(404, "API not found."); else page(); });
    server.begin(); display.show(SetupStage::Portal);
    Serial.println("[wifi] SETUP AP=Vinyl-Setup key=vinylsetup url=http://192.168.4.1 DNS=captive; stays open until connected");
    while (!offline && (!success || millis() - connectedAt < 7000)) {
      dns.processNextRequest(); server.handleClient(); serialTick();
      uint32_t now = millis(); scanTick(now); joinTick(now); display.tick(now); delay(3);
    }
    server.stop(); dns.stop();
    if (scanning) esp_wifi_scan_stop();
    WiFi.scanDelete(); WiFi.softAPdisconnect(true); WiFi.mode(WIFI_STA); WiFi.setSleep(true);
    if (offline) { WiFi.disconnect(false, false); Serial.println("[wifi] offline mode requested by USB; use setup to return"); }
    return success && !offline;
  }
};
}
uint8_t lastWiFiReason() { return reason.load(); }
void readWiFiProfile(Preferences& prefs, String& ssid, String& password) {
  String value = prefs.getString("wifi_v2", ""); StaticJsonDocument<384> doc;
  if (value.length() && !deserializeJson(doc, value) && doc["ssid"].is<const char*>() && doc["pass"].is<const char*>()) {
    ssid = doc["ssid"].as<String>(); password = doc["pass"].as<String>();
  } else { ssid = prefs.getString("ssid", ""); password = prefs.getString("pass", ""); }
}
bool configureWiFi(Preferences& prefs, SetupDisplay& display, bool forcePortal) {
  if (!eventInstalled) {
    WiFi.onEvent([](WiFiEvent_t, WiFiEventInfo_t info) {
      uint8_t code = info.wifi_sta_disconnected.reason;
      // Our deliberate disconnect before a scan/join is not a router failure.
      if (code != WIFI_REASON_ASSOC_LEAVE) reason.store(code);
    }, ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
    eventInstalled = true;
  }
  WiFi.useStaticBuffers(true); // Arduino 2.x: use SDK defaults, bypass hard-coded 4/32/32 overrides.
  Serial.printf("[wifi] init pools=SDK rx_static=%u rx_dynamic=%u tx_dynamic=%u rx_ba=%u\n",
    CONFIG_ESP32_WIFI_STATIC_RX_BUFFER_NUM, CONFIG_ESP32_WIFI_DYNAMIC_RX_BUFFER_NUM,
    CONFIG_ESP32_WIFI_DYNAMIC_TX_BUFFER_NUM, CONFIG_ESP32_WIFI_RX_BA_WIN);
  WiFi.persistent(false); WiFi.mode(WIFI_STA); WiFi.setAutoReconnect(false);
  WiFi.setSleep(false); // provisioning happens before audio; prioritize association
  String ssid, password; readWiFiProfile(prefs, ssid, password);
  Serial.printf("[wifi] boot profile=%s force_portal=%u\n", ssid.length() ? "present" : "missing", forcePortal);
  if (!forcePortal && ssid.length()) {
    display.show(SetupStage::Joining, ssid); beginJoin(ssid, password);
    uint32_t start = millis();
    while (!hasIP() && millis() - start < JOIN_MS) { display.tick(millis()); delay(8); }
    if (hasIP()) {
      reason.store(0);
      Serial.printf("[wifi] saved network connected ip=%s rssi=%d\n", WiFi.localIP().toString().c_str(), WiFi.RSSI());
      display.show(SetupStage::Connected, WiFi.localIP().toString());
      for (uint32_t now = millis(); millis() - now < 1200;) { display.tick(millis()); delay(8); }
      WiFi.setSleep(true); return true;
    }
    Serial.printf("[wifi] saved network failed reason=%u status=%d; opening captive portal\n", reason.load(), WiFi.status());
    WiFi.disconnect(false, false);
  }
  Portal portal(prefs, display);
  return portal.run();
}
