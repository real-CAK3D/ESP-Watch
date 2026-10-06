// Local map geometry pushed by the phone (from OpenStreetMap via Overpass).
//
// MSG_MAP binary layout (little-endian):
//   'G','M', u8 version(=1), u8 reserved, i32 lat0*1e7, i32 lon0*1e7, u16 featureCount
//   feature: u8 kind, u8 reserved, u16 npts, npts * (i16 x, i16 y)
//   x = meters east of the anchor * 2, y = meters north * 2 (0.5 m units)
// Area features (water/park) are closed rings; the rest are polylines.
#pragma once
#include <Arduino.h>

enum MapKind : uint8_t {
  MK_MOTORWAY = 1,
  MK_PRIMARY = 2,
  MK_SECONDARY = 3,
  MK_STREET = 4,
  MK_SERVICE = 5,
  MK_PATH = 6,
  MK_RAIL = 7,
  MK_WATER = 20,
  MK_RIVER = 21,
  MK_PARK = 22,
  MK_BEACH = 23,
};

inline bool mapKindIsArea(uint8_t k) { return k == MK_WATER || k == MK_PARK || k == MK_BEACH; }

struct MapFeature {
  uint8_t kind;
  uint16_t npts;
  const int16_t *pts;  // 2*npts values
  int16_t minX, minY, maxX, maxY;
};

namespace mapdata {
bool load(const uint8_t *blob, size_t len, bool persist);  // parses + replaces the current map
bool loadRoute(const uint8_t *blob, size_t len);
void clearRoute();
bool restore();  // loads the last map from flash at boot

bool hasMap();
double anchorLat();
double anchorLon();
int featureCount();
const MapFeature &feature(int i);

// Route in the same frame as the map (meters, float)
int routePoints();
const float *routeXY();  // 2*n floats

// lat/lon -> meters relative to the map anchor
void project(double lat, double lon, float &x, float &y);
}  // namespace mapdata
