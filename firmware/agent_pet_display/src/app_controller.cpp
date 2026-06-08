#include "app_controller.h"

#include <Arduino.h>
#include <WiFi.h>

#include "app_config.h"
#include "battery_monitor.h"
#include "bridge_client.h"
#include "environment_monitor.h"
#include "models.h"
#include "pet_state_machine.h"
#include "screen_renderer.h"

namespace {

ScreenRenderer gRenderer;
AgentState gAgent;
PowerState gPower;
EnvironmentState gEnvironment;
ScreenPage gPage = ScreenPage::Overview;
uint32_t gLastBridgePoll = 0;
uint32_t gLastBatterySample = 0;
uint32_t gLastEnvironmentSample = 0;
uint32_t gLastPageToggleAt = 0;
wl_status_t gLastWifiStatus = WL_IDLE_STATUS;
bool gClockSyncAttempted = false;
bool gBootPressed = false;

}  // namespace

void AppController::begin() {
  gRenderer.begin();
  gAgent.connected = false;
  gPower = derivePowerState(3800, false);
  beginEnvironmentMonitor();
  pinMode(kBootButtonPin, INPUT_PULLUP);
  Serial.println("[BOOT] WiFi credentials configured");
  Serial.println("[BOOT] Bridge endpoint configured");
  WiFi.begin(kWifiSsid, kWifiPassword);
}

void AppController::tick() {
  const uint32_t now = millis();
  const wl_status_t wifiStatus = WiFi.status();
  const bool bootPressed = digitalRead(kBootButtonPin) == LOW;

  if (bootPressed && !gBootPressed && now - gLastPageToggleAt >= kPageToggleDebounceMs) {
    gPage = nextScreenPage(gPage);
    gLastPageToggleAt = now;
    Serial.printf("[UI] page=%d\n", static_cast<int>(gPage));
  }
  gBootPressed = bootPressed;

  if (wifiStatus != gLastWifiStatus) {
    if (wifiStatus == WL_CONNECTED) {
      Serial.printf("[WIFI] connected ip=%s\n", WiFi.localIP().toString().c_str());
      gClockSyncAttempted = false;
    } else {
      Serial.printf("[WIFI] status=%d\n", static_cast<int>(wifiStatus));
    }
    gLastWifiStatus = wifiStatus;
  }

  if (wifiStatus == WL_CONNECTED && now - gLastBridgePoll >= kBridgePollMs) {
    AgentState next{};
    if (fetchAgentState(kBridgeUrl, next)) {
      gAgent = next;
      Serial.printf("[BRIDGE] ok source=%d status=%d task=%s\n",
                    static_cast<int>(gAgent.source),
                    static_cast<int>(gAgent.status),
                    gAgent.task.c_str());
    } else {
      gAgent.connected = false;
      Serial.println("[BRIDGE] fetch failed");
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
    if (wifiStatus == WL_CONNECTED && !gEnvironment.clockValid && !gClockSyncAttempted) {
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

  const DisplayState view = deriveDisplayState(gAgent, gPower, gEnvironment, gPage);
  gRenderer.render(view, gPower);
  delay(100);
}
