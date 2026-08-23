# Waveshare ESP32-S3 Watch GTA-Style GPS Map

## Target Device

- Host detected: NukeBox / `NucBox_M7`
- Watch USB serial: `COM5`
- USB identity: `USB\VID_303A&PID_1001`
- Expected board family: Waveshare ESP32-S3 Touch AMOLED 2.06, 410 x 502 rectangular AMOLED, touch, IMU, TF card

## Product Goal

Build a rectangular GTA V-inspired live GPS minimap for the watch. The map should fill the watch display instead of being circular. It should feel like a game HUD, but show real-world location from the phone and offline map data from the watch SD card.

## Core Interaction

- Default state: close-view map centered on current location.
- First tap: zoom out to show a larger surrounding area.
- Second tap: return to the close-view map.
- Repeat with each tap.

The tap target can be the whole map surface at first. Later, dedicated controls can be added if needed.

## Visual Direction

- Rectangular minimap using the full 410 x 502 display.
- GTA V-inspired styling:
  - muted street colors
  - dark/transparent HUD frame
  - bright route line
  - player arrow fixed near center
  - compact status strip for speed, heading, GPS/BLE state, and zoom level
- No circular mask.
- No step-driven fake map motion for the final version.

## Data Flow

1. Phone gets GPS from Android location services.
2. Phone sends `lat`, `lon`, `speed`, `heading`, and accuracy to the watch over BLE.
3. Watch reads offline OSM map data or prepared map tiles from the TF card.
4. Watch renders the local map at the selected zoom level.
5. Touch toggles between close and zoomed-out scale.

## Firmware Milestones

1. Display proof:
   - Draw rectangular HUD map shell on the watch.
   - Show fake location, fake roads, and zoom state.
   - Touch toggles close/overview mode.

2. Phone GPS proof:
   - Receive fake GPS over serial first for easy testing from NukeBox.
   - Add BLE GPS characteristic or Chronos/Navio bridge.
   - Show live coordinates/status on screen.

3. Offline map proof:
   - Load a tiny prepared local map area from SD.
   - Render roads/paths/buildings with simplified styling.
   - Keep player marker centered.

4. GTA V pass:
   - Add route line, heading rotation, labels/status strip, and polished colors.
   - Tune close and overview zoom levels for the 410 x 502 screen.

## Open Decisions

- Whether to extend Chronos/Navio or use a separate companion BLE location bridge.
- Whether map storage should start as raster tiles or simplified vector OSM.
- Which real-world test area should be generated for the first SD-card map pack.
