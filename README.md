# GTA-Watch

GTA V–style minimap + turn-by-turn navigation for the **Waveshare ESP32-S3-Touch-AMOLED-2.06**
watch, with an Android companion app. Rebuilt from scratch (2026-09-30) to replace the earlier
Codex "GTA-Nav" project. The old code is kept in git history under the `v1.0.0` tag and the
[v1.0.0 release](https://github.com/real-CAK3D/ESP-Watch/releases/tag/v1.0.0) (legacy firmware + APK, for rollback).

Prebuilt watch firmware and the Android APK are on the
[Releases page](https://github.com/real-CAK3D/ESP-Watch/releases/latest).

```
firmware/   ESP32-S3 watch firmware (PlatformIO, Arduino 3.x, LVGL 9.3, NimBLE)
android/    Companion app (Kotlin, Jetpack Compose, MapLibre)
tools/      PC tools: flash.ps1, watchctl.py (screenshots/touch), phonesim.py (phone simulator)
backups/    backup.py: segmented full-flash backup of the watch over USB
docs/       Protocol spec
```

## Using the watch

| Where | Gesture | Result |
|---|---|---|
| Watch face | swipe left | **GTA minimap** (always the close-up radar) |
| Minimap | tap or hold | opens the bigger map in the phone app |
| Minimap | swipe left | Places (Safe House, Mechanic, Parked Car… tap one to route there; Park / Find phone / Stop buttons) |
| Places | swipe left | Activity (steps, distance, calories, batteries) |
| Watch face | swipe right | Weather |
| Watch face | swipe down | Quick settings (brightness, map-stays-on, 24h, units) |
| Watch face | swipe up | Notifications |
| Watch face | long-press | switch digital ⇄ analog face |
| BOOT button | press | home (face) — on the face it jumps to the map |
| PWR button | press | screen on/off |

The minimap is heading-up like GTA: the white arrow is you, roads/water/parks are real
OpenStreetMap data streamed from the phone, hills are shaded from real elevation data, the purple
line is your GPS route, the green ring is the watch battery (health) and the blue ring the phone
battery (armor). The "N" badge orbits the frame to show north. While navigating, the turn card
shows the next maneuver, distance, ETA, remaining distance, gas cost and the time; the watch wakes
itself up before each turn.

**Charging & battery:** plugging in shows a charging screen with the percentage and time to full.
Below 20% / 10% / 5% the watch warns you (and the phone, if enabled); below 15% power saver dims the
screen and shortens the timeout. Raise your wrist to wake the screen (toggle in quick settings).

**Updates:** new versions are published on the Releases page. The phone app checks it, updates
itself, and sends new watch firmware over Bluetooth (about a minute; the watch shows progress and
restarts). USB flashing still works as before.

## Phone app

Install `GTA-Watch.apk` from the latest release (debug-signed, for sideloading; Android 8.0+, arm64). Tabs:
- **Map** – GTA V pause-map style. *Radar* mode follows you heading-up (the "bigger minimap");
  drag the map for the free pause map. Search an address or long-press anywhere to drop a
  waypoint, then Drive / Walk.
- **Places** – add GTA blips (Safe House, Mechanic, Pay 'n' Spray, Cluckin' Bell…) from your
  location or an address. They appear on the watch and can be routed to from the wrist.
- **Watch** – pair through Android's companion pairing sheet (the same system Galaxy Wearable
  uses, so the link survives in the background), link status + signal bars, battery with a 24 h
  charge chart, telemetry, updates, link log, and a crash report if the app ever crashed.
- **Settings** – map style (GTA *Road* pause map or GTA *Atlas*), terrain on phone / watch, watch
  radar zoom, watch face, raise to wake, spoken turn directions, fuel economy + gas price, charge and
  low-battery alerts, permissions, updates.

The **Map** tab also has a GTA-style legend (list button on the right) of the blips on your map.

For a link that survives the screen being off: allow notification access, "Display over other
apps", and set the app's battery mode to *Unrestricted*.

Data sources (free, no API keys): OpenStreetMap via Overpass (watch map), OpenFreeMap tiles
(phone map), AWS Terrain Tiles (hill shading), OSRM (routes), Nominatim (search), Open-Meteo (weather).

## Publishing a release

```powershell
cd G:\GTA-Watch\tools
.\release.ps1 -Version 2.2.0 -Notes ..\docs\release-notes\v2.2.0.md
```
Bumps the firmware and app versions, builds both, packages the watch images + APK + checksums,
commits, tags, pushes and creates the GitHub release that the app's updater looks for.

## Building

Firmware needs PlatformIO Core 6.2.0 or newer (the pioarduino platform refuses older cores).
The display/sensor/PMU libraries are vendored in `firmware/lib`; LVGL, NimBLE and ArduinoJson
are fetched by PlatformIO.

```powershell
# watch firmware: build, then flash over USB (adjust upload_port in platformio.ini)
cd firmware
pio run
pio run -t upload

# android app (JDK 17+, Android SDK 35)
cd android
.\gradlew.bat assembleRelease   # -> app/build/outputs/apk/release/app-release.apk
```

On the author's machine everything lives on G: (`tools\flash.ps1` builds and flashes using
`PLATFORMIO_CORE_DIR=G:\GTA-Watch\.pio` and `G:\GTA-Watch\tools\pio-venv`;
set `JAVA_HOME=G:\Android\jdk` and `GRADLE_USER_HOME=G:\Android\gradle` for the app).

## Debugging the watch from the PC

```powershell
cd G:\GTA-Watch\tools
.\pio-venv\Scripts\python.exe watchctl.py shot shots\now.png   # screenshot of the watch
.\pio-venv\Scripts\python.exe watchctl.py info                 # battery, BLE, map, memory
.\pio-venv\Scripts\python.exe watchctl.py swipe 380 250 30 250 # inject a swipe
.\pio-venv\Scripts\python.exe phonesim.py demo                 # weather, places, a notification
.\pio-venv\Scripts\python.exe phonesim.py map 44.0966 -70.2148 # real OSM map at a point
.\pio-venv\Scripts\python.exe phonesim.py walk LAT LON DLAT DLON 5   # simulated route, screenshot every 5 s
```

`phonesim.py` is the reference implementation of what the app does (map packing, routing,
turn-by-turn), so the watch can be tested end-to-end without the phone.

## Hardware notes

ESP32-S3R8 (8 MB octal PSRAM, 32 MB flash), CO5300 410×502 AMOLED on QSPI, FT3168 touch,
AXP2101 PMU, PCF85063 RTC, QMI8658 IMU. LVGL's heap is in PSRAM on purpose — with it in
internal RAM only ~1.6 KB was left free, which starves Bluetooth.
