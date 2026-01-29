# RTK Rover - Camas Base Station Client

## Overview

ESP-IDF based RTK rover that receives corrections from the Camas NTRIP caster for cm-level positioning.

## Hardware

- **MCU**: SparkFun ESP32 WROOM (Qwiic)
- **GNSS**: u-blox ZED-X20P
- **Connection**: I2C via Qwiic connector (address 0x42)
- **Antenna**: Multi-band GNSS antenna with clear sky view
- **Power**: USB-C (can be powered from iPhone)

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
                    +---> Serial output
                    |
                    +---> HTTP POST to Dashboard
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
    ├── config.h            # WiFi/NTRIP/I2C settings (gitignored)
    ├── main.c              # Application entry
    ├── wifi.c/h            # Multi-WiFi connection manager
    ├── ntrip_client.c/h    # NTRIP client (receive RTCM)
    ├── zed_rover.c/h       # ZED-X20P driver
    └── dashboard_client.c/h # HTTP POST to dashboard
```

## Multi-WiFi Support

The rover scans for available networks and connects to the strongest known network. Configured networks are defined in `wifi.c`:

```c
static const wifi_network_t wifi_networks[] = {
    { "RudyTheCanadian", "BIG22slick" },  // iPhone hotspot (portable)
    { "Glasshouse2.4", "BIG22slick" },    // Home network
};
```

**Features:**
- Scans all available networks on startup
- Connects to strongest known network by RSSI
- Auto-reconnects on signal loss
- Falls back to other known networks if primary fails
- Background task monitors connection health

**Adding Networks:** Edit the `wifi_networks[]` array in `src/wifi.c`.

## Configuration

Edit `src/config.h` (not tracked in git for security):
- WiFi settings (retries, scan interval, RSSI threshold)
- NTRIP settings (host, port, mountpoint, credentials)
- I2C address (ZED_I2C_ADDR = 0x42 default)
- Dashboard settings (host, port, reporting interval)

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
I (1234) wifi_multi: Initializing multi-network WiFi manager
I (1234) wifi_multi: Configured networks:
I (1234) wifi_multi:   1. RudyTheCanadian
I (1234) wifi_multi:   2. Glasshouse2.4
I (2000) wifi_multi: Scanning for WiFi networks...
I (3500) wifi_multi: Found 5 networks:
I (3500) wifi_multi:   [KNOWN]: RudyTheCanadian (RSSI: -45 dBm)
I (3500) wifi_multi:          : Neighbor_Network (RSSI: -72 dBm)
I (3500) wifi_multi: Best network: RudyTheCanadian (RSSI: -45 dBm)
I (4000) wifi_multi: ===========================================
I (4000) wifi_multi: Connected to: RudyTheCanadian
I (4000) wifi_multi: IP Address:   172.20.10.5
I (4000) wifi_multi: ===========================================

[18:05:32 UTC] RTK FIXED
  Lat: 45.647194700  Lon: -122.349807123
  Alt: 134.606 m MSL
  hAcc: 0.014 m  vAcc: 0.019 m  Sats: 24
  RTCM: 45230 bytes rx, 45230 bytes tx
  *** RTK FIXED - cm-level accuracy ***
```

## Dashboard Integration

The rover sends position updates to the ROVER_DASHBOARD every second:
- **Endpoint**: `POST http://srv1190594.hstgr.cloud:3000/api/position`
- **Data**: lat, lon, alt, accuracy, fix status, satellites, battery

## Portable Survey Setup

For field use with iPhone:
1. Connect ESP32 to iPhone via USB-C (power)
2. Enable iPhone Personal Hotspot
3. Rover auto-connects to `RudyTheCanadian` hotspot
4. Open `http://srv1190594.hstgr.cloud:3000/portable/` on iPhone
5. Mark survey points with cm-level accuracy

## Related Projects

- **ROVER_DASHBOARD**: Web dashboard for monitoring - `/Users/rudy/Projects/ROVER_DASHBOARD/`
- **PORTABLE_DASHBOARD**: Mobile survey app - `/Users/rudy/Projects/PORTABLE_DASHBOARD/`
- **RTK_BASE_CAMAS**: Base station firmware - `/Users/rudy/Projects/RTK_BASE_CAMAS/`
- **NTRIP Caster**: `srv1190594.hstgr.cloud:2101`

## Troubleshooting

**Watchdog timeout on wifi_mgr task:**
- Fixed in commit cc344fe - task now sleeps properly when connected

**No RTK FIXED:**
- Check base station is running and sending RTCM
- Verify antenna has clear sky view
- Wait 30-60 seconds for convergence

**WiFi won't connect:**
- Check SSID/password in wifi.c
- Verify network is in range (RSSI > -80 dBm)
- Check serial output for scan results
