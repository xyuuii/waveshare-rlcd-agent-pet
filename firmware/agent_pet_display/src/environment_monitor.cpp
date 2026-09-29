#include "environment_monitor.h"

#include "time_keeper.h"

#ifdef ARDUINO

#include <Arduino.h>
#include <Wire.h>
#include <time.h>

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

bool readRtcFields(CivilTime& out) {
  if (!gEnvironmentReady) {
    return false;
  }
  RTC_DateTime rtc = gRtc.getDateTime();
  out.year = rtc.getYear();
  out.month = rtc.getMonth();
  out.day = rtc.getDay();
  out.hour = rtc.getHour();
  out.minute = rtc.getMinute();
  out.second = rtc.getSecond();
  out.weekday = rtc.getWeek();
  return out.year >= 2024 && out.month >= 1 && out.month <= 12 && out.day >= 1 && out.day <= 31 &&
         out.hour < 24 && out.minute < 60 && out.second < 60;
}

void writeRtcEpoch(int64_t epoch) {
  const CivilTime utc = civilUtcFromEpoch(epoch);
  gRtc.setDateTime(RTC_DateTime(static_cast<uint16_t>(utc.year),
                                static_cast<uint8_t>(utc.month),
                                static_cast<uint8_t>(utc.day),
                                static_cast<uint8_t>(utc.hour),
                                static_cast<uint8_t>(utc.minute),
                                static_cast<uint8_t>(utc.second),
                                static_cast<uint8_t>(utc.weekday)));
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

bool migrateLegacyRtcToUtc() {
  CivilTime fields{};
  if (!readRtcFields(fields)) {
    return false;
  }
  writeRtcEpoch(legacyRtcToUtc(fields));
  return true;
}

void sampleClock(EnvironmentState& state, const char* posixTz) {
  state.clockValid = false;
  int64_t epoch = 0;
  CivilTime utc{};
  if (readRtcFields(utc)) {
    epoch = epochFromCivilUtc(utc);
  }
  if (!isPlausibleEpoch(epoch) && systemClockPlausible()) {
    epoch = static_cast<int64_t>(time(nullptr));
  }
  CivilTime local{};
  if (!localTimeFromEpoch(epoch, posixTz, local)) {
    return;
  }
  state.year = local.year;
  state.month = local.month;
  state.day = local.day;
  state.hour = local.hour;
  state.minute = local.minute;
  state.second = local.second;
  state.weekday = local.weekday;
  state.clockValid = true;
}

void sampleClimate(EnvironmentState& state) {
  state.climateValid = false;
  if (!gEnvironmentReady) {
    return;
  }
  writeShtc3Command(kShtc3WakeupCommand);
  delay(2);
  writeShtc3Command(kShtc3MeasurePollingCommand);
  delay(20);

  uint8_t bytes[6] = {0};
  if (!readShtc3Bytes(bytes, sizeof(bytes))) {
    return;
  }
  if (shtc3Crc(bytes, 2) != bytes[2] || shtc3Crc(bytes + 3, 2) != bytes[5]) {
    return;
  }
  const uint16_t rawTemperature = static_cast<uint16_t>((bytes[0] << 8) | bytes[1]);
  const uint16_t rawHumidity = static_cast<uint16_t>((bytes[3] << 8) | bytes[4]);
  state.temperatureC = 175.0f * static_cast<float>(rawTemperature) / 65536.0f - 45.0f - kShtc3TemperatureOffsetC;
  state.humidityPct = 100.0f * static_cast<float>(rawHumidity) / 65536.0f;
  state.climateValid = true;
}

bool systemClockPlausible() {
  return isPlausibleEpoch(static_cast<int64_t>(time(nullptr)));
}

bool writeRtcFromSystemClock() {
  if (!gEnvironmentReady || !systemClockPlausible()) {
    return false;
  }
  writeRtcEpoch(static_cast<int64_t>(time(nullptr)));
  return true;
}

#else

void beginEnvironmentMonitor() {}

bool migrateLegacyRtcToUtc() {
  return false;
}

void sampleClock(EnvironmentState& state, const char* posixTz) {
  (void)posixTz;
  state.clockValid = false;
}

void sampleClimate(EnvironmentState& state) {
  state.climateValid = false;
}

bool systemClockPlausible() {
  return false;
}

bool writeRtcFromSystemClock() {
  return false;
}

#endif
