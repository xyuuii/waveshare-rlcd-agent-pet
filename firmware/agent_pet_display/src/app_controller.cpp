#include "app_controller.h"

#include <Arduino.h>
#include <WiFi.h>

#include "app_config.h"
#include "battery_monitor.h"
#include "button_actions.h"
#include "bridge_client.h"
#include "environment_monitor.h"
#include "focus_controller.h"
#include "models.h"
#include "pet_state_machine.h"
#include "screen_renderer.h"

namespace {

ScreenRenderer gRenderer;
AgentState gAgent;
PowerState gPower;
EnvironmentState gEnvironment;
NetworkState gNetwork;
ScreenPage gPage = ScreenPage::Overview;
uint32_t gLastBridgePoll = 0;
uint32_t gLastBatterySample = 0;
uint32_t gLastEnvironmentSample = 0;
uint32_t gLastPageToggleAt = 0;
uint32_t gLastAgentFocusAt = 0;
uint32_t gLastWifiReconnect = 0;
uint32_t gLastWifiReport = 0;
uint32_t gWifiReconnectAttempts = 0;
wl_status_t gLastWifiStatus = WL_IDLE_STATUS;
bool gClockSyncAttempted = false;
ButtonGestureState gBootButton;
ButtonGestureState gAgentFocusButton;
FocusRequestState gFocusRequest;

void togglePage(uint32_t now) {
  if (now - gLastPageToggleAt < kPageToggleDebounceMs) {
    return;
  }
  gPage = nextScreenPage(gPage);
  gLastPageToggleAt = now;
  Serial.printf("[UI] page=%d\n", static_cast<int>(gPage));
}

void cycleAgentFocus(uint32_t now) {
  if (now - gLastAgentFocusAt < kPageToggleDebounceMs) {
    return;
  }
  advanceFocusSelection(gFocusRequest, gAgent, now, kFocusAutoReturnMs);
  gLastAgentFocusAt = now;
  Serial.printf("[UI] focus=%s index=%d count=%d id=%s\n",
                gFocusRequest.mode == LocalFocusMode::Pinned ? "pinned" : "auto",
                gFocusRequest.visibleIndex,
                gFocusRequest.visibleCount,
                gFocusRequest.focusId.c_str());
}

void handleButtonAction(ButtonUiAction action, uint32_t now) {
  if (action == ButtonUiAction::Page) {
    togglePage(now);
    return;
  }
  if (action == ButtonUiAction::AgentFocus) {
    cycleAgentFocus(now);
  }
}

}  // namespace

void AppController::begin() {
  gRenderer.begin();
  gAgent.connected = false;
  gPower = derivePowerState(3800, false);
  beginEnvironmentMonitor();
  pinMode(kBootButtonPin, INPUT_PULLUP);
  pinMode(kAgentFocusButtonPin, INPUT_PULLUP);
  Serial.printf("[BOOT] WiFi SSID=%s\n", kWifiSsid);
  Serial.printf("[BOOT] Bridge URL=%s\n", kBridgeUrl);
  WiFi.mode(WIFI_STA);
  WiFi.persistent(false);
  WiFi.setAutoReconnect(true);
  WiFi.begin(kWifiSsid, kWifiPassword);
}

