#include "mapdata.h"

#include <LittleFS.h>

#include "state.h"

static uint8_t *blobBuf = nullptr;  // owns the raw map bytes (PSRAM)
static size_t blobLen = 0;
static MapFeature *features = nullptr;
static int nFeatures = 0;
static double lat0 = 0, lon0 = 0;
static bool haveMap = false;

static double *routeLL = nullptr;  // lat,lon pairs
static float *routeProj = nullptr;
static int nRoute = 0;

static constexpr double M_PER_DEG_LAT = 110574.0;
static inline double mPerDegLon(double lat) { return 111320.0 * cos(lat * DEG_TO_RAD); }

static inline int16_t rd16(const uint8_t *p) { return (int16_t)(p[0] | (p[1] << 8)); }
static inline uint16_t ru16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static inline int32_t rd32(const uint8_t *p) { return (int32_t)(p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24)); }

namespace mapdata {

void project(double lat, double lon, float &x, float &y) {
  x = (float)((lon - lon0) * mPerDegLon(lat0));
  y = (float)((lat - lat0) * M_PER_DEG_LAT);
}

static void reprojectRoute() {
  if (!routeProj) return;
  for (int i = 0; i < nRoute; i++) project(routeLL[i * 2], routeLL[i * 2 + 1], routeProj[i * 2], routeProj[i * 2 + 1]);
}

bool load(const uint8_t *blob, size_t len, bool persist) {
  if (len < 14 || blob[0] != 'G' || blob[1] != 'M' || blob[2] != 1) return false;
  uint16_t count = ru16(blob + 12);

  // validate before replacing the current map
  size_t off = 14;
  for (int i = 0; i < count; i++) {
    if (off + 4 > len) return false;
    uint16_t n = ru16(blob + off + 2);
    off += 4 + (size_t)n * 4;
    if (off > len) return false;
  }

  uint8_t *nb = (uint8_t *)heap_caps_malloc(len, MALLOC_CAP_SPIRAM);
  MapFeature *nf = (MapFeature *)heap_caps_malloc(sizeof(MapFeature) * max<int>(count, 1), MALLOC_CAP_SPIRAM);
  if (!nb || !nf) {
    free(nb);
    free(nf);
    return false;
  }
  memcpy(nb, blob, len);

  off = 14;
  for (int i = 0; i < count; i++) {
    MapFeature &f = nf[i];
    f.kind = nb[off];
    f.npts = ru16(nb + off + 2);
    f.pts = (const int16_t *)(nb + off + 4);  // 2-byte aligned: header is 14 bytes, features are 4 + 4n
    int16_t mnx = INT16_MAX, mny = INT16_MAX, mxx = INT16_MIN, mxy = INT16_MIN;
    for (int p = 0; p < f.npts; p++) {
      int16_t x = f.pts[p * 2], y = f.pts[p * 2 + 1];
      mnx = min(mnx, x);
      mxx = max(mxx, x);
      mny = min(mny, y);
      mxy = max(mxy, y);
    }
    f.minX = mnx;
    f.minY = mny;
    f.maxX = mxx;
    f.maxY = mxy;
    off += 4 + (size_t)f.npts * 4;
  }

  free(blobBuf);
  free(features);
  blobBuf = nb;
  blobLen = len;
  features = nf;
  nFeatures = count;
  lat0 = rd32(nb + 4) / 1e7;
  lon0 = rd32(nb + 8) / 1e7;
  haveMap = true;
  reprojectRoute();
  app.mapSeq++;

  if (persist) {
    File fh = LittleFS.open("/map.bin", "w");
    if (fh) {
      fh.write(blobBuf, blobLen);
      fh.close();
    }
  }
  return true;
}

bool restore() {
  File fh = LittleFS.open("/map.bin", "r");
  if (!fh) return false;
  size_t len = fh.size();
  uint8_t *tmp = (uint8_t *)heap_caps_malloc(len, MALLOC_CAP_SPIRAM);
  if (!tmp) return false;
  fh.read(tmp, len);
  fh.close();
  bool ok = load(tmp, len, false);
  free(tmp);
  return ok;
}

bool loadRoute(const uint8_t *blob, size_t len) {
  if (len < 10) return false;
  double rlat0 = rd32(blob) / 1e7, rlon0 = rd32(blob + 4) / 1e7;
  uint16_t n = ru16(blob + 8);
  if (len < 10 + (size_t)n * 4) return false;
  clearRoute();
  if (n == 0) return true;
  routeLL = (double *)heap_caps_malloc(sizeof(double) * 2 * n, MALLOC_CAP_SPIRAM);
  routeProj = (float *)heap_caps_malloc(sizeof(float) * 2 * n, MALLOC_CAP_SPIRAM);
  if (!routeLL || !routeProj) {
    clearRoute();
    return false;
  }
  const double mlon = mPerDegLon(rlat0);
  for (int i = 0; i < n; i++) {
    double x = rd16(blob + 10 + i * 4) * 0.5, y = rd16(blob + 12 + i * 4) * 0.5;
    routeLL[i * 2] = rlat0 + y / M_PER_DEG_LAT;
    routeLL[i * 2 + 1] = rlon0 + x / mlon;
  }
  nRoute = n;
  reprojectRoute();
  app.mapSeq++;
  return true;
}

void clearRoute() {
  free(routeLL);
  free(routeProj);
  routeLL = nullptr;
  routeProj = nullptr;
  if (nRoute) app.mapSeq++;
  nRoute = 0;
}

bool hasMap() { return haveMap; }
double anchorLat() { return lat0; }
double anchorLon() { return lon0; }
int featureCount() { return nFeatures; }
const MapFeature &feature(int i) { return features[i]; }
int routePoints() { return nRoute; }
const float *routeXY() { return routeProj; }

}  // namespace mapdata
