// GTA V style minimap page: heading-up radar drawn with the software rasterizer into a
// full-screen canvas, with LVGL widgets layered on top for text (street, turn card).
#include <lvgl.h>

#include "hal.h"
#include "mapdata.h"
#include "phone.h"
#include "raster.h"
#include "state.h"
#include "terrain.h"
#include "ui.h"
#include "ui_common.h"

namespace {

// ---- GTA V radar palette (sampled from the in-game minimap) ----
constexpr uint16_t C_LAND = rgb565(0x4F5B56);
constexpr uint16_t C_PARK = rgb565(0x5A7362);
constexpr uint16_t C_BEACH = rgb565(0x8A8A70);
constexpr uint16_t C_WATER = rgb565(0x8DB4C8);
constexpr uint16_t C_ROAD = rgb565(0xB4C0BA);
constexpr uint16_t C_ROAD_MAJOR = rgb565(0xC9D3CE);
constexpr uint16_t C_PATH = rgb565(0x7F8B86);
constexpr uint16_t C_RAIL = rgb565(0x66716C);
constexpr uint16_t C_ROUTE = rgb565(0xA64CF2);
constexpr uint16_t C_ROUTE_EDGE = rgb565(0x5A1E8C);
constexpr uint16_t C_BLACK = 0x0000;
constexpr uint16_t C_WHITE = 0xFFFF;
constexpr uint16_t C_HEALTH = rgb565(0x5FB65A);
constexpr uint16_t C_HEALTH_DIM = rgb565(0x24421F);
constexpr uint16_t C_ARMOR = rgb565(0x5DADE2);
constexpr uint16_t C_ARMOR_DIM = rgb565(0x1D3A4E);
constexpr uint16_t C_GRID = rgb565(0x46514D);

constexpr int W = hal::W, H = hal::H;
// Frame geometry: rounded rectangle that follows the panel's rounded corners.
constexpr int FRAME_INSET = 3, FRAME_RADIUS = 62, RING = 13, RING_GAP = 3;
constexpr int MAP_X0 = FRAME_INSET + RING, MAP_Y0 = FRAME_INSET + RING;
constexpr int MAP_X1 = W - FRAME_INSET - RING, MAP_Y1 = H - FRAME_INSET - RING;
constexpr float PLAYER_X = W * 0.5f, PLAYER_Y = H * 0.60f;

uint16_t *fb = nullptr;
Raster rs;
lv_obj_t *canvas, *northBadge, *streetPill, *streetLbl, *areaLbl, *timeLbl;
lv_obj_t *navCard, *navArrowCanvas, *navDistLbl, *navStreetLbl, *navMetaLbl;
uint16_t *arrowBuf;
Raster arrowRs;
constexpr int ARROW_SZ = 84;
lv_obj_t *waitLbl;

constexpr int ICON_POOL = 14;
lv_obj_t *icons[ICON_POOL];
lv_obj_t *iconLbls[ICON_POOL];

// ring lookup: per row, outer and inner spans; per ring pixel, its param (0=separator)
int16_t outerL[H], outerR[H], innerL[H], innerR[H];
uint8_t *ringT = nullptr;
uint8_t *ringSide = nullptr;

// smoothed display pose (meters in map frame)
float dispX = 0, dispY = 0, dispHdg = 0, dispScale = 1.3f;
bool poseInit = false;
uint32_t lastFrameMs = 0;
uint32_t lastMapSeq = 0;
uint8_t lastManeuver = 255;

float spanHalf(float y, float top, float bottom, float left, float right, float r) {
  // horizontal half-extent of a rounded rect at row y (returns -1 if outside)
  if (y < top || y >= bottom) return -1;
  float cxL = left + r, cxR = right - r;
  float dy = 0;
  if (y < top + r) dy = top + r - y;
  else if (y > bottom - r) dy = y - (bottom - r);
  float inset = 0;
  if (dy > 0) {
    if (dy > r) return -1;
    inset = r - sqrtf(r * r - dy * dy);
  }
  (void)cxL;
  (void)cxR;
  return inset;
}

void buildRing() {
  const float oL = FRAME_INSET, oR = W - FRAME_INSET, oT = FRAME_INSET, oB = H - FRAME_INSET;
  const float iL = MAP_X0, iR = MAP_X1, iT = MAP_Y0, iB = MAP_Y1;
  const float iRad = FRAME_RADIUS - RING;
  int count = 0;
  for (int y = 0; y < H; y++) {
    float yc = y + 0.5f;
    float o = spanHalf(yc, oT, oB, oL, oR, FRAME_RADIUS);
    if (o < 0) {
      outerL[y] = outerR[y] = 0;
    } else {
      outerL[y] = (int16_t)lroundf(oL + o);
      outerR[y] = (int16_t)lroundf(oR - o);
    }
    float in = spanHalf(yc, iT, iB, iL, iR, iRad);
    if (in < 0) {
      innerL[y] = innerR[y] = 0;
    } else {
      innerL[y] = (int16_t)lroundf(iL + in);
      innerR[y] = (int16_t)lroundf(iR - in);
    }
    for (int x = outerL[y]; x < outerR[y]; x++)
      if (!(x >= innerL[y] && x < innerR[y])) count++;
  }
  ringT = (uint8_t *)heap_caps_malloc(count, MALLOC_CAP_SPIRAM);
  ringSide = (uint8_t *)heap_caps_malloc(count, MALLOC_CAP_SPIRAM);
  const float cx = W * 0.5f, cy = H * 0.5f;
  int k = 0;
  for (int y = 0; y < H; y++) {
    for (int x = outerL[y]; x < outerR[y]; x++) {
      if (x >= innerL[y] && x < innerR[y]) continue;
      float dx = x + 0.5f - cx, dy = y + 0.5f - cy;
      // param along each half: 0 at bottom center, 1 at top center (normalised for the aspect)
      float a = atan2f(fabsf(dx) / (W * 0.5f), dy / (H * 0.5f));  // 0 = straight down, pi = straight up
      float t = a / (float)M_PI;
      // separator: pixels hugging the map edge
      bool sep = false;
      for (int d = 1; d <= RING_GAP && !sep; d++) {
        int yy = y, xx = x;
        // check if a pixel `d` toward the centre is inside the map area
        float len = sqrtf(dx * dx + dy * dy);
        xx = (int)(x - dx / len * d);
        yy = (int)(y - dy / len * d);
        if (yy >= 0 && yy < H && xx >= innerL[yy] && xx < innerR[yy]) sep = true;
      }
      ringT[k] = sep ? 0 : (uint8_t)constrain((int)(t * 254) + 1, 1, 255);
      ringSide[k] = dx < 0 ? 0 : 1;
      k++;
    }
  }
}

// world meters -> screen
inline void toScreen(float wx, float wy, float cs, float sn, float &sx, float &sy) {
  float dx = wx - dispX, dy = wy - dispY;
  sx = PLAYER_X + (dx * cs - dy * sn) * dispScale;
  sy = PLAYER_Y - (dx * sn + dy * cs) * dispScale;
}

float roadWidth(uint8_t k) {
  // widths in meters, scaled with zoom, with a minimum pixel width so small streets stay visible
  float m;
  switch (k) {
    case MK_MOTORWAY: m = 15; break;
    case MK_PRIMARY: m = 12; break;
    case MK_SECONDARY: m = 10; break;
    case MK_STREET: m = 7.5f; break;
    case MK_SERVICE: m = 4; break;
    case MK_PATH: m = 2; break;
    case MK_RAIL: m = 2; break;
    case MK_RIVER: m = 10; break;
    default: m = 5;
  }
  float px = m * dispScale;
  float minPx = (k == MK_PATH || k == MK_RAIL) ? 1.5f : (k == MK_SERVICE ? 2.0f : 4.0f);
  return max(px, minPx);
}

float *scratch = nullptr;
int scratchCap = 0;
float *ensureScratch(int n) {
  if (n * 2 > scratchCap) {
    free(scratch);
    scratchCap = n * 2 + 256;
    scratch = (float *)heap_caps_malloc(sizeof(float) * scratchCap, MALLOC_CAP_SPIRAM);
  }
  return scratch;
}

void drawFeatures(float cs, float sn) {
  const float viewR = hypotf(W, H) / dispScale;  // meters; generous
  const int nf = mapdata::featureCount();
  // pass 0: areas, pass 1: minor lines, pass 2: major roads
  for (int pass = 0; pass < 3; pass++) {
    for (int i = 0; i < nf; i++) {
      const MapFeature &f = mapdata::feature(i);
      bool area = mapKindIsArea(f.kind);
      bool major = f.kind == MK_MOTORWAY || f.kind == MK_PRIMARY || f.kind == MK_SECONDARY;
      int want = area ? 0 : (major ? 2 : 1);
      if (want != pass) continue;
      // bbox cull (feature coords are 0.5 m units)
      if (f.maxX * 0.5f < dispX - viewR || f.minX * 0.5f > dispX + viewR || f.maxY * 0.5f < dispY - viewR ||
          f.minY * 0.5f > dispY + viewR)
        continue;
      float *xy = ensureScratch(f.npts);
      if (!xy) return;
      for (int p = 0; p < f.npts; p++) toScreen(f.pts[p * 2] * 0.5f, f.pts[p * 2 + 1] * 0.5f, cs, sn, xy[p * 2], xy[p * 2 + 1]);
      if (area) {
        uint16_t c = f.kind == MK_WATER ? C_WATER : (f.kind == MK_PARK ? C_PARK : C_BEACH);
        rs.fillPoly(xy, f.npts, c);
      } else {
        uint16_t c = major ? C_ROAD_MAJOR : C_ROAD;
        if (f.kind == MK_PATH) c = C_PATH;
        if (f.kind == MK_RAIL) c = C_RAIL;
        if (f.kind == MK_RIVER) c = C_WATER;
        rs.polyline(xy, f.npts, roadWidth(f.kind), c);
      }
    }
  }
}

// Land with hill shading (GTA's radar keeps it subtle). Drawn in 4x4 blocks: ~12k samples a frame.
uint16_t landShades[32];
bool landShadesReady = false;

void drawLand(float cs, float sn) {
  if (!app.settings.terrain || !terrain::has()) {
    rs.fill(C_LAND);
    return;
  }
  if (!landShadesReady) {
    const int r0 = 0x4F, g0 = 0x5B, b0 = 0x56;  // C_LAND
    for (int i = 0; i < 32; i++) {
      float f = 0.70f + 0.60f * i / 31.0f;  // 0.70 (deep shadow) .. 1.30 (sunlit slope)
      int r = min(255, (int)(r0 * f)), g = min(255, (int)(g0 * f)), b = min(255, (int)(b0 * f));
      landShades[i] = rgb565((r << 16) | (g << 8) | b);
    }
    landShadesReady = true;
  }
  constexpr int B = 4;
  const float inv = 1.0f / dispScale;
  for (int by = MAP_Y0; by < MAP_Y1; by += B) {
    const float b = (PLAYER_Y - (by + B * 0.5f)) * inv;
    for (int bx = MAP_X0; bx < MAP_X1; bx += B) {
      const float a = (bx + B * 0.5f - PLAYER_X) * inv;
      // inverse of toScreen(): screen offset -> world offset
      const float wx = dispX + a * cs + b * sn;
      const float wy = dispY - a * sn + b * cs;
      rs.fillRect(bx, by, B, B, landShades[terrain::sample(wx, wy) >> 3]);
    }
  }
}

void drawRoute(float cs, float sn) {
  int n = mapdata::routePoints();
  if (n < 2) return;
  const float *r = mapdata::routeXY();
  float *xy = ensureScratch(n);
  if (!xy) return;
  for (int i = 0; i < n; i++) toScreen(r[i * 2], r[i * 2 + 1], cs, sn, xy[i * 2], xy[i * 2 + 1]);
  float w = max(6.0f, 7.0f * dispScale);
  rs.polyline(xy, n, w + 3, C_ROUTE_EDGE);
  rs.polyline(xy, n, w, C_ROUTE);
}

void drawGrid(float cs, float sn) {
  // subtle block grid when no map is loaded yet, so the radar never looks empty
  const float step = 80;
  float gx0 = floorf((dispX - 400) / step) * step, gy0 = floorf((dispY - 400) / step) * step;
  for (float g = 0; g <= 800; g += step) {
    float ax, ay, bx, by;
    toScreen(gx0 + g, gy0, cs, sn, ax, ay);
    toScreen(gx0 + g, gy0 + 800, cs, sn, bx, by);
    rs.thickLine(ax, ay, bx, by, 5, C_GRID, false);
    toScreen(gx0, gy0 + g, cs, sn, ax, ay);
    toScreen(gx0 + 800, gy0 + g, cs, sn, bx, by);
    rs.thickLine(ax, ay, bx, by, 5, C_GRID, false);
  }
}

void drawBlip() {
  // GTA player blip: white arrow with a dark outline, always pointing up (heading-up map)
  const float s = 1.0f;
  float outline[] = {PLAYER_X, PLAYER_Y - 21 * s, PLAYER_X + 15 * s, PLAYER_Y + 15 * s, PLAYER_X, PLAYER_Y + 7 * s,
                     PLAYER_X - 15 * s, PLAYER_Y + 15 * s};
  float body[] = {PLAYER_X, PLAYER_Y - 16 * s, PLAYER_X + 11 * s, PLAYER_Y + 10.5f * s, PLAYER_X, PLAYER_Y + 4 * s,
                  PLAYER_X - 11 * s, PLAYER_Y + 10.5f * s};
  rs.fillPoly(outline, 4, rgb565(0x1A1A1A));
  rs.fillPoly(body, 4, C_WHITE);
}

void drawFrame() {
  const int wb = hal::batteryPercent() < 0 ? 100 : hal::batteryPercent();
  const int pb = app.phoneBattery < 0 ? 0 : app.phoneBattery;
  const uint8_t wT = (uint8_t)(1 + wb * 254 / 100), pT = (uint8_t)(1 + pb * 254 / 100);
  int k = 0;
  for (int y = 0; y < H; y++) {
    uint16_t *row = fb + y * W;
    int oL = outerL[y], oR = outerR[y];
    if (oR <= oL) {
      memset(row, 0, W * 2);
      continue;
    }
    for (int x = 0; x < oL; x++) row[x] = C_BLACK;
    for (int x = oR; x < W; x++) row[x] = C_BLACK;
    for (int x = oL; x < oR; x++) {
      if (x >= innerL[y] && x < innerR[y]) {
        x = innerR[y] - 1;
        continue;
      }
      uint8_t t = ringT[k];
      bool left = ringSide[k] == 0;
      k++;
      if (t == 0) {
        row[x] = C_BLACK;
      } else if (left) {
        row[x] = t <= wT ? C_HEALTH : C_HEALTH_DIM;
      } else {
        row[x] = t <= pT ? C_ARMOR : C_ARMOR_DIM;
      }
    }
  }
}

// Maneuver arrow for the nav card, drawn with the rasterizer.
void drawArrow(uint8_t man) {
  arrowRs.fill(rgb565(0x111111));
  const float c = ARROW_SZ / 2.0f;
  const uint16_t col = C_WHITE;
  auto head = [&](float x, float y, float ang) {  // ang: direction the arrow points, radians (0 = up)
    float s = sinf(ang), co = cosf(ang);
    auto P = [&](float px, float py, float *o) {
      o[0] = x + px * co - py * s;
      o[1] = y + px * s + py * co;
    };
    float tri[6];
    P(0, -16, tri);
    P(15, 6, tri + 2);
    P(-15, 6, tri + 4);
    arrowRs.fillPoly(tri, 3, col);
  };
  const float lw = 9;
  if (man == MAN_ARRIVE) {
    arrowRs.fillCircle(c, c - 8, 18, rgb565(0xA64CF2));
    arrowRs.fillCircle(c, c - 8, 7, col);
    float pin[] = {c - 15, c + 1, c + 15, c + 1, c, c + 28};
    arrowRs.fillPoly(pin, 3, rgb565(0xA64CF2));
    return;
  }
  if (man == MAN_UTURN) {
    arrowRs.thickLine(c + 12, c + 30, c + 12, c - 6, lw, col);
    for (int i = 0; i <= 12; i++) {
      float a0 = M_PI * i / 12, a1 = M_PI * (i + 1) / 12;
      arrowRs.thickLine(c + 12 * cosf(a0), c - 6 - 12 * sinf(a0), c + 12 * cosf(a1), c - 6 - 12 * sinf(a1), lw, col);
    }
    arrowRs.thickLine(c - 12, c - 6, c - 12, c + 8, lw, col);
    head(c - 12, c + 18, (float)M_PI);
    return;
  }
  if (man == MAN_ROUNDABOUT) {
    for (int i = 0; i < 24; i++) {
      float a0 = 2 * M_PI * i / 24, a1 = 2 * M_PI * (i + 1) / 24;
      arrowRs.thickLine(c + 13 * cosf(a0), c + 4 + 13 * sinf(a0), c + 13 * cosf(a1), c + 4 + 13 * sinf(a1), 6, col);
    }
    arrowRs.thickLine(c, c + 17, c, c + 34, lw, col);
    arrowRs.thickLine(c + 9, c - 5, c + 22, c - 18, lw, col);
    head(c + 25, c - 21, (float)M_PI / 4);
    return;
  }
  float turn = 0;  // degrees, + = right
  switch (man) {
    case MAN_SLIGHT_LEFT: case MAN_FORK_LEFT: turn = -40; break;
    case MAN_LEFT: turn = -90; break;
    case MAN_SHARP_LEFT: turn = -135; break;
    case MAN_SLIGHT_RIGHT: case MAN_FORK_RIGHT: case MAN_MERGE: turn = 40; break;
    case MAN_RIGHT: turn = 90; break;
    case MAN_SHARP_RIGHT: turn = 135; break;
    default: turn = 0;
  }
  if (turn == 0) {
    arrowRs.thickLine(c, c + 32, c, c - 14, lw, col);
    head(c, c - 20, 0);
    return;
  }
  const float a = turn * (float)M_PI / 180;
  const float jx = c - sinf(a) * 8, jy = c + 6;
  arrowRs.thickLine(jx, c + 32, jx, jy, lw, col);
  const float len = 22;
  float ex = jx + sinf(a) * len, ey = jy - cosf(a) * len;
  arrowRs.thickLine(jx, jy, ex, ey, lw, col);
  head(ex + sinf(a) * 6, ey - cosf(a) * 6, a);
}

lv_obj_t *makePill(lv_obj_t *parent) {
  lv_obj_t *p = lv_obj_create(parent);
  lv_obj_remove_style_all(p);
  lv_obj_set_style_bg_color(p, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(p, LV_OPA_70, 0);
  lv_obj_set_style_radius(p, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_pad_hor(p, 16, 0);
  lv_obj_set_style_pad_ver(p, 6, 0);
  lv_obj_set_size(p, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_remove_flag(p, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_remove_flag(p, LV_OBJ_FLAG_SCROLLABLE);
  return p;
}

void onMapTap(lv_event_t *) {
  phone::sendEvent("open_map");
  ui::toast(app.connected ? "Opening big map on phone" : "Phone not connected", "\xEF\x89\xB9");
}

void updateNavCard() {
  const NavState &n = app.nav;
  if (!n.active) {
    lv_obj_add_flag(navCard, LV_OBJ_FLAG_HIDDEN);
    return;
  }
  lv_obj_remove_flag(navCard, LV_OBJ_FLAG_HIDDEN);
  if (n.maneuver != lastManeuver) {
    lastManeuver = n.maneuver;
    drawArrow(n.maneuver);
    lv_obj_invalidate(navArrowCanvas);
  }
  char d[24];
  if (n.maneuver == MAN_ARRIVE && n.distNext < 25) strcpy(d, "Arrived");
  else state::formatDistance(n.distNext, d, sizeof(d));
  lv_label_set_text(navDistLbl, d);
  lv_label_set_text(navStreetLbl, n.street[0] ? n.street : n.instruction);
  char meta[64], rem[16];
  state::formatDistance(n.distRemain, rem, sizeof(rem));
  int mins = (n.etaSec + 59) / 60;
  char clk[12];
  ui::formatClock(clk, sizeof(clk), false);  // the card hides the map's clock pill, so show time here
  if (n.cost[0]) snprintf(meta, sizeof(meta), "%d min \xE2\x80\xA2 %s \xE2\x80\xA2 %s \xE2\x80\xA2 %s", mins, rem, n.cost, clk);
  else snprintf(meta, sizeof(meta), "%d min  \xE2\x80\xA2  %s  \xE2\x80\xA2  %s", mins, rem, clk);
  lv_label_set_text(navMetaLbl, meta);
}

void placeIcon(int &used, float wx, float wy, float cs, float sn, const char *kind, bool clampToEdge) {
  if (used >= ICON_POOL) return;
  float sx, sy;
  toScreen(wx, wy, cs, sn, sx, sy);
  // standing on a saved place: the player arrow wins, like GTA hides a blip you're on top of
  if (hypotf(sx - PLAYER_X, sy - PLAYER_Y) < 30) return;
  const float m = 26;
  // keep blips out from under the turn card while navigating
  const float top = app.nav.active ? MAP_Y0 + 150 : MAP_Y0 + m;
  bool inside = sx > MAP_X0 + m && sx < MAP_X1 - m && sy > top && sy < MAP_Y1 - m;
  if (!inside) {
    if (!clampToEdge) return;
    // GTA-style: pin off-screen blips to the radar edge along the direction from the player
    float dx = sx - PLAYER_X, dy = sy - PLAYER_Y;
    float tx = dx > 0 ? (MAP_X1 - m - PLAYER_X) / dx : (MAP_X0 + m - PLAYER_X) / dx;
    float ty = dy > 0 ? (MAP_Y1 - m - PLAYER_Y) / dy : (top - PLAYER_Y) / dy;
    float t = min(fabsf(tx), fabsf(ty));
    sx = PLAYER_X + dx * t;
    sy = PLAYER_Y + dy * t;
  }
  lv_obj_t *o = icons[used];
  lv_obj_set_style_border_color(o, lv_color_hex(state::placeColor(kind)), 0);
  lv_label_set_text(iconLbls[used], state::placeIcon(kind));
  lv_obj_set_style_text_color(iconLbls[used], lv_color_hex(state::placeColor(kind)), 0);
  lv_obj_set_pos(o, (int)sx - 20, (int)sy - 20);
  lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN);
  used++;
}

void updateOverlays(float cs, float sn) {
  // north marker rides the frame like the GTA radar's "N"
  float nx = -sn, ny = -cs;  // screen direction of world north
  float cx = W * 0.5f, cy = H * 0.5f;
  float hx = (W * 0.5f - FRAME_INSET - RING * 0.5f), hy = (H * 0.5f - FRAME_INSET - RING * 0.5f);
  float t = min(fabsf(nx) > 1e-4f ? hx / fabsf(nx) : 1e9f, fabsf(ny) > 1e-4f ? hy / fabsf(ny) : 1e9f);
  float bx = cx + nx * t, by = cy + ny * t;
  // pull corners in so the badge follows the rounded frame
  float cornerX = hx - FRAME_RADIUS * 0.45f, cornerY = hy - FRAME_RADIUS * 0.45f;
  bx = constrain(bx, cx - cornerX, cx + cornerX);
  by = constrain(by, cy - cornerY, cy + cornerY);
  lv_obj_set_pos(northBadge, (int)bx - 17, (int)by - 17);

  int used = 0;
  if (mapdata::hasMap()) {
    for (int i = 0; i < app.placeCount && used < ICON_POOL - 1; i++) {
      float wx, wy;
      mapdata::project(app.places[i].lat, app.places[i].lon, wx, wy);
      bool important = !strcmp(app.places[i].kind, "safehouse") || !strcmp(app.places[i].kind, "parking");
      placeIcon(used, wx, wy, cs, sn, app.places[i].kind, important && hypotf(wx - dispX, wy - dispY) < 3000);
    }
    int rn = mapdata::routePoints();
    if (app.nav.active && rn > 1) {
      const float *r = mapdata::routeXY();
      placeIcon(used, r[(rn - 1) * 2], r[(rn - 1) * 2 + 1], cs, sn, "dest", true);
    }
  }
  for (int i = used; i < ICON_POOL; i++) lv_obj_add_flag(icons[i], LV_OBJ_FLAG_HIDDEN);

  const GpsState &g = app.gps;
  if (g.street[0]) {
    lv_label_set_text(streetLbl, g.street);
    lv_label_set_text(areaLbl, g.area);
    lv_obj_remove_flag(streetPill, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_add_flag(streetPill, LV_OBJ_FLAG_HIDDEN);
  }

  if (!g.valid) {
    lv_label_set_text(waitLbl, app.connected ? "Waiting for phone GPS\xE2\x80\xA6" : "Connect the GTA-Watch app");
    lv_obj_remove_flag(waitLbl, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_add_flag(waitLbl, LV_OBJ_FLAG_HIDDEN);
  }

  char tb[16];
  ui::formatClock(tb, sizeof(tb), false);
  lv_label_set_text(timeLbl, tb);
  if (app.nav.active) lv_obj_add_flag(timeLbl, LV_OBJ_FLAG_HIDDEN);  // the turn card shows the clock instead
  else lv_obj_remove_flag(timeLbl, LV_OBJ_FLAG_HIDDEN);
  updateNavCard();
}

}  // namespace

namespace ui_map {

void create(lv_obj_t *tile) {
  fb = (uint16_t *)heap_caps_malloc(W * H * 2, MALLOC_CAP_SPIRAM);
  rs.init(fb, W, H);
  buildRing();

  canvas = lv_canvas_create(tile);
  lv_canvas_set_buffer(canvas, fb, W, H, LV_COLOR_FORMAT_RGB565);
  lv_obj_set_pos(canvas, 0, 0);
  lv_obj_add_flag(canvas, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(canvas, onMapTap, LV_EVENT_SHORT_CLICKED, nullptr);
  lv_obj_add_event_cb(canvas, onMapTap, LV_EVENT_LONG_PRESSED, nullptr);

  for (int i = 0; i < ICON_POOL; i++) {
    lv_obj_t *o = lv_obj_create(tile);
    lv_obj_remove_style_all(o);
    lv_obj_set_size(o, 40, 40);
    lv_obj_set_style_radius(o, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(o, lv_color_hex(0x0B0B0B), 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(o, 2, 0);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
    iconLbls[i] = lv_label_create(o);
    lv_obj_set_style_text_font(iconLbls[i], &font_sm, 0);
    lv_obj_center(iconLbls[i]);
    icons[i] = o;
  }

  northBadge = lv_obj_create(tile);
  lv_obj_remove_style_all(northBadge);
  lv_obj_set_size(northBadge, 34, 34);
  lv_obj_set_style_radius(northBadge, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(northBadge, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(northBadge, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(northBadge, lv_color_white(), 0);
  lv_obj_set_style_border_width(northBadge, 2, 0);
  lv_obj_remove_flag(northBadge, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_t *n = lv_label_create(northBadge);
  lv_obj_set_style_text_font(n, &font_md, 0);
  lv_obj_set_style_text_color(n, lv_color_white(), 0);
  lv_label_set_text(n, "N");
  lv_obj_center(n);

  timeLbl = lv_label_create(tile);
  lv_obj_set_style_text_font(timeLbl, &font_md, 0);
  lv_obj_set_style_text_color(timeLbl, lv_color_white(), 0);
  lv_obj_set_style_bg_color(timeLbl, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(timeLbl, LV_OPA_60, 0);
  lv_obj_set_style_radius(timeLbl, 14, 0);
  lv_obj_set_style_pad_hor(timeLbl, 12, 0);
  lv_obj_set_style_pad_ver(timeLbl, 2, 0);
  lv_obj_align(timeLbl, LV_ALIGN_TOP_MID, 0, MAP_Y0 + 6);

  streetPill = makePill(tile);
  lv_obj_set_flex_flow(streetPill, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(streetPill, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_radius(streetPill, 18, 0);
  streetLbl = lv_label_create(streetPill);
  lv_obj_set_style_text_font(streetLbl, &font_md, 0);
  lv_obj_set_style_text_color(streetLbl, lv_color_white(), 0);
  lv_label_set_long_mode(streetLbl, LV_LABEL_LONG_DOT);
  lv_obj_set_style_max_width(streetLbl, 320, 0);
  areaLbl = lv_label_create(streetPill);
  lv_obj_set_style_text_font(areaLbl, &font_sm, 0);
  lv_obj_set_style_text_color(areaLbl, lv_color_hex(0xF0C850), 0);
  lv_obj_align(streetPill, LV_ALIGN_BOTTOM_MID, 0, -(H - MAP_Y1) - 10);
  lv_obj_add_flag(streetPill, LV_OBJ_FLAG_HIDDEN);

  waitLbl = lv_label_create(tile);
  lv_obj_set_style_text_font(waitLbl, &font_sm, 0);
  lv_obj_set_style_text_color(waitLbl, lv_color_white(), 0);
  lv_obj_set_style_bg_color(waitLbl, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(waitLbl, LV_OPA_70, 0);
  lv_obj_set_style_radius(waitLbl, 12, 0);
  lv_obj_set_style_pad_all(waitLbl, 10, 0);
  lv_obj_align(waitLbl, LV_ALIGN_CENTER, 0, 60);

  // turn-by-turn card
  navCard = lv_obj_create(tile);
  lv_obj_remove_style_all(navCard);
  lv_obj_set_size(navCard, MAP_X1 - MAP_X0 - 24, 118);
  lv_obj_align(navCard, LV_ALIGN_TOP_MID, 0, MAP_Y0 + 8);
  lv_obj_set_style_bg_color(navCard, lv_color_hex(0x111111), 0);
  lv_obj_set_style_bg_opa(navCard, LV_OPA_90, 0);
  lv_obj_set_style_radius(navCard, 26, 0);
  lv_obj_set_style_border_color(navCard, lv_color_hex(0xA64CF2), 0);
  lv_obj_set_style_border_width(navCard, 2, 0);
  lv_obj_remove_flag(navCard, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(navCard, LV_OBJ_FLAG_CLICKABLE);
  arrowBuf = (uint16_t *)heap_caps_malloc(ARROW_SZ * ARROW_SZ * 2, MALLOC_CAP_SPIRAM);
  arrowRs.init(arrowBuf, ARROW_SZ, ARROW_SZ);
  navArrowCanvas = lv_canvas_create(navCard);
  lv_canvas_set_buffer(navArrowCanvas, arrowBuf, ARROW_SZ, ARROW_SZ, LV_COLOR_FORMAT_RGB565);
  lv_obj_align(navArrowCanvas, LV_ALIGN_LEFT_MID, 12, 0);
  navDistLbl = lv_label_create(navCard);
  lv_obj_set_style_text_font(navDistLbl, &font_xl, 0);
  lv_obj_set_style_text_color(navDistLbl, lv_color_white(), 0);
  lv_obj_set_pos(navDistLbl, ARROW_SZ + 24, 6);
  navStreetLbl = lv_label_create(navCard);
  lv_obj_set_style_text_font(navStreetLbl, &font_md, 0);
  lv_obj_set_style_text_color(navStreetLbl, lv_color_hex(0xDDDDDD), 0);
  lv_label_set_long_mode(navStreetLbl, LV_LABEL_LONG_DOT);
  lv_obj_set_width(navStreetLbl, MAP_X1 - MAP_X0 - 24 - ARROW_SZ - 36);
  lv_obj_set_pos(navStreetLbl, ARROW_SZ + 24, 54);
  navMetaLbl = lv_label_create(navCard);
  lv_obj_set_style_text_font(navMetaLbl, &font_sm, 0);
  lv_obj_set_style_text_color(navMetaLbl, lv_color_hex(0xB98AF0), 0);
  lv_obj_set_pos(navMetaLbl, ARROW_SZ + 24, 84);
  lv_obj_add_flag(navCard, LV_OBJ_FLAG_HIDDEN);

  lastFrameMs = millis();
}

// Called every UI loop pass while the map page is visible and the screen is on.
void tick() {
  uint32_t now = millis();
  const GpsState &g = app.gps;
  bool moving = false;

  float dt = (now - lastFrameMs) / 1000.0f;
  // ~15 fps while things move, 2 fps otherwise (clock, battery)
  bool animating = false;
  if (g.valid && mapdata::hasMap()) {
    float tx, ty;
    mapdata::project(g.lat, g.lon, tx, ty);
    // dead-reckon between 1 Hz fixes so the blip glides instead of jumping
    float ahead = min((now - g.rxMs) / 1000.0f, 1.5f);
    float hr = g.heading * (float)DEG_TO_RAD;
    if (g.speed > 0.4f) {
      tx += sinf(hr) * g.speed * ahead;
      ty += cosf(hr) * g.speed * ahead;
    }
    if (!poseInit || app.mapSeq != lastMapSeq || hypotf(tx - dispX, ty - dispY) > 300) {
      dispX = tx;
      dispY = ty;
      dispHdg = g.heading;
      poseInit = true;
    }
    float k = min(1.0f, dt * 5.0f);
    dispX += (tx - dispX) * k;
    dispY += (ty - dispY) * k;
    float dh = fmodf(g.heading - dispHdg + 540.0f, 360.0f) - 180.0f;
    dispHdg = fmodf(dispHdg + dh * min(1.0f, dt * 4.0f) + 360.0f, 360.0f);
    // speed-adaptive zoom like GTA: zoom out when driving fast
    static const float ZOOM[3] = {1.75f, 1.3f, 0.95f};
    const float base = ZOOM[min<int>(app.settings.radarZoom, 2)];
    float targetScale = g.speed < 3 ? base : (g.speed > 25 ? base * 0.46f : base * (1.0f - (g.speed - 3) / 22.0f * 0.54f));
    dispScale += (targetScale - dispScale) * min(1.0f, dt * 1.5f);
    moving = hypotf(tx - dispX, ty - dispY) > 0.05f || fabsf(dh) > 0.3f || g.speed > 0.4f;
    animating = moving;
  }
  uint32_t interval = animating ? 66 : 500;
  if (now - lastFrameMs < interval && app.mapSeq == lastMapSeq) return;
  lastFrameMs = now;
  lastMapSeq = app.mapSeq;

  const float hr = dispHdg * (float)DEG_TO_RAD;
  const float cs = cosf(hr), sn = sinf(hr);
  rs.clip(MAP_X0, MAP_Y0, MAP_X1, MAP_Y1);
  drawLand(cs, sn);
  if (mapdata::hasMap()) {
    drawFeatures(cs, sn);
    drawRoute(cs, sn);
  } else {
    drawGrid(cs, sn);
  }
  drawBlip();
  rs.clip(0, 0, W, H);
  drawFrame();
  updateOverlays(cs, sn);
  lv_obj_invalidate(canvas);
}

}  // namespace ui_map
