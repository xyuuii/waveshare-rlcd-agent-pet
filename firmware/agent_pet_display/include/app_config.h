#pragma once

#include <stdint.h>

// Wi-Fi and bridge credentials live in include/secrets.h, which git ignores.
// Copy include/secrets.example.h to include/secrets.h and fill it in, or run
// tools/import-legacy-secrets.sh to reuse the values of an older checkout.
#if __has_include("secrets.h")
#include "secrets.h"
#else
static constexpr const char* kWifiSsid = "YOUR_WIFI_SSID";
static constexpr const char* kWifiPassword = "YOUR_WIFI_PASSWORD";
static constexpr const char* kBridgeUrl =
    "http://<bridge-host>:17366/esp32/poll?token=<token>";
#endif

static constexpr uint32_t kBridgePollMs = 3000;
static constexpr uint32_t kWifiReconnectMs = 10000;
static constexpr uint32_t kWifiReportMs = 15000;
static constexpr uint32_t kBatterySampleMs = 5000;
static constexpr uint32_t kEnvironmentSampleMs = 1000;  // climate + legacy callers
static constexpr uint32_t kClockSampleMs = 200;         // RTC read; keeps seconds on time
static constexpr uint32_t kClimateSampleMs = 5000;
static constexpr uint32_t kRtcResyncMs = 6UL * 3600UL * 1000UL;
static constexpr int kBootButtonPin = 0;
static constexpr int kAgentFocusButtonPin = 18;  // "KEY" on the board (factory firmware uses GPIO18)
static constexpr uint32_t kPageToggleDebounceMs = 250;
static constexpr uint32_t kButtonLongPressMs = 1500;
static constexpr uint32_t kButtonVeryLongPressMs = 5000;
static constexpr uint32_t kEggChordHoldMs = 2000;
static constexpr uint32_t kFocusAutoReturnMs = 45000;
static constexpr uint32_t kRenderIntervalMs = 100;
static constexpr uint32_t kSettingsSaveDelayMs = 2000;
static constexpr int kChargeSensePin = -1;
