#include "app_controller.h"

#include <Arduino.h>

#include "app_config.h"
#include "battery_monitor.h"
#include "bridge_client.h"
#include "button_actions.h"
#include "device_settings.h"
#include "egg_player.h"
#include "environment_monitor.h"
#include "firmware_version.h"
#include "focus_controller.h"
#include "models.h"
#include "net_worker.h"
#include "pet_state_machine.h"
#include "screen_renderer.h"

namespace {

ScreenRenderer gRenderer;
DeviceSettingsState gSettings;
AgentState gAgent;
PowerState gPower;
EnvironmentState gEnvironment;
NetworkState gNetwork;
NetSnapshot gSnapshot;
FocusRequestState gFocusRequest;
ButtonGestureState gBootButton;
ButtonGestureState gKeyButton;
ChordGestureState gEggChord;

uint32_t gLastClockSample = 0;
uint32_t gLastClimateSample = 0;
uint32_t gLastBatterySample = 0;
uint32_t gLastRender = 0;
uint32_t gLastTelemetry = 0;
uint32_t gLastPollSeen = 0;
uint32_t gLastRtcWrite = 0;
bool gRtcWrittenThisBoot = false;
bool gSettingsDirty = false;
uint32_t gSettingsChangedAt = 0;
bool gForceRender = true;

uint32_t gEggRequest = 0;       // counter sent to the bridge
uint32_t gEggRequestedAt = 0;   // waiting for the bridge to answer
uint32_t gLastEggRev = 0;       // last egg command we started
bool gEggWasActive = false;

void markSettingsDirty(uint32_t now) {
  gSettingsDirty = true;
  gSettingsChangedAt = now;
}

void saveSettingsIfDue(uint32_t now) {
  if (gSettingsDirty && now - gSettingsChangedAt >= kSettingsSaveDelayMs) {
    saveDeviceSettings(gSettings);
    gSettingsDirty = false;
    Serial.println("[CFG] saved");
  }
}

void setPage(ScreenPage page, uint32_t now) {
  if (gSettings.page == page) {
    return;
  }
  gSettings.page = page;
  markSettingsDirty(now);
  gForceRender = true;
  Serial.printf("[UI] page=%s\n", screenPageName(page));
}

void cycleAgentFocus(uint32_t now) {
  advanceFocusSelection(gFocusRequest, gAgent, now, kFocusAutoReturnMs);
  netWorkerSetFocus(gFocusRequest);
  gForceRender = true;
  Serial.printf("[UI] focus=%s index=%d count=%d\n",
                gFocusRequest.mode == LocalFocusMode::Pinned ? "pinned" : "auto",
                gFocusRequest.visibleIndex,
                gFocusRequest.visibleCount);
}

void requestEgg(uint32_t now) {
  gEggRequest += 1;
  gEggRequestedAt = now;
  netWorkerSetFastPoll(true);
  Serial.printf("[EGG] requested #%lu\n", static_cast<unsigned long>(gEggRequest));
}

void handleAction(ButtonUiAction action, uint32_t now) {
  if (action == ButtonUiAction::None) {
    return;
  }
  if (eggActive()) {
    // Any button stops the show.
    eggStop();
    return;
  }
  switch (contextualAction(action, gSettings.page)) {
    case ButtonUiAction::Page:
      setPage(nextScreenPage(gSettings.page), now);
      break;
    case ButtonUiAction::AgentFocus:
      cycleAgentFocus(now);
      break;
    case ButtonUiAction::ClockStyle:
      gSettings.display.clockStyle = nextClockStyle(gSettings.display.clockStyle);
      markSettingsDirty(now);
      gForceRender = true;
      Serial.printf("[UI] clock=%s\n", clockStyleName(gSettings.display.clockStyle));
      break;
    case ButtonUiAction::Egg:
      requestEgg(now);
      break;
    default:
      break;
  }
}

void readButtons(uint32_t now) {
  const bool bootPressed = digitalRead(kBootButtonPin) == LOW;
  const bool keyPressed = digitalRead(kAgentFocusButtonPin) == LOW;

  const ButtonUiAction bootAction = updateButtonGestureEx(gBootButton,
                                                          bootPressed,
                                                          now,
                                                          kButtonLongPressMs,
                                                          kButtonVeryLongPressMs,
                                                          ButtonUiAction::Page,
                                                          ButtonUiAction::AgentFocus,
                                                          ButtonUiAction::Egg);
  const ButtonUiAction keyAction = updateButtonGestureEx(gKeyButton,
                                                         keyPressed,
                                                         now,
                                                         kButtonLongPressMs,
                                                         0,
                                                         ButtonUiAction::AgentFocus,
                                                         ButtonUiAction::AgentFocus,
                                                         ButtonUiAction::None);
  const bool chordFired =
      updateChordGesture(gEggChord, gBootButton, gKeyButton, bootPressed, keyPressed, now, kEggChordHoldMs);
  if (chordFired) {
    handleAction(ButtonUiAction::Egg, now);
    return;
  }
  if (gEggChord.active) {
    return;  // both buttons are down: single-button gestures are swallowed
  }
  handleAction(bootAction, now);
  handleAction(keyAction, now);
}

void sampleSensors(uint32_t now) {
  if (gLastClockSample == 0 || now - gLastClockSample >= kClockSampleMs) {
    const int previousSecond = gEnvironment.second;
    sampleClock(gEnvironment, gSettings.tz.c_str());
    if (gEnvironment.second != previousSecond) {
      gForceRender = true;
    }
    gLastClockSample = now;
  }
  if (gLastClimateSample == 0 || now - gLastClimateSample >= kClimateSampleMs) {
    sampleClimate(gEnvironment);
    gLastClimateSample = now;
  }
  if (gLastBatterySample == 0 || now - gLastBatterySample >= kBatterySampleMs) {
    gPower = samplePowerState(false);
    gLastBatterySample = now;
  }
  // SNTP sets the system clock asynchronously; mirror it into the RTC now and then.
  if (systemClockPlausible() && (!gRtcWrittenThisBoot || now - gLastRtcWrite >= kRtcResyncMs)) {
    if (writeRtcFromSystemClock()) {
      gRtcWrittenThisBoot = true;
      gLastRtcWrite = now;
      Serial.println("[TIME] RTC synced from NTP (UTC)");
    }
  }
}

void applyBridge(uint32_t now) {
  if (!netWorkerSnapshot(gSnapshot)) {
    return;
  }
  gNetwork = gSnapshot.network;
  if (gSnapshot.pollSeq == gLastPollSeen) {
    if (!gNetwork.wifiConnected) {
      gAgent.connected = false;
    }
    return;
  }
  gLastPollSeen = gSnapshot.pollSeq;
  gAgent = gSnapshot.agent;
  gForceRender = true;
  if (!gSnapshot.lastPollOk) {
    return;
  }
  syncFocusSelectionFromBridge(gFocusRequest, gAgent, now, kFocusAutoReturnMs);
  netWorkerSetFocus(gFocusRequest);

  const BridgeCommands& commands = gSnapshot.commands;
  if (applyDisplayCommand(gSettings, commands.display)) {
    markSettingsDirty(now);
    Serial.printf("[CFG] from bridge rev=%lu clock=%s page=%s\n",
                  static_cast<unsigned long>(gSettings.displayRev),
                  clockStyleName(gSettings.display.clockStyle),
                  screenPageName(gSettings.page));
  }
  if (commands.hasTz && applyTzCommand(gSettings, commands.tz)) {
    markSettingsDirty(now);
    Serial.printf("[TIME] tz=%s\n", gSettings.tz.c_str());
  }
  if (commands.egg.present && commands.egg.rev != gLastEggRev) {
    gLastEggRev = commands.egg.rev;
    gEggRequestedAt = 0;
    netWorkerSetFastPoll(false);
    eggStartStream(commands.egg, gSnapshot, now);
  }
}

void updateTelemetry(uint32_t now) {
  if (gLastTelemetry != 0 && now - gLastTelemetry < 1000) {
    return;
  }
  gLastTelemetry = now;
  DeviceTelemetry telemetry{};
  telemetry.firmware = kFirmwareVersion;
  telemetry.batteryValid = gPower.sampleOk;
  telemetry.batteryPercent = gPower.percent;
  telemetry.batteryMv = gPower.voltageMv;
  telemetry.charging = gPower.charging;
  telemetry.climateValid = gEnvironment.climateValid;
  telemetry.temperatureC = gEnvironment.temperatureC;
  telemetry.humidityPct = gEnvironment.humidityPct;
  telemetry.rssi = gNetwork.wifiConnected ? gNetwork.rssi : 0;
  telemetry.uptimeS = now / 1000;
  telemetry.page = gSettings.page;
  telemetry.clockStyle = gSettings.display.clockStyle;
  telemetry.settingsRev = gSettings.displayRev;
  telemetry.eggRev = gLastEggRev;
  telemetry.freeHeap = ESP.getFreeHeap();
  telemetry.timeValid = gEnvironment.clockValid;
  telemetry.eggRequest = gEggRequest;
  netWorkerSetTelemetry(telemetry);
}

void checkEggRequestTimeout(uint32_t now) {
  // The bridge had ~3 s to announce an animation; otherwise play the built-in one.
  if (gEggRequestedAt != 0 && now - gEggRequestedAt > 3000) {
    gEggRequestedAt = 0;
    netWorkerSetFastPoll(false);
    if (!eggActive()) {
      eggStartBuiltin(now);
    }
  }
}

}  // namespace

