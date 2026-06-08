#pragma once

#include <stdint.h>

#include "models.h"

int mapBatteryPercent(int32_t voltageMv);
PowerState derivePowerState(int32_t voltageMv, bool charging);
PowerState samplePowerState(bool charging);
