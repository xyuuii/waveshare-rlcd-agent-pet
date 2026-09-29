#pragma once

#include "models.h"

DisplayState deriveDisplayState(const AgentState& agent,
                                const PowerState& power,
                                const EnvironmentState& environment,
                                ScreenPage page,
                                const NetworkState& network = NetworkState{},
                                const DisplaySettings& settings = DisplaySettings{});
