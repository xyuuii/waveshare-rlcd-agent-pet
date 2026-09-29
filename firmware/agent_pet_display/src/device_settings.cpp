#include "device_settings.h"

#include "time_keeper.h"

#ifdef ARDUINO
#include <Preferences.h>
#endif

bool applyDisplayCommand(DeviceSettingsState& state, const BridgeDisplayCommand& command) {
  if (!command.present || command.rev == 0 || command.rev == state.displayRev) {
    return false;
  }
  state.displayRev = command.rev;
  if (command.hasStyle) {
    state.display.clockStyle = command.style;
  }
  if (command.hasHour12) {
    state.display.hour12 = command.hour12;
  }
  if (command.hasShowSeconds) {
    state.display.showSeconds = command.showSeconds;
  }
  if (command.hasPage) {
    state.page = command.page;
  }
  return true;
}

bool applyTzCommand(DeviceSettingsState& state, const std::string& tz) {
  if (!isAcceptablePosixTz(tz.c_str()) || tz == state.tz) {
    return false;
  }
  state.tz = tz;
  return true;
}

#ifdef ARDUINO

namespace {
constexpr const char* kNamespace = "rlcdpet";
}

bool loadDeviceSettings(DeviceSettingsState& state) {
  Preferences prefs;
  if (!prefs.begin(kNamespace, true)) {
    return false;
  }
  const uint8_t style = prefs.getUChar("style", 0);
  state.display.clockStyle = style < kClockStyleCount ? static_cast<ClockStyle>(style) : ClockStyle::Sans;
  state.display.hour12 = prefs.getBool("h12", false);
  state.display.showSeconds = prefs.getBool("sec", true);
  const uint8_t page = prefs.getUChar("page", 0);
  state.page = page < kScreenPageCount ? static_cast<ScreenPage>(page) : ScreenPage::Overview;
  state.displayRev = prefs.getUInt("drev", 0);
  const String tz = prefs.getString("tz", "");
  state.tz = isAcceptablePosixTz(tz.c_str()) ? std::string(tz.c_str()) : std::string();
  state.rtcIsUtc = prefs.getBool("rtcutc", false);
  prefs.end();
  return true;
}

void saveDeviceSettings(const DeviceSettingsState& state) {
  Preferences prefs;
  if (!prefs.begin(kNamespace, false)) {
    return;
  }
  prefs.putUChar("style", static_cast<uint8_t>(state.display.clockStyle));
  prefs.putBool("h12", state.display.hour12);
  prefs.putBool("sec", state.display.showSeconds);
  prefs.putUChar("page", static_cast<uint8_t>(state.page));
  prefs.putUInt("drev", state.displayRev);
  prefs.putString("tz", state.tz.c_str());
  prefs.putBool("rtcutc", state.rtcIsUtc);
  prefs.end();
}

#else

bool loadDeviceSettings(DeviceSettingsState& state) {
  (void)state;
  return false;
}

void saveDeviceSettings(const DeviceSettingsState& state) {
  (void)state;
}

#endif
