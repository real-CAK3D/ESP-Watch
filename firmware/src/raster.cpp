#include "raster.h"

#include <math.h>
#include <string.h>
#include <stdlib.h>
#include <algorithm>

void Raster::fill(uint16_t c) {
  const uint32_t c2 = ((uint32_t)c << 16) | c;
  for (int y = cy0; y < cy1; y++) {
    uint16_t *row = px + y * w;
    int x = cx0;
    if ((x & 1) && x < cx1) row[x++] = c;
    uint32_t *r32 = (uint32_t *)(row + x);
    int pairs = (cx1 - x) / 2;
    for (int i = 0; i < pairs; i++) r32[i] = c2;
    x += pairs * 2;
    if (x < cx1) row[x] = c;
  }
}

void Raster::fillRect(int x, int y, int rw, int rh, uint16_t c) {
  int x0 = std::max(x, cx0), x1 = std::min(x + rw, cx1);
  int y0 = std::max(y, cy0), y1 = std::min(y + rh, cy1);
  for (int yy = y0; yy < y1; yy++) {
    uint16_t *row = px + yy * w;
    for (int xx = x0; xx < x1; xx++) row[xx] = c;
  }
}

static inline void hspan(uint16_t *row, int x0, int x1, uint16_t c) {
  if (x1 - x0 > 8) {
    if (x0 & 1) row[x0++] = c;
    const uint32_t c2 = ((uint32_t)c << 16) | c;
    uint32_t *r32 = (uint32_t *)(row + x0);
    int pairs = (x1 - x0) >> 1;
    for (int i = 0; i < pairs; i++) r32[i] = c2;
    x0 += pairs << 1;
  }
  for (; x0 < x1; x0++) row[x0] = c;
}

struct Edge {
  float yTop, yBot, x, dxdy;
};

void Raster::fillPoly(const float *xy, int n, uint16_t c) {
  if (n < 3) return;
  static Edge *edges = nullptr;
  static int edgeCap = 0;
  static float *xs = nullptr;
  static int xsCap = 0;
  if (edgeCap < n) {
    free(edges);
    edgeCap = n + 64;
    edges = (Edge *)malloc(sizeof(Edge) * edgeCap);
  }
  if (xsCap < n) {
    free(xs);
    xsCap = n + 64;
    xs = (float *)malloc(sizeof(float) * xsCap);
  }
  if (!edges || !xs) return;

  float minY = 1e9f, maxY = -1e9f, minX = 1e9f, maxX = -1e9f;
  int ne = 0;
  for (int i = 0; i < n; i++) {
    float x0 = xy[i * 2], y0 = xy[i * 2 + 1];
    int j = (i + 1) % n;
    float x1 = xy[j * 2], y1 = xy[j * 2 + 1];
    minX = std::min(minX, x0);
    maxX = std::max(maxX, x0);
    if (y0 == y1) continue;
    if (y0 > y1) {
      std::swap(x0, x1);
      std::swap(y0, y1);
    }
    Edge &e = edges[ne++];
    e.yTop = y0;
    e.yBot = y1;
    e.dxdy = (x1 - x0) / (y1 - y0);
    e.x = x0;
    minY = std::min(minY, y0);
    maxY = std::max(maxY, y1);
  }
  if (ne < 2 || maxX < cx0 || minX >= cx1) return;
  int yStart = std::max(cy0, (int)ceilf(minY - 0.5f));
  int yEnd = std::min(cy1 - 1, (int)floorf(maxY - 0.5f));
  if (yStart > yEnd) return;

  std::sort(edges, edges + ne, [](const Edge &a, const Edge &b) { return a.yTop < b.yTop; });
  int next = 0;
  // active edges are tracked by index list inside `xs` computation (simple, n is small)
  static int *active = nullptr;
  static int activeCap = 0;
  if (activeCap < ne) {
    free(active);
    activeCap = ne + 64;
    active = (int *)malloc(sizeof(int) * activeCap);
  }
  if (!active) return;
  int na = 0;
  for (int y = yStart; y <= yEnd; y++) {
    float sy = y + 0.5f;
    while (next < ne && edges[next].yTop <= sy) active[na++] = next++;
    int nx = 0;
    for (int k = 0; k < na;) {
      Edge &e = edges[active[k]];
      if (e.yBot <= sy) {
        active[k] = active[--na];
        continue;
      }
      if (e.yTop <= sy) xs[nx++] = e.x + (sy - e.yTop) * e.dxdy;
      k++;
    }
    if (nx < 2) continue;
    std::sort(xs, xs + nx);
    uint16_t *row = px + y * w;
    for (int k = 0; k + 1 < nx; k += 2) {
      int xa = std::max(cx0, (int)ceilf(xs[k] - 0.5f));
      int xb = std::min(cx1, (int)ceilf(xs[k + 1] - 0.5f));
      if (xa < xb) hspan(row, xa, xb, c);
    }
  }
}

void Raster::fillCircle(float x, float y, float r, uint16_t c) {
  if (r <= 0.3f) return;
  int y0 = std::max(cy0, (int)floorf(y - r)), y1 = std::min(cy1 - 1, (int)ceilf(y + r));
  float r2 = r * r;
  for (int yy = y0; yy <= y1; yy++) {
    float dy = yy + 0.5f - y;
    float d = r2 - dy * dy;
    if (d < 0) continue;
    float hw = sqrtf(d);
    int xa = std::max(cx0, (int)ceilf(x - hw - 0.5f));
    int xb = std::min(cx1, (int)ceilf(x + hw - 0.5f));
    if (xa < xb) hspan(px + yy * w, xa, xb, c);
  }
}

void Raster::thickLine(float x0, float y0, float x1, float y1, float width, uint16_t c, bool roundCaps) {
  float dx = x1 - x0, dy = y1 - y0;
  float len = sqrtf(dx * dx + dy * dy);
  float hw = width * 0.5f;
  // cheap reject against clip rect
  if (std::max(x0, x1) + hw < cx0 || std::min(x0, x1) - hw >= cx1 || std::max(y0, y1) + hw < cy0 ||
      std::min(y0, y1) - hw >= cy1)
    return;
  if (len < 0.01f) {
    fillCircle(x0, y0, hw, c);
    return;
  }
  float nx = -dy / len * hw, ny = dx / len * hw;
  float q[8] = {x0 + nx, y0 + ny, x1 + nx, y1 + ny, x1 - nx, y1 - ny, x0 - nx, y0 - ny};
  fillPoly(q, 4, c);
  if (roundCaps && width >= 2.5f) {
    fillCircle(x0, y0, hw, c);
    fillCircle(x1, y1, hw, c);
  }
}

void Raster::polyline(const float *xy, int n, float width, uint16_t c) {
  for (int i = 0; i + 1 < n; i++) thickLine(xy[i * 2], xy[i * 2 + 1], xy[i * 2 + 2], xy[i * 2 + 3], width, c, false);
  if (width >= 2.5f)
    for (int i = 0; i < n; i++) fillCircle(xy[i * 2], xy[i * 2 + 1], width * 0.5f, c);
}
