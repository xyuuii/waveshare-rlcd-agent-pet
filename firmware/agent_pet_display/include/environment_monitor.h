#pragma once

#include "models.h"

void beginEnvironmentMonitor();
EnvironmentState sampleEnvironmentState();
bool syncEnvironmentClockFromNtp();
