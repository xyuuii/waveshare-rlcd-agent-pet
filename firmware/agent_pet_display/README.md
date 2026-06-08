# Agent Pet Display Firmware

PlatformIO Arduino firmware for the Waveshare `ESP32-S3-RLCD-4.2`.

## Configure

Edit `include/app_config.h` before flashing:

1. set `kWifiSsid`
2. set `kWifiPassword`
3. set `kBridgeUrl`
4. optionally set `kChargeSensePin` if you know the board GPIO for charge detection

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
- battery voltage is derived from calibrated ADC millivolts and multiplied by `3`
- RTC uses `SensorLib` on `SCL 14` / `SDA 13`
- SHTC3 sampling follows the Waveshare command sequence on the same `Wire` bus
- `ST7305_U8g2` is included locally under `lib/`
- `SensorLib` is vendored under `lib/SensorLib`
