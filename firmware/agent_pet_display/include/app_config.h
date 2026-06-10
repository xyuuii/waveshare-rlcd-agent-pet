#pragma once

#include <stdint.h>

static constexpr const char* kWifiSsid = "YOUR_WIFI_SSID";
static constexpr const char* kWifiPassword = "YOUR_WIFI_PASSWORD";
static constexpr const char* kBridgeUrl =
    "http://<bridge-host>:17366/esp32/poll?token=<token>";
static constexpr uint32_t kBridgePollMs = 3000;
static constexpr uint32_t kWifiReconnectMs = 10000;
static constexpr uint32_t kWifiReportMs = 15000;
static constexpr uint32_t kBatterySampleMs = 5000;
static constexpr uint32_t kEnvironmentSampleMs = 1000;
static constexpr int kBootButtonPin = 0;
static constexpr int kAgentFocusButtonPin = 18;
static constexpr uint32_t kPageToggleDebounceMs = 250;
static constexpr uint32_t kButtonLongPressMs = 1500;
static constexpr uint32_t kFocusAutoReturnMs = 45000;
static constexpr int kChargeSensePin = -1;