void AppController::begin() {
  loadDeviceSettings(gSettings);
  gRenderer.begin();
  gAgent.connected = false;
  gPower = derivePowerState(3800, false);
  beginEnvironmentMonitor();
  if (!gSettings.rtcIsUtc) {
    const bool migrated = migrateLegacyRtcToUtc();
    gSettings.rtcIsUtc = true;
    saveDeviceSettings(gSettings);
    Serial.printf("[TIME] RTC convention -> UTC (%s)\n", migrated ? "converted from CST" : "nothing to convert");
  }
  pinMode(kBootButtonPin, INPUT_PULLUP);
  pinMode(kAgentFocusButtonPin, INPUT_PULLUP);
  Serial.printf("[BOOT] firmware=%s\n", kFirmwareVersion);
  Serial.printf("[BOOT] WiFi SSID=%s\n", kWifiSsid);
  Serial.printf("[BOOT] Bridge URL=%s\n", redactUrlToken(kBridgeUrl).c_str());
  Serial.printf("[BOOT] page=%s clock=%s tz=%s\n",
                screenPageName(gSettings.page),
                clockStyleName(gSettings.display.clockStyle),
                gSettings.tz.empty() ? "(default)" : gSettings.tz.c_str());
  netWorkerBegin(kWifiSsid, kWifiPassword, kBridgeUrl);
  netWorkerSetFocus(gFocusRequest);
}

void AppController::tick() {
  const uint32_t now = millis();

  readButtons(now);
  applyBridge(now);
  sampleSensors(now);
  updateTelemetry(now);
  checkEggRequestTimeout(now);
  saveSettingsIfDue(now);

  const LocalFocusMode focusBefore = gFocusRequest.mode;
  expireFocusIfNeeded(gFocusRequest, now);
  if (focusBefore != gFocusRequest.mode) {
    netWorkerSetFocus(gFocusRequest);  // pinned agent timed out back to AUTO
    gForceRender = true;
  }

  U8G2* panel = gRenderer.u8g2();
  if (eggActive() && panel) {
    eggTick(*panel, now);
    gEggWasActive = true;
    delay(2);
    return;
  }
  if (gEggWasActive) {
    gEggWasActive = false;
    gRenderer.invalidate();
    gForceRender = true;
  }

  if (gForceRender || now - gLastRender >= kRenderIntervalMs) {
    const DisplayState view =
        deriveDisplayState(gAgent, gPower, gEnvironment, gSettings.page, gNetwork, gSettings.display);
    gRenderer.render(view, gPower);
    gLastRender = now;
    gForceRender = false;
  }
  delay(15);
}
