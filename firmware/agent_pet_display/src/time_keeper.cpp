#include "time_keeper.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>

namespace {

// Howard Hinnant's days_from_civil / civil_from_days (public domain).
int64_t daysFromCivil(int64_t year, unsigned month, unsigned day) {
  year -= month <= 2 ? 1 : 0;
  const int64_t era = (year >= 0 ? year : year - 399) / 400;
  const unsigned yoe = static_cast<unsigned>(year - era * 400);
  const unsigned doy = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + static_cast<int64_t>(doe) - 719468;
}

void civilFromDays(int64_t days, int& year, int& month, int& day) {
  days += 719468;
  const int64_t era = (days >= 0 ? days : days - 146096) / 146097;
  const unsigned doe = static_cast<unsigned>(days - era * 146097);
  const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  const int64_t y = static_cast<int64_t>(yoe) + era * 400;
  const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  const unsigned mp = (5 * doy + 2) / 153;
  day = static_cast<int>(doy - (153 * mp + 2) / 5 + 1);
  month = static_cast<int>(mp < 10 ? mp + 3 : mp - 9);
  year = static_cast<int>(y + (month <= 2 ? 1 : 0));
}

int64_t floorDiv(int64_t value, int64_t divisor) {
  int64_t quotient = value / divisor;
  if ((value % divisor != 0) && ((value < 0) != (divisor < 0))) {
    --quotient;
  }
  return quotient;
}

void applyTz(const char* tz) {
  const char* wanted = (tz && tz[0]) ? tz : kDefaultPosixTz;
  // Compare with the live environment: configTime() on the ESP32 rewrites TZ.
  const char* current = getenv("TZ");
  if (current && strcmp(current, wanted) == 0) {
    return;
  }
  setenv("TZ", wanted, 1);
  tzset();
}

}  // namespace

int64_t epochFromCivilUtc(const CivilTime& utc) {
  const int64_t days = daysFromCivil(utc.year, static_cast<unsigned>(utc.month), static_cast<unsigned>(utc.day));
  return days * 86400 + static_cast<int64_t>(utc.hour) * 3600 + static_cast<int64_t>(utc.minute) * 60 + utc.second;
}

CivilTime civilUtcFromEpoch(int64_t epoch) {
  CivilTime out{};
  const int64_t days = floorDiv(epoch, 86400);
  int64_t secondsOfDay = epoch - days * 86400;
  civilFromDays(days, out.year, out.month, out.day);
  out.hour = static_cast<int>(secondsOfDay / 3600);
  secondsOfDay %= 3600;
  out.minute = static_cast<int>(secondsOfDay / 60);
  out.second = static_cast<int>(secondsOfDay % 60);
  out.weekday = static_cast<int>(((days % 7) + 7 + 4) % 7);  // 1970-01-01 was a Thursday
  return out;
}

bool isPlausibleEpoch(int64_t epoch) {
  return epoch >= kEarliestPlausibleEpoch && epoch < 4102444800LL;  // before 2100
}

bool localTimeFromEpoch(int64_t epoch, const char* posixTz, CivilTime& out) {
  if (!isPlausibleEpoch(epoch)) {
    return false;
  }
  applyTz(isAcceptablePosixTz(posixTz) ? posixTz : kDefaultPosixTz);
  const time_t value = static_cast<time_t>(epoch);
  struct tm local {};
  if (!localtime_r(&value, &local)) {
    return false;
  }
  out.year = local.tm_year + 1900;
  out.month = local.tm_mon + 1;
  out.day = local.tm_mday;
  out.hour = local.tm_hour;
  out.minute = local.tm_min;
  out.second = local.tm_sec;
  out.weekday = local.tm_wday;
  return true;
}

int64_t legacyRtcToUtc(const CivilTime& rtcFields) {
  return epochFromCivilUtc(rtcFields) - 8 * 3600;
}

bool isAcceptablePosixTz(const char* tz) {
  if (!tz) {
    return false;
  }
  const size_t length = strlen(tz);
  if (length < 3 || length > 63) {
    return false;
  }
  const char first = tz[0];
  if (!((first >= 'A' && first <= 'Z') || (first >= 'a' && first <= 'z') || first == '<')) {
    return false;
  }
  for (size_t index = 0; index < length; ++index) {
    const char ch = tz[index];
    const bool ok = (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') ||
                    ch == '<' || ch == '>' || ch == '+' || ch == '-' || ch == ',' || ch == '.' ||
                    ch == ':' || ch == '/';
    if (!ok) {
      return false;
    }
  }
  return true;
}
