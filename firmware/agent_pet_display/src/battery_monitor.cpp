#include "battery_monitor.h"

#ifdef ARDUINO
#include <Arduino.h>
#include <esp_adc_cal.h>
#include <driver/adc.h>
#endif

#include "app_config.h"

namespace {

#ifdef ARDUINO
esp_adc_cal_characteristics_t gBatteryChars{};
#endif
bool gBatteryReady = false;

int clampPercent(int value) {
  if (value < 0) {
    return 0;
  }
  if (value > 100) {
    return 100;
  }
  return value;
}

}  // namespace

void initBatterySampler() {
#ifdef ARDUINO
  if (gBatteryReady) {
    return;
  }

  if (adc1_config_width(ADC_WIDTH_BIT_12) != ESP_OK) {
    return;
  }
  if (adc1_config_channel_atten(ADC1_CHANNEL_3, ADC_ATTEN_DB_12) != ESP_OK) {
    return;
  }

  esp_adc_cal_characterize(ADC_UNIT_1, ADC_ATTEN_DB_12, ADC_WIDTH_BIT_12, 0, &gBatteryChars);
  gBatteryReady = true;
#endif
}

bool sampleChargingState() {
#ifdef ARDUINO
  if (kChargeSensePin < 0) {
    return false;
  }

  pinMode(kChargeSensePin, INPUT);
  return digitalRead(kChargeSensePin) == HIGH;
#else
  return false;
#endif
}

int mapBatteryPercent(int32_t voltageMv) {
  if (voltageMv >= 4200) {
    return 100;
  }
  if (voltageMv <= 3300) {
    return 0;
  }
  return clampPercent(static_cast<int>((voltageMv - 3300) * 100 / 900));
}

PowerState derivePowerState(int32_t voltageMv, bool charging) {
  PowerState power{};
  power.voltageMv = static_cast<int>(voltageMv);
  power.percent = mapBatteryPercent(voltageMv);
  power.charging = charging;
  power.lowBattery = voltageMv <= 3450;
  power.sampleOk = voltageMv > 0;
  return power;
}

PowerState samplePowerState(bool charging) {
#ifdef ARDUINO
  if (!gBatteryReady) {
    initBatterySampler();
  }
  if (!gBatteryReady) {
    return derivePowerState(0, charging);
  }

  int raw = 0;
  int mv = 0;
  raw = adc1_get_raw(ADC1_CHANNEL_3);
  if (raw < 0) {
    return derivePowerState(0, charging);
  }
  mv = static_cast<int>(esp_adc_cal_raw_to_voltage(static_cast<uint32_t>(raw), &gBatteryChars));

  const int32_t batteryMv = static_cast<int32_t>(mv) * 3;
  return derivePowerState(batteryMv, charging || sampleChargingState());
#else
  return derivePowerState(0, charging);
#endif
}
