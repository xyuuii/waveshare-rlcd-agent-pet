# Agent Pet Display Firmware

PlatformIO Arduino firmware for the Waveshare ESP32-S3-RLCD-4.2.

## Before flashing

1. Set Wi-Fi credentials in `include/app_config.h`
2. Set `kBridgeUrl` to the Mac's LAN IP and bridge endpoint
3. If a charge-status GPIO is identified on hardware, set `kChargeSensePin`; otherwise keep it at `-1` and validate voltage and percent first

## Commands

```bash
cd firmware/agent_pet_display
pio test -e native
pio run -e waveshare_rlcd
pio run -e waveshare_rlcd -t upload
pio device monitor -b 115200
```

## Notes

- RLCD pins: `SCK 11`, `MOSI 12`, `DC 5`, `CS 40`, `RST 41`
- ADC example uses `ADC_CHANNEL_3` and multiplies calibrated millivolts by `3`
- Charge-state GPIO is not exposed by the sample code and must be identified on hardware before enabling charging detection
- RTC uses the official `SensorLib` Arduino path on `SCL 14` / `SDA 13`
- SHTC3 sampling follows the official Waveshare command sequence and conversion formulas on the same `Wire` bus
- `lib_extra_dirs` points at the cloned official `SensorLib` so `SensorPCF85063` builds without forking the whole upstream library
