#pragma once

#include <stdint.h>

#include <string>

#include "bridge_client.h"
#include "models.h"

// Board-side preferences that survive reboots (NVS on the ESP32).
struct DeviceSettingsState {
  DisplaySettings display{};
  ScreenPage page{ScreenPage::Overview};
  uint32_t displayRev{0};  // last bridge "display" revision applied
  std::string tz{};        // POSIX TZ; empty means kDefaultPosixTz
  bool rtcIsUtc{false};    // RTC migrated from the old China-local convention
};

// Applies dashboard/menu-bar changes. Only a new revision is applied so local
// button choices are not overwritten by a stale bridge value on every poll.
bool applyDisplayCommand(DeviceSettingsState& state, const BridgeDisplayCommand& command);
bool applyTzCommand(DeviceSettingsState& state, const std::string& tz);

bool loadDeviceSettings(DeviceSettingsState& state);
void saveDeviceSettings(const DeviceSettingsState& state);
