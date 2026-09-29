# Agent Pet Display Firmware

PlatformIO Arduino firmware for the Waveshare ESP32-S3-RLCD-4.2.

## Before flashing

1. Copy `include/secrets.example.h` to `include/secrets.h` (git ignores it) and set the Wi-Fi credentials, or run `tools/import-legacy-secrets.sh <old include/app_config.h>` to reuse an older checkout's values
2. Set `kBridgeUrl` to the Mac's LAN IP and bridge endpoint, with the bridge token
3. If a charge-status GPIO is identified on hardware, set `kChargeSensePin`; otherwise keep it at `-1` and validate voltage and percent first

## Commands

```bash
cd firmware/agent_pet_display
pio test -e native
pio run -e waveshare_rlcd
pio run -e waveshare_rlcd -t upload
pio device monitor -b 115200
```

Without PlatformIO, `tools/native_tests.sh` builds and runs the native tests with
the system compiler, and `tools/host_render/build.sh` renders every screen to
images with the real drawing code (see the top-level README).

## What runs where

- The main loop reads buttons and sensors, drives the pet state machine and redraws only when a frame changes.
- A FreeRTOS task on core 0 (`net_worker`) owns Wi-Fi, the bridge poll and easter-egg streaming, so a slow network never freezes the screen or the seconds.
- Time: the RTC holds UTC; the POSIX time zone comes from the bridge and is stored in NVS with the clock settings. A board that still has China-time RTC data from firmware before 1.3.0 is migrated once.
- Easter egg: RLA1 frames are fetched in 16 KB chunks into three buffers and shown on a schedule shared with the bridge; if the stream stalls or the bridge has no animation, the built-in animation plays.

## Notes

- RLCD pins: `SCK 11`, `MOSI 12`, `DC 5`, `CS 40`, `RST 41`
- ADC example uses `ADC_CHANNEL_3` and multiplies calibrated millivolts by `3`
- Charge-state GPIO is not exposed by the sample code and must be identified on hardware before enabling charging detection
- RTC uses the official `SensorLib` Arduino path on `SCL 14` / `SDA 13`
- SHTC3 sampling follows the official Waveshare command sequence and conversion formulas on the same `Wire` bus
- `SensorLib` is vendored in `lib/SensorLib` so `SensorPCF85063` builds without extra checkouts
