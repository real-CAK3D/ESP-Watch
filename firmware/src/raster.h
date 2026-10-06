// Minimal RGB565 rasterizer used to draw the minimap (polygons, thick lines, discs).
#pragma once
#include <stdint.h>

constexpr uint16_t rgb565(uint32_t hex) {
  return (uint16_t)((((hex >> 16) & 0xFF) >> 3) << 11 | (((hex >> 8) & 0xFF) >> 2) << 5 | ((hex & 0xFF) >> 3));
}

struct Raster {
  uint16_t *px;
  int w, h;
  int cx0, cy0, cx1, cy1;  // clip rect, inclusive-exclusive

  void init(uint16_t *buf, int width, int height) {
    px = buf;
    w = width;
    h = height;
    clip(0, 0, width, height);
  }
  void clip(int x0, int y0, int x1, int y1) {
    cx0 = x0 < 0 ? 0 : x0;
    cy0 = y0 < 0 ? 0 : y0;
    cx1 = x1 > w ? w : x1;
    cy1 = y1 > h ? h : y1;
  }
  void fill(uint16_t c);
  void fillRect(int x, int y, int rw, int rh, uint16_t c);
  // Even-odd scanline fill; xy = x0,y0,x1,y1,... (n points). Works for concave shapes.
  void fillPoly(const float *xy, int n, uint16_t c);
  void fillCircle(float x, float y, float r, uint16_t c);
  void thickLine(float x0, float y0, float x1, float y1, float width, uint16_t c, bool roundCaps = true);
  // Polyline with round joins. xy has n points.
  void polyline(const float *xy, int n, float width, uint16_t c);
};
