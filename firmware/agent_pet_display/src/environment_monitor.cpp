#include "environment_monitor.h"

#ifdef ARDUINO

#include <Arduino.h>
#include <time.h>
#include <Wire.h>

#include "SensorPCF85063.hpp"

namespace {

constexpr int kEnvI2cSclPin = 14;
constexpr int kEnvI2cSdaPin = 13;
constexpr uint8_t kShtc3Address = 0x70;
constexpr uint16_t kShtc3WakeupCommand = 0x3517;
constexpr uint16_t kShtc3SoftResetCommand = 0x805D;
constexpr uint16_t kShtc3MeasurePollingCommand = 0x7866;
constexpr float kShtc3TemperatureOffsetC = 4.0f;

SensorPCF85063 gRtc;
bool gEnvironmentReady = false;

void writeShtc3Command(uint16_t command) {
  Wire.beginTransmission(kShtc3Address);
  Wire.write(static_cast<uint8_t>(command >> 8));
  Wire.write(static_cast<uint8_t>(command & 0xFF));
  Wire.endTransmission();
}

bool readShtc3Bytes(uint8_t* buffer, size_t length) {
  if (Wire.requestFrom(static_cast<int>(kShtc3Address), static_cast<int>(length)) !=
      static_cast<int>(length)) {
    return false;
  }
  for (size_t index = 0; index < length; ++index) {
    buffer[index] = static_cast<uint8_t>(Wire.read());
  }
  return true;
}

uint8_t shtc3Crc(const uint8_t* data, size_t length) {
  uint8_t crc = 0xFF;
  for (size_t index = 0; index < length; ++index) {
    crc ^= data[index];
    for (uint8_t bit = 0; bit < 8; ++bit) {
      crc = (crc & 0x80) ? static_cast<uint8_t>((crc << 1) ^ 0x31) : static_cast<uint8_t>(crc << 1);
    }
  }
  return crc;
}

bool sampleClimate(float& temperatureC, float& humidityPct) {
  writeShtc3Command(kShtc3WakeupCommand);
  delay(2);
  writeShtc3Command(kShtc3MeasurePollingCommand);
  delay(20);

  uint8_t bytes[6] = {0};
  if (!readShtc3Bytes(bytes, sizeof(bytes))) {
    return false;
  }
  if (shtc3Crc(bytes, 2) != bytes[2] || shtc3Crc(bytes + 3, 2) != bytes[5]) {
    return false;
  }

  const uint16_t rawTemperature = static_cast<uint16_t>((bytes[0] << 8) | bytes[1]);
  const uint16_t rawHumidity = static_cast<uint16_t>((bytes[3] << 8) | bytes[4]);
  temperatureC = 175.0f * static_cast<float>(rawTemperature) / 65536.0f - 45.0f -
                 kShtc3TemperatureOffsetC;
  humidityPct = 100.0f * static_cast<float>(rawHumidity) / 65536.0f;
  return true;
}

}  // namespace

void beginEnvironmentMonitor() {
  if (gEnvironmentReady) {
    return;
  }

  if (!gRtc.begin(Wire, kEnvI2cSdaPin, kEnvI2cSclPin)) {
    return;
  }
  writeShtc3Command(kShtc3WakeupCommand);
  delay(2);
  writeShtc3Command(kShtc3SoftResetCommand);
  delay(20);
  gEnvironmentReady = true;
}

EnvironmentState sampleEnvironmentState() {
  EnvironmentState state{};
  if (!gEnvironmentReady) {
    return state;
  }

  const RTC_DateTime rtc = gRtc.getDateTime();
  state.year = rtc.getYear();
  state.month = rtc.getMonth();
  state.day = rtc.getDay();
  state.hour = rtc.getHour();
  state.minute = rtc.getMinute();
  state.second = rtc.getSecond();
  state.weekday = rtc.getWeek();
  state.clockValid = state.year >= 2024 && state.month > 0 && state.day > 0;

  float temperatureC = 0.0f;
  float humidityPct = 0.0f;
  if (sampleClimate(temperatureC, humidityPct)) {
    state.temperatureC = temperatureC;
    state.humidityPct = humidityPct;
    state.climateValid = true;
  }

  return state;
}

bool syncEnvironmentClockFromNtp() {
  if (!gEnvironmentReady) {
    return false;
  }

  configTzTime("CST-8", "pool.ntp.org", "ntp.aliyun.com", "time.cloudflare.com");
  struct tm timeInfo = {};
  if (!getLocalTime(&timeInfo, 5000)) {
    return false;
  }
  gRtc.setDateTime(RTC_DateTime(timeInfo));
  return true;
}

#else

void beginEnvironmentMonitor() {}

EnvironmentState sampleEnvironmentState() {
  return EnvironmentState{};
}

bool syncEnvironmentClockFromNtp() {
  return false;
}

#endif
