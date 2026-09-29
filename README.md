# waveshare-rlcd-agent-pet

Agent status pet for the Waveshare `ESP32-S3-RLCD-4.2`.

This project turns the 4.2" monochrome reflective RLCD board into a low-power desktop companion that shows:

- current agent source and state (Codex, Claude Code, Hermes, OpenClaw)
- task summary
- time with seconds, and a full-screen clock page with seven faces
  (big sans, seven-segment, dot matrix, analog dial, word clock, terminal, pet)
- battery percentage and voltage
- onboard temperature and humidity
- token, context, 5-hour, and weekly usage cards
- a compact RLCD-adapted GUGUGAGA pixel pet with state bubbles
- an easter egg: 1-bit videos (for example your own *Bad Apple!!*) streamed
  from the Mac and played full screen, or a built-in animation

On the Mac side the bridge adds a web dashboard (`/ui/`) and a menu bar app
(`PetBar`) that show the same agent status plus the board's telemetry, switch
pages and clock faces, and convert and play easter-egg videos.

## Repo layout

- `firmware/agent_pet_display`
  - PlatformIO Arduino firmware for the Waveshare board
- `bridge/codex-pet-bridge`
  - bundled bridge layer used to normalize Codex, Claude Code, and Hermes style events for the display

## Quick start

### 1. Start the bridge

```bash
cd bridge/codex-pet-bridge
node ./src/bridge-server.js
```

If you want to expose the bridge to your LAN for the ESP32, set a host and token as described in `bridge/codex-pet-bridge/docs/SECURITY.md`.

On a Mac, `bridge/codex-pet-bridge/tools/mac/install.sh` sets it up as LaunchAgents
(dry run first, `--apply` to do it), and `bridge/codex-pet-bridge/macos/PetBar/build.sh --install`
builds the menu bar app. The dashboard is at `http://127.0.0.1:17366/ui/`.

### 2. Configure the firmware

Wi-Fi and bridge settings live in `firmware/agent_pet_display/include/secrets.h`,
which git ignores. Copy `include/secrets.example.h` to `include/secrets.h` and set:

- `kWifiSsid`
- `kWifiPassword`
- `kBridgeUrl`

Example:

```cpp
static constexpr const char* kBridgeUrl =
    "http://192.168.1.23:17366/esp32/poll?token=<your-token>";
```

If an older checkout still has these values inside `include/app_config.h`,
`tools/import-legacy-secrets.sh <old app_config.h>` copies them over without
printing them.

### 3. Build and flash

```bash
cd firmware/agent_pet_display
pio test -e native
pio run -e waveshare_rlcd
pio run -e waveshare_rlcd -t upload
pio device monitor -b 115200
```

## Buttons

| Button | Press | Overview / usage page | Clock page |
| --- | --- | --- | --- |
| BOOT | short | next page (overview → usage → clock) | next page |
| BOOT | 1.5 s | follow the next agent | next clock face |
| KEY | short or 1.5 s | follow the next agent | next clock face |
| BOOT + KEY | hold both 2 s | easter egg | easter egg |
| BOOT | hold 5 s | easter egg | easter egg |
| any | while the egg plays | stop it | stop it |

The clock face, 12/24 h and seconds are also set from the dashboard or PetBar and
survive a reboot. The board keeps UTC in its RTC and gets its time zone (with
daylight saving rules) from the Mac.

## Previews without hardware

`firmware/agent_pet_display/tools/host_render/build.sh` compiles the real drawing
code and U8g2 for the host and writes every screen as a 400×300 image. They show
the exact pixels, not how the reflective panel looks in a room.

## Hardware notes

- board: Waveshare `ESP32-S3-RLCD-4.2`
- display: `300x400` ST7305 monochrome RLCD
- RTC: `PCF85063`
- climate sensor: `SHTC3`
- power: ADC-based battery reading, optional charge GPIO

## Upstream and attribution

This repo includes or derives from several MIT-licensed upstream components:

- `bridge/codex-pet-bridge` is based on the `codex-pet-bridge` project
- `firmware/agent_pet_display/lib/SensorLib` is from Lewis He
- the RLCD board support and wiring were adapted from Waveshare examples
- the GUGUGAGA pet artwork was converted to a monochrome RLCD bitmap from
  [`drlrf/cc-guga`](https://github.com/drlrf/cc-guga), which publishes the
  `pets/gugugaga` package under the MIT License

See the preserved license files inside those directories for details.