void AppController::tick() {
  const uint32_t now = millis();
  const wl_status_t wifiStatus = WiFi.status();
  const bool wifiConnected = wifiStatus == WL_CONNECTED;
  const bool wifiConnecting =
      wifiStatus == WL_IDLE_STATUS || wifiStatus == WL_DISCONNECTED || wifiStatus == WL_SCAN_COMPLETED;
  const bool bootPressed = digitalRead(kBootButtonPin) == LOW;
  const bool agentFocusPressed = digitalRead(kAgentFocusButtonPin) == LOW;

  gNetwork.wifiKnown = true;
  gNetwork.wifiConnected = wifiConnected;
  gNetwork.wifiConnecting = wifiConnecting && !wifiConnected;
  gNetwork.wifiStatusCode = static_cast<int>(wifiStatus);
  gNetwork.reconnectAttempts = gWifiReconnectAttempts;
  if (wifiConnected) {
    gNetwork.rssi = WiFi.RSSI();
    gNetwork.ip = WiFi.localIP().toString().c_str();
  } else {
    gNetwork.rssi = 0;
    gNetwork.ip.clear();
    gAgent.connected = false;
  }

  expireFocusIfNeeded(gFocusRequest, now);
  handleButtonAction(updateButtonGesture(gBootButton,
                                         bootPressed,
                                         now,
                                         kPageToggleDebounceMs,
                                         kButtonLongPressMs,
                                         ButtonUiAction::Page,
                                         ButtonUiAction::AgentFocus),
                     now);
  handleButtonAction(updateButtonGesture(gAgentFocusButton,
                                         agentFocusPressed,
                                         now,
                                         kPageToggleDebounceMs,
                                         kButtonLongPressMs,
                                         ButtonUiAction::AgentFocus,
                                         ButtonUiAction::AgentFocus),
                     now);

  if (wifiStatus != gLastWifiStatus) {
    if (wifiConnected) {
      Serial.printf("[WIFI] connected ip=%s\n", WiFi.localIP().toString().c_str());
      Serial.printf("[WIFI] rssi=%d dBm\n", WiFi.RSSI());
      gClockSyncAttempted = false;
      gWifiReconnectAttempts = 0;
    } else {
      Serial.printf("[WIFI] status=%d\n", static_cast<int>(wifiStatus));
    }
    gLastWifiStatus = wifiStatus;
  }

  if (!wifiConnected && now - gLastWifiReconnect >= kWifiReconnectMs) {
    gWifiReconnectAttempts += 1;
    Serial.printf("[WIFI] reconnect attempt=%lu status=%d\n",
                  static_cast<unsigned long>(gWifiReconnectAttempts),
                  static_cast<int>(wifiStatus));
    WiFi.disconnect(false);
    WiFi.begin(kWifiSsid, kWifiPassword);
    gLastWifiReconnect = now;
  }

  if (now - gLastWifiReport >= kWifiReportMs) {
    if (wifiConnected) {
      Serial.printf("[WIFI] ok ip=%s rssi=%d dBm\n",
                    WiFi.localIP().toString().c_str(),
                    WiFi.RSSI());
    } else {
      Serial.printf("[WIFI] waiting status=%d attempts=%lu\n",
                    static_cast<int>(wifiStatus),
                    static_cast<unsigned long>(gWifiReconnectAttempts));
    }
    gLastWifiReport = now;
  }

  if (wifiConnected && now - gLastBridgePoll >= kBridgePollMs) {
    AgentState next{};
    const std::string pollUrl = buildFocusedPollUrl(kBridgeUrl, gFocusRequest);
    if (fetchAgentState(pollUrl.c_str(), next)) {
      gAgent = next;
      gNetwork.wifiKnown = true;
      gNetwork.wifiConnected = true;
      gNetwork.wifiConnecting = false;
      gNetwork.wifiStatusCode = static_cast<int>(wifiStatus);
      gNetwork.rssi = WiFi.RSSI();
      gNetwork.ip = WiFi.localIP().toString().c_str();
      syncFocusSelectionFromBridge(gFocusRequest, gAgent, now, kFocusAutoReturnMs);
      Serial.printf("[BRIDGE] ok source=%d status=%d focus=%s index=%d count=%d task=%s\n",
                    static_cast<int>(gAgent.source),
                    static_cast<int>(gAgent.status),
                    gFocusRequest.mode == LocalFocusMode::Pinned ? "pinned" : "auto",
                    gFocusRequest.visibleIndex,
                    gFocusRequest.visibleCount,
                    gAgent.task.c_str());
    } else {
      gAgent.connected = false;
      Serial.printf("[BRIDGE] fetch failed wifi_ip=%s rssi=%d\n",
                    WiFi.localIP().toString().c_str(),
                    WiFi.RSSI());
    }
    gLastBridgePoll = now;
  }

  if (now - gLastBatterySample >= kBatterySampleMs) {
    gPower = samplePowerState(false);
    Serial.printf("[POWER] sample_ok=%d battery=%d%% voltage=%dmV low=%d charging=%d\n",
                  gPower.sampleOk ? 1 : 0,
                  gPower.percent,
                  gPower.voltageMv,
                  gPower.lowBattery ? 1 : 0,
                  gPower.charging ? 1 : 0);
    gLastBatterySample = now;
  }

  if (now - gLastEnvironmentSample >= kEnvironmentSampleMs) {
    gEnvironment = sampleEnvironmentState();
    if (wifiConnected && !gEnvironment.clockValid && !gClockSyncAttempted) {
      gClockSyncAttempted = true;
      if (syncEnvironmentClockFromNtp()) {
        Serial.println("[ENV] RTC synced from NTP");
        gEnvironment = sampleEnvironmentState();
      } else {
        Serial.println("[ENV] RTC sync failed");
      }
    }
    Serial.printf("[ENV] clock=%d %04d/%02d/%02d %02d:%02d temp=%.1fC humidity=%.0f%% climate=%d\n",
                  gEnvironment.clockValid ? 1 : 0,
                  gEnvironment.year,
                  gEnvironment.month,
                  gEnvironment.day,
                  gEnvironment.hour,
                  gEnvironment.minute,
                  static_cast<double>(gEnvironment.temperatureC),
                  static_cast<double>(gEnvironment.humidityPct),
                  gEnvironment.climateValid ? 1 : 0);
    gLastEnvironmentSample = now;
  }

  const DisplayState view = deriveDisplayState(gAgent, gPower, gEnvironment, gPage, gNetwork);
  gRenderer.render(view, gPower);
  delay(100);
}
