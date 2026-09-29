#pragma once

// Copy to include/secrets.h (git-ignored) and fill in. Never commit real values.
static constexpr const char* kWifiSsid = "YOUR_WIFI_SSID";
static constexpr const char* kWifiPassword = "YOUR_WIFI_PASSWORD";
// The Mac running codex-pet-bridge (fixed LAN IP or DHCP reservation) + its token.
static constexpr const char* kBridgeUrl =
    "http://192.168.1.23:17366/esp32/poll?token=<token>";
