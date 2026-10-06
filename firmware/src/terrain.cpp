#include "terrain.h"

#include <LittleFS.h>

#include "mapdata.h"
#include "state.h"

static uint8_t *grid = nullptr;
static int n = 0;
static float cell = 25;
static double lat0 = 0, lon0 = 0;
// grid centre in the current map frame, refreshed when a new map arrives
static float cx = 0, cy = 0;
static uint32_t projectedForMap = 0xFFFFFFFF;

static inline int32_t rd32(const uint8_t *p) { return (int32_t)(p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24)); }
static inline uint16_t ru16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }

namespace terrain {

bool load(const uint8_t *blob, size_t len, bool persist) {
  if (len < 12) return false;
  int nn = ru16(blob + 8);
  if (nn < 2 || nn > 512 || len < 12 + (size_t)nn * nn) return false;
  uint8_t *g = (uint8_t *)heap_caps_malloc(nn * nn, MALLOC_CAP_SPIRAM);
  if (!g) return false;
  memcpy(g, blob + 12, nn * nn);
  free(grid);
  grid = g;
  n = nn;
  lat0 = rd32(blob) / 1e7;
  lon0 = rd32(blob + 4) / 1e7;
  cell = max(1, (int)ru16(blob + 10)) / 10.0f;
  projectedForMap = 0xFFFFFFFF;
  app.mapSeq++;
  if (persist) {
    File f = LittleFS.open("/terrain.bin", "w");
    if (f) {
      f.write(blob, 12 + (size_t)nn * nn);
      f.close();
    }
  }
  return true;
}

bool restore() {
  File f = LittleFS.open("/terrain.bin", "r");
  if (!f) return false;
  size_t len = f.size();
  uint8_t *tmp = (uint8_t *)heap_caps_malloc(len, MALLOC_CAP_SPIRAM);
  if (!tmp) return false;
  f.read(tmp, len);
  f.close();
  bool ok = load(tmp, len, false);
  free(tmp);
  return ok;
}

bool has() { return grid != nullptr && mapdata::hasMap(); }

uint8_t sample(float x, float y) {
  if (!grid) return 128;
  if (projectedForMap != app.mapSeq) {
    mapdata::project(lat0, lon0, cx, cy);
    projectedForMap = app.mapSeq;
  }
  // bilinear between cell centres, so slopes read as smooth relief instead of 25 m squares
  float fx = (x - cx) / cell + n * 0.5f - 0.5f;
  float fy = n * 0.5f - (y - cy) / cell - 0.5f;
  if (fx < 0 || fy < 0 || fx >= n - 1 || fy >= n - 1) return 128;
  int x0 = (int)fx, y0 = (int)fy;
  float tx = fx - x0, ty = fy - y0;
  const uint8_t *r0 = grid + y0 * n + x0, *r1 = r0 + n;
  float top = r0[0] + (r0[1] - r0[0]) * tx;
  float bot = r1[0] + (r1[1] - r1[0]) * tx;
  return (uint8_t)(top + (bot - top) * ty);
}

}  // namespace terrain
