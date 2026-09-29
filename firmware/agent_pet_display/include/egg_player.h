#pragma once

#include <stdint.h>

#include "bridge_client.h"
#include "net_worker.h"

class U8G2;

// Full-screen easter-egg playback. While active it owns the panel.
void eggStartBuiltin(uint32_t nowMs);
// Streams an RLA1 animation announced by the bridge, starting at the bridge's
// start time (so the dashboard can play the soundtrack in sync).
void eggStartStream(const BridgeEggCommand& command, const NetSnapshot& snapshot, uint32_t nowMs);
void eggStop();
bool eggActive();
// Advances playback; draws and pushes frames when due.
void eggTick(U8G2& g, uint32_t nowMs);
