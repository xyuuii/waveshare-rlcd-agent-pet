#pragma once

#include <stddef.h>
#include <stdint.h>

#include <string>

#include "bridge_client.h"
#include "focus_controller.h"
#include "models.h"

// Everything network-facing runs in one FreeRTOS task on core 0 so that Wi-Fi
// reconnects and slow HTTP never freeze the clock or the buttons (core 1).

struct NetSnapshot {
  NetworkState network{};
  AgentState agent{};
  BridgeCommands commands{};
  uint32_t pollSeq{0};        // increments on every finished poll (ok or not)
  bool lastPollOk{false};
  uint32_t lastPollOkAtMs{0};
  int64_t serverOffsetMs{0};  // bridge wall clock minus local millis()
  bool serverOffsetKnown{false};
};

void netWorkerBegin(const char* ssid, const char* password, const char* pollUrl);
bool netWorkerSnapshot(NetSnapshot& out);  // false before the task runs
void netWorkerSetFocus(const FocusRequestState& focus);
void netWorkerSetTelemetry(const DeviceTelemetry& telemetry);
void netWorkerSetFastPoll(bool fast);

// ---- easter-egg streaming (producer: net task, consumer: player on core 1)

static constexpr size_t kAnimChunkBytes = 16384;
static constexpr int kAnimChunkSlots = 3;

struct AnimChunkView {
  const uint8_t* data{nullptr};
  size_t length{0};
  uint32_t firstFrame{0};
  uint32_t frameCount{0};
  int slot{-1};
};

void netWorkerStartStream(const std::string& id, uint32_t totalFrames);
void netWorkerStopStream();
// Next chunk in frame order, or false if it has not arrived yet.
bool netWorkerPeekChunk(uint32_t firstFrame, AnimChunkView& out);
void netWorkerReleaseChunk(int slot);
bool netWorkerStreamFailed();
