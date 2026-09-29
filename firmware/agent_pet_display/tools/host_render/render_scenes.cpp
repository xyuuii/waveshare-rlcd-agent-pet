// Renders representative screens with the real firmware drawing code and the
// real U8g2 library, then writes 400x300 PBM files for review.
//
// Usage: render_scenes <output-dir>

#include <Arduino.h>

#include <stdio.h>
#include <sys/stat.h>

#include <string>
#include <vector>

#include "egg_builtin.h"
#include "host_frame.h"
#include "models.h"
#include "pet_state_machine.h"
#include "screen_renderer.h"

namespace {

struct Scene {
  std::string name;
  AgentState agent;
  PowerState power;
  EnvironmentState environment;
  NetworkState network;
  ScreenPage page;
  uint32_t millis;
  DisplaySettings settings{};
};

EnvironmentState sampleEnvironment() {
  EnvironmentState env{};
  env.clockValid = true;
  env.year = 2026;
  env.month = 9;
  env.day = 28;
  env.hour = 10;
  env.minute = 42;
  env.second = 17;
  env.weekday = 1;
  env.temperatureC = 22.6f;
  env.humidityPct = 48.0f;
  env.climateValid = true;
  return env;
}

NetworkState onlineNetwork() {
  NetworkState net{};
  net.wifiKnown = true;
  net.wifiConnected = true;
  net.rssi = -52;
  net.ip = "192.168.1.231";
  return net;
}

AgentState codexAgent(const char* detail, AgentStatus status) {
  AgentState agent{};
  agent.connected = true;
  agent.source = SourceKind::Codex;
  agent.status = status;
  agent.statusDetail = detail;
  agent.task = "refactor bridge relay";
  agent.updatedAt = "2026-09-28T09:42:00Z";
  agent.usageToday = "812k tok";
  agent.usageContext = "189k / 258k";
  agent.usageQuota = "5H 88% WK 92%";
  agent.focusCount = 3;
  return agent;
}

PowerState battery(int percent, int mv) {
  PowerState power{};
  power.percent = percent;
  power.voltageMv = mv;
  power.sampleOk = true;
  return power;
}

std::vector<Scene> buildScenes() {
  std::vector<Scene> scenes;
  scenes.push_back({"overview-codex-thinking", codexAgent("thinking", AgentStatus::Running), battery(84, 3980),
                    sampleEnvironment(), onlineNetwork(), ScreenPage::Overview, 1000});
  scenes.push_back({"usage-codex", codexAgent("tool-use", AgentStatus::Running), battery(84, 3980),
                    sampleEnvironment(), onlineNetwork(), ScreenPage::Usage, 1000});
  AgentState offline{};
  NetworkState wifiOk = onlineNetwork();
  scenes.push_back({"overview-bridge-offline", offline, battery(84, 3980), sampleEnvironment(), wifiOk,
                    ScreenPage::Overview, 1000});

  // Clock page: every style with a busy Codex plus two other agents.
  AgentState multi = codexAgent("thinking", AgentStatus::Running);
  multi.focusCount = 3;
  multi.agentSlots[0] = AgentSlotSummary{"slot_a", SourceKind::Codex, AgentStatus::Running, "refactor", "", true};
  multi.agentSlots[1] = AgentSlotSummary{"slot_b", SourceKind::Hermes, AgentStatus::Completed, "notes", "", true};
  multi.agentSlots[2] = AgentSlotSummary{"slot_c", SourceKind::ClaudeCode, AgentStatus::Idle, "", "", true};
  for (int index = 0; index < kClockStyleCount; ++index) {
    Scene scene{std::string("clock-") + clockStyleName(static_cast<ClockStyle>(index)), multi, battery(84, 3980),
                sampleEnvironment(), onlineNetwork(), ScreenPage::Clock, 1000};
    scene.settings.clockStyle = static_cast<ClockStyle>(index);
    scenes.push_back(scene);
  }

  // Variants: someone is waiting for you, 12-hour afternoon, bridge down, night pet.
  AgentState waiting = codexAgent("needs-attention", AgentStatus::NeedsAttention);
  waiting.source = SourceKind::Hermes;
  waiting.task = "approve shell command";
  waiting.focusCount = 2;
  waiting.agentSlots[0] = AgentSlotSummary{"slot_h", SourceKind::Hermes, AgentStatus::NeedsAttention, "", "", true};
  waiting.agentSlots[1] = AgentSlotSummary{"slot_a", SourceKind::Codex, AgentStatus::Running, "", "", true};
  Scene attention{"clock-segment-attention-12h", waiting, battery(23, 3610), sampleEnvironment(), onlineNetwork(),
                  ScreenPage::Clock, 1000};
  attention.environment.hour = 16;
  attention.environment.minute = 7;
  attention.environment.second = 44;
  attention.settings.clockStyle = ClockStyle::Segment;
  attention.settings.hour12 = true;
  scenes.push_back(attention);
  Scene termAttention = attention;
  termAttention.name = "clock-terminal-attention";
  termAttention.settings.clockStyle = ClockStyle::Terminal;
  termAttention.settings.hour12 = false;
  scenes.push_back(termAttention);

  Scene analogOffline{"clock-analog-bridge-off", offline, battery(84, 3980), sampleEnvironment(), wifiOk,
                      ScreenPage::Clock, 1000};
  analogOffline.environment.hour = 3;
  analogOffline.environment.minute = 51;
  analogOffline.environment.second = 5;
  analogOffline.settings.clockStyle = ClockStyle::Analog;
  scenes.push_back(analogOffline);

  AgentState idle = codexAgent("idle", AgentStatus::Idle);
  idle.task = "";
  Scene night{"clock-pet-night", idle, battery(84, 3980), sampleEnvironment(), onlineNetwork(), ScreenPage::Clock, 1000};
  night.environment.hour = 1;
  night.environment.minute = 12;
  night.settings.clockStyle = ClockStyle::Pet;
  scenes.push_back(night);

  Scene words{"clock-words-2359", idle, battery(84, 3980), sampleEnvironment(), onlineNetwork(), ScreenPage::Clock, 1000};
  words.environment.hour = 23;
  words.environment.minute = 58;
  words.settings.clockStyle = ClockStyle::Words;
  scenes.push_back(words);

  EnvironmentState unsynced = sampleEnvironment();
  unsynced.clockValid = false;
  Scene noTime{"clock-sans-unsynced", offline, battery(84, 3980), unsynced, wifiOk, ScreenPage::Clock, 1000};
  scenes.push_back(noTime);
  return scenes;
}

}  // namespace

