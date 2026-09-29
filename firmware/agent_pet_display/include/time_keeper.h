#pragma once

#include <stdint.h>

// Wall-clock helpers. The RTC holds UTC; local time comes from a POSIX TZ
// string (e.g. "GMT0BST,M3.5.0/1,M10.5.0") that the bridge sends from the Mac.

static constexpr const char* kDefaultPosixTz = "CST-8";      // firmware <= 1.2 behaviour
static constexpr int64_t kEarliestPlausibleEpoch = 1704067200;  // 2024-01-01T00:00:00Z

struct CivilTime {
  int year{0};
  int month{0};    // 1..12
  int day{0};      // 1..31
  int hour{0};
  int minute{0};
  int second{0};
  int weekday{0};  // 0 = Sunday
};

// Proleptic Gregorian <-> Unix seconds, no time zones involved.
int64_t epochFromCivilUtc(const CivilTime& utc);
CivilTime civilUtcFromEpoch(int64_t epoch);
bool isPlausibleEpoch(int64_t epoch);

// Converts a UTC instant to local civil time using a POSIX TZ rule.
// Falls back to kDefaultPosixTz when tz is empty. Returns false if the
// instant is not plausible (RTC never set).
bool localTimeFromEpoch(int64_t epoch, const char* posixTz, CivilTime& out);

// Firmware <= 1.2 wrote China local time (UTC+8) into the RTC. Given what the
// RTC reports, returns the equivalent UTC instant.
int64_t legacyRtcToUtc(const CivilTime& rtcFields);

// Sanity limits for TZ strings we accept from the network.
bool isAcceptablePosixTz(const char* tz);
