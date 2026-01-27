# RTK Rover - Camas Base Station Client

## Overview

ESP-IDF based RTK rover that receives corrections from the Camas NTRIP caster for cm-level positioning.

## Hardware

- **MCU**: SparkFun ESP32 WROOM (Qwiic)
- **GNSS**: u-blox ZED-X20P
- **Connection**: I2C via Qwiic connector (address 0x42)
- **Antenna**: Multi-band GNSS antenna with clear sky view

## Data Flow

```
NTRIP Caster (srv1190594.hstgr.cloud:2101/CAMAS_TEST)
                    |
                    v (RTCM corrections)
              ESP32 WiFi
                    |
                    v (I2C write)
               ZED-X20P
                    |
                    v (RTK processing)
         RTK Fixed Position
                    |
                    v (I2C read NAV-PVT)
              ESP32 parses
                    |
                    v
           Serial output
```

## Project Structure

```
RTK_ROVER_CAMAS/
├── platformio.ini          # ESP-IDF config
├── partitions.csv          # Flash partitions
├── sdkconfig.defaults      # ESP-IDF defaults
├── CMakeLists.txt          # Top-level cmake
└── src/
    ├── CMakeLists.txt      # Component cmake
    ├── config.h            # WiFi/NTRIP/I2C settings
    ├── main.c              # Application entry
    ├── wifi.c/h            # WiFi connection
    ├── ntrip_client.c/h    # NTRIP client (receive RTCM)
    └── zed_rover.c/h       # ZED-X20P driver
```

## Configuration

Edit `src/config.h`:
- WiFi credentials (WIFI_SSID, WIFI_PASSWORD)
- NTRIP settings (already configured for Camas)
- I2C address (ZED_I2C_ADDR = 0x42 default)

## ZED-X20P Setup

Before using, flash the rover configuration:
```bash
cd /Users/rudy/Projects/ucenter-2/full_output
./configure_rtk_rover.py
```

This enables:
- All GNSS constellations (GPS, GLONASS, Galileo, BeiDou)
- RTCM input on I2C
- NAV-PVT output on I2C
- 5Hz navigation rate

## Build & Upload

```bash
cd /Users/rudy/Projects/RTK_ROVER_CAMAS
pio run -t upload
pio device monitor
```

## Expected Output

```
[18:05:32 UTC] RTK FIXED
  Lat: 45.647194700  Lon: -122.349807123
  Alt: 134.606 m MSL
  hAcc: 0.014 m  vAcc: 0.019 m  Sats: 24
  RTCM: 45230 bytes rx, 45230 bytes tx
  *** RTK FIXED - cm-level accuracy ***
```

## Related

- Base station: `/Users/rudy/Projects/RTK_BASE_CAMAS/`
- Configuration scripts: `/Users/rudy/Projects/ucenter-2/full_output/`
- NTRIP caster: `srv1190594.hstgr.cloud:2101`