int main(int argc, char** argv) {
  const std::string outDir = argc > 1 ? argv[1] : "host-render-out";
  mkdir(outDir.c_str(), 0755);

  ScreenRenderer renderer;
  renderer.begin();

  int failures = 0;
  for (const Scene& scene : buildScenes()) {
    hostSetMillis(scene.millis);
    const DisplayState view =
        deriveDisplayState(scene.agent, scene.power, scene.environment, scene.page, scene.network, scene.settings);
    renderer.render(view, scene.power);
    const std::string path = outDir + "/" + scene.name + ".pbm";
    if (!hostWritePanelPbm(path)) {
      fprintf(stderr, "failed to write %s\n", path.c_str());
      ++failures;
      continue;
    }
    printf("wrote %s\n", path.c_str());
  }
  // Easter egg screens are drawn straight into the panel buffer.
  U8G2* panel = renderer.u8g2();
  const uint32_t eggTimes[] = {400, 2600, 6100, 10500};
  for (uint32_t at : eggTimes) {
    drawBuiltinEggFrame(*panel, at);
    const std::string path = outDir + "/egg-builtin-" + std::to_string(at) + "ms.pbm";
    failures += hostWritePanelPbm(path) ? 0 : 1;
    printf("wrote %s\n", path.c_str());
  }
  drawEggCountdown(*panel, "badapple", 3);
  failures += hostWritePanelPbm(outDir + "/egg-countdown.pbm") ? 0 : 1;
  drawEggMessage(*panel, "STREAM STALLED", "CHECK THE BRIDGE / WIFI");
  failures += hostWritePanelPbm(outDir + "/egg-message.pbm") ? 0 : 1;
  return failures == 0 ? 0 : 1;
}
