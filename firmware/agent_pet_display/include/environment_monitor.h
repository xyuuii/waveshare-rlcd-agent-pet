#pragma once

#include "models.h"

void beginEnvironmentMonitor();

// Firmware <= 1.2 kept China local time in the RTC. Rewrites it as UTC once.
// Returns true when a plausible legacy value was converted.
bool migrateLegacyRtcToUtc();

// Clock fields in local time (posixTz), from the RTC (UTC) or, if the RTC is
// not set yet, from an SNTP-set system clock.
void sampleClock(EnvironmentState& state, const char* posixTz);

// SHTC3 temperature / humidity.
void sampleClimate(EnvironmentState& state);

// True once SNTP (or anything else) has set a plausible system time.
bool systemClockPlausible();

// Copies the system clock into the RTC as UTC. Returns true when written.
bool writeRtcFromSystemClock();
