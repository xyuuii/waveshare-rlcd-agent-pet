# waveshare-rlcd-agent-pet

Agent status pet for the Waveshare `ESP32-S3-RLCD-4.2`.

This project turns the 4.2" monochrome reflective RLCD board into a low-power desktop companion that shows:

- current agent source and state
- task summary
- time with seconds
- battery percentage and voltage
- onboard temperature and humidity
- token, context, 5-hour, and weekly usage cards
- a compact RLCD-adapted GUGUGAGA pixel pet with state bubbles

## Repo layout

- `firmware/agent_pet_display`
  - PlatformIO Arduino firmware for the Waveshare board
- `bridge/codex-pet-bridge`
  - bundled bridge layer used to normalize Codex, Claude Code, and Hermes style events for the display

## Quick start

### 1. Start the bridge

```bash
cd bridge/codex-pet-bridge
npm install
node ./src/bridge-server.js
```

If you want to expose the bridge to your LAN for the ESP32, set a host and token as described in `bridge/codex-pet-bridge/docs/SECURITY.md`.

### 2. Configure the firmware

Edit:

`firmware/agent_pet_display/include/app_config.h`

Set:

- `kWifiSsid`
- `kWifiPassword`
- `kBridgeUrl`

Example:

```cpp
static constexpr const char* kBridgeUrl =
    "http://192.168.1.23:17366/esp32/poll?token=<your-token>";
```

### 3. Build and flash

```bash
cd firmware/agent_pet_display
pio test -e native
pio run -e waveshare_rlcd
pio run -e waveshare_rlcd -t upload
pio device monitor -b 115200
```

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
