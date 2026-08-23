# GTA-Nav OSM-To-GTA Tile Pipeline

Goal: real-life OSM data with the visual feel of the GTA map art.

The watch should not style raw OSM in firmware. The clean pipeline is:

1. Download a bounded OSM extract for the target area.
2. Convert OSM features into a compact intermediate JSON.
3. Render the JSON with a GTA-style palette and layer order on a stronger device.
4. Export rectangular raster tiles to SD card.
5. Let the watch pan/crop raster tiles while the phone streams GPS.

Recommended renderer stack:

- MapLibre style JSON for web/mobile previews.
- OpenMapTiles-compatible layers for larger areas.
- Offline raster export for the ESP32 watch.

Current prototype:

- `gta-phone-dashboard/map-data/lewiston-style-map.json` is generated from `osm-data/lewiston-roads.osm`.
- `gta-phone-dashboard/app.js` renders that JSON onto a pause-map canvas with GTA-style colors.
- The watch currently pans a GTA-style bitmap source from phone GPS. The next firmware step is replacing that source image with SD-backed tiles exported from the same renderer.
