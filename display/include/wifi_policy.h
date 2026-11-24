#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
namespace vinyl {
inline const char* credentialError(const char* ssid, size_t ssidBytes, const char* password, size_t passwordBytes, bool secure) {
  if (!ssidBytes || ssidBytes > 32 || strlen(ssid) != ssidBytes) return "ssid";
  if (strlen(password) != passwordBytes || passwordBytes > 64) return "password";
  if (!passwordBytes) return secure ? "password" : nullptr;
  if (passwordBytes < 8) return "password";
  if (passwordBytes == 64) {
    for (size_t i = 0; i < passwordBytes; ++i) {
      char c = password[i];
      if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) return "password";
    }
  }
  return nullptr;
}
enum class WiFiFailure : uint8_t { Password, NotFound, Rejected, Lost, Timeout };
inline WiFiFailure classifyWiFiReason(uint8_t reason) {
  switch (reason) {
    case 15: case 23: case 202: case 204: return WiFiFailure::Password;
    case 201: return WiFiFailure::NotFound;
    case 200: return WiFiFailure::Lost;
    case 2: case 4: return WiFiFailure::Timeout;
    default: return reason ? WiFiFailure::Rejected : WiFiFailure::Timeout;
  }
}
// DHCP and association can finish independently; success requires a real IP.
inline bool wifiJoinSucceeded(bool associated, uint32_t ipv4) { return associated && ipv4 != 0; }
}
