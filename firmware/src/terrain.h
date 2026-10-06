// Hill-shade grid computed by the phone from elevation tiles, used to shade the minimap land.
//
// MSG_TERRAIN layout (little-endian):
//   i32 lat0*1e7, i32 lon0*1e7   grid centre
//   u16 n                       grid is n x n cells
//   u16 cell_dm                 cell size in decimetres
//   n*n u8                      shade, row 0 = north edge; 128 = flat, <128 shadow, >128 lit slope
#pragma once
#include <Arduino.h>

namespace terrain {
bool load(const uint8_t *blob, size_t len, bool persist);
bool restore();
bool has();
// Shade at a point in the map frame (meters east/north of the map anchor); 128 if unknown.
uint8_t sample(float x, float y);
}  // namespace terrain
