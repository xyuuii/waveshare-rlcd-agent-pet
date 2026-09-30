# Notes for coding agents

This repository has two parts that talk over HTTP:

- `firmware/agent_pet_display`: PlatformIO/Arduino firmware for the Waveshare
  ESP32-S3-RLCD-4.2. The board build is C++11 (Arduino-ESP32 2.0.17 / IDF 4.4);
  the native tests build as gnu++17.
- `bridge/codex-pet-bridge`: the Node.js bridge (Node 20+, no npm dependencies)
  with the dashboard (`ui/`), the Mac installer (`tools/mac/`) and the PetBar
  menu bar app (`macos/PetBar/`).

To install, repair or upgrade on a Mac, follow
[docs/AGENT_INSTALL.md](docs/AGENT_INSTALL.md) step by step.

## Rules

- Ask the user before flashing the board, running `install.sh --apply`,
  changing LaunchAgents or Hermes/OpenClaw/Claude/Codex configuration,
  installing PetBar, or pushing. Read-only checks need no permission.
- Never print or commit the Wi-Fi password, the bridge token or
  `firmware/agent_pet_display/include/secrets.h` (git-ignored). Keep
  `app_config.h` and `secrets.example.h` on placeholders.
- Runtime logs (`~/.codex-pet-bridge/state/events.jsonl`,
  `~/Library/Logs/codex-pet-bridge/`) are private: diagnose with them, never
  commit or paste them wholesale.
- The bridge must not listen on the LAN without its token.
- Flash only the port whose hardware ID contains `303A:1001`.
- A browser preview or host render is not the reflective panel: say so when
  you report on how something looks.

## Checks before you commit

| What | Command |
| --- | --- |
| Firmware unit tests | `cd firmware/agent_pet_display && pio test -e native` (or `tools/native_tests.sh` with `UNITY_DIR` / `ARDUINOJSON_DIR`) |
| Firmware build | `pio run -e waveshare_rlcd` |
| Bridge tests | `cd bridge/codex-pet-bridge && node --test` |
| Bridge ⇄ firmware protocol | `tools/contract-test.sh` (real bridge answer through the firmware's parser and codec) |
| Dashboard in a browser | `node bridge/codex-pet-bridge/tools/ui-e2e.mjs <video>` (needs Playwright) |
| Mac installer | `bridge/codex-pet-bridge/tools/mac/test-install.sh` (fake home directory) |
| Screens as images | `firmware/agent_pet_display/tools/host_render/build.sh` |

## Keep these in sync

- RLA1 animation format: `bridge/codex-pet-bridge/src/rla-codec.js` and
  `firmware/agent_pet_display/src/anim_codec.cpp`. Regenerate the shared test
  vector with `node bridge/codex-pet-bridge/tools/gen-rla-vectors.mjs
  firmware/agent_pet_display/test/native/test_anim_codec/anim_vectors.h`.
- Clock face and page names: `bridge/codex-pet-bridge/src/display-settings.js`,
  `firmware/agent_pet_display/src/clock_model.cpp` and `bridge_client.cpp`.
- The poll protocol (display, time zone, egg, telemetry):
  `bridge/codex-pet-bridge/src/bridge-server.js` and
  `firmware/agent_pet_display/src/bridge_client.cpp`.
- Dashboard face previews (`bridge/codex-pet-bridge/ui/previews/`): after
  changing a face, re-render with host_render and run
  `firmware/agent_pet_display/tools/host_render/export_ui_previews.py`.
