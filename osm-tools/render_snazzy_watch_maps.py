#!/usr/bin/env python3
"""Render Snazzy GTA-style OSM map headers for the GTA-Nav watch."""

from __future__ import annotations

import argparse
import json
import math
from pathlib import Path


WIDTH = 410
HEIGHT = 342


def rgb565(hex_color: str) -> int:
    value = int(hex_color.lstrip("#"), 16)
    r = (value >> 16) & 0xFF
    g = (value >> 8) & 0xFF
    b = value & 0xFF
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)


def style_color(style: list[dict], feature_type: str, element_type: str, fallback: str) -> str:
    for rule in style:
      if rule.get("featureType") != feature_type or rule.get("elementType") != element_type:
          continue
      for styler in rule.get("stylers", []):
          if "color" in styler:
              return styler["color"]
    return fallback


def mercator(center: dict, lat: float, lon: float) -> tuple[float, float]:
    meters_per_lon = 111320.0 * math.cos(math.radians(center["lat"]))
    return (lon - center["lon"]) * meters_per_lon, (center["lat"] - lat) * 111320.0


def project(center: dict, lat: float, lon: float, scale: float) -> tuple[int, int]:
    x, y = mercator(center, lat, lon)
    return round(WIDTH / 2 + x * scale), round(HEIGHT / 2 + y * scale)


def set_px(buf: list[int], x: int, y: int, color: int) -> None:
    if 0 <= x < WIDTH and 0 <= y < HEIGHT:
        buf[y * WIDTH + x] = color


def fill_rect(buf: list[int], x: int, y: int, w: int, h: int, color: int) -> None:
    x0 = max(0, x)
    y0 = max(0, y)
    x1 = min(WIDTH, x + w)
    y1 = min(HEIGHT, y + h)
    for yy in range(y0, y1):
        row = yy * WIDTH
        for xx in range(x0, x1):
            buf[row + xx] = color


def fill_circle(buf: list[int], cx: int, cy: int, radius: int, color: int) -> None:
    rr = radius * radius
    for y in range(cy - radius, cy + radius + 1):
        for x in range(cx - radius, cx + radius + 1):
            if (x - cx) * (x - cx) + (y - cy) * (y - cy) <= rr:
                set_px(buf, x, y, color)


def draw_line(buf: list[int], x0: int, y0: int, x1: int, y1: int, color: int, width: int) -> None:
    dx = abs(x1 - x0)
    dy = -abs(y1 - y0)
    sx = 1 if x0 < x1 else -1
    sy = 1 if y0 < y1 else -1
    err = dx + dy
    radius = max(1, width // 2)
    while True:
        fill_circle(buf, x0, y0, radius, color)
        if x0 == x1 and y0 == y1:
            break
        e2 = 2 * err
        if e2 >= dy:
            err += dy
            x0 += sx
        if e2 <= dx:
            err += dx
            y0 += sy


def fill_polygon(buf: list[int], points: list[tuple[int, int]], color: int) -> None:
    if len(points) < 3:
        return
    min_y = max(0, min(y for _, y in points))
    max_y = min(HEIGHT - 1, max(y for _, y in points))
    for y in range(min_y, max_y + 1):
        nodes: list[int] = []
        j = len(points) - 1
        for i, (xi, yi) in enumerate(points):
            xj, yj = points[j]
            if (yi < y and yj >= y) or (yj < y and yi >= y):
                if yj != yi:
                    nodes.append(round(xi + (y - yi) / (yj - yi) * (xj - xi)))
            j = i
        nodes.sort()
        for a, b in zip(nodes[0::2], nodes[1::2]):
            for x in range(max(0, a), min(WIDTH, b + 1)):
                buf[y * WIDTH + x] = color


def road_color(weight: int, colors: dict[str, int]) -> int:
    if weight >= 5:
        return colors["highway"]
    if weight >= 3:
        return colors["arterial"]
    return colors["local"]


def road_width(weight: int, overview: bool) -> int:
    if overview:
        if weight >= 5:
            return 6
        if weight >= 3:
            return 4
        return 3
    if weight >= 5:
        return 14
    if weight >= 3:
        return 9
    return 6


def render(map_data: dict, style: list[dict], scale: float, overview: bool) -> list[int]:
    colors = {
        "background": rgb565(style_color(style, "landscape.natural", "geometry", "#000000")),
        "man_made": rgb565(style_color(style, "landscape.man_made", "geometry", "#1d1d1d")),
        "park": rgb565(style_color(style, "poi.park", "geometry.fill", "#788c40")),
        "highway": rgb565(style_color(style, "road.highway", "geometry", "#bebebe")),
        "arterial": rgb565(style_color(style, "road.arterial", "geometry", "#aeaeae")),
        "local": rgb565(style_color(style, "road.local", "geometry", "#777777")),
        "water": rgb565(style_color(style, "water", "geometry.fill", "#7088b0")),
        "black": rgb565("#000000"),
    }
    buf = [colors["background"]] * (WIDTH * HEIGHT)
    center = map_data["center"]

    for feature in map_data["features"]:
        if feature["kind"] not in ("water", "land", "building"):
            continue
        pts = [project(center, lat, lon, scale) for lat, lon in feature.get("points", [])]
        if feature["kind"] == "water":
            fill_polygon(buf, pts, colors["water"])
        elif feature["kind"] == "land":
            fill_polygon(buf, pts, colors["park"])
        elif feature["kind"] == "building":
            fill_polygon(buf, pts, colors["man_made"])

    roads = [feature for feature in map_data["features"] if feature["kind"] == "road"]
    roads.sort(key=lambda item: item.get("weight", 0))
    for feature in roads:
        pts = [project(center, lat, lon, scale) for lat, lon in feature.get("points", [])]
        width = road_width(int(feature.get("weight", 1)), overview)
        for (x0, y0), (x1, y1) in zip(pts, pts[1:]):
            draw_line(buf, x0, y0, x1, y1, colors["black"], width + 4)
            draw_line(buf, x0, y0, x1, y1, road_color(int(feature.get("weight", 1)), colors), width)

    return buf


def write_header(path: Path, name: str, buf: list[int]) -> None:
    lines = [
        "#pragma once",
        "#include <Arduino.h>",
        "",
        f"const uint16_t {name}_W = {WIDTH};",
        f"const uint16_t {name}_H = {HEIGHT};",
        f"const uint16_t {name}[{WIDTH} * {HEIGHT}] PROGMEM = {{",
    ]
    for i in range(0, len(buf), 16):
        lines.append("  " + ", ".join(f"0x{value:04X}" for value in buf[i:i + 16]) + ",")
    lines.append("};")
    path.write_text("\n".join(lines), encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--map-json", type=Path, required=True)
    parser.add_argument("--style-json", type=Path, required=True)
    parser.add_argument("--out-dir", type=Path, required=True)
    parser.add_argument("--close-scale", type=float, default=1.48)
    parser.add_argument("--overview-scale", type=float, default=0.62)
    args = parser.parse_args()

    map_data = json.loads(args.map_json.read_text(encoding="utf-8"))
    style = json.loads(args.style_json.read_text(encoding="utf-8"))
    args.out_dir.mkdir(parents=True, exist_ok=True)
    close = render(map_data, style, args.close_scale, False)
    overview = render(map_data, style, args.overview_scale, True)
    write_header(args.out_dir / "gta_map_watch_close.h", "GTA_MAP_WATCH_CLOSE", close)
    write_header(args.out_dir / "gta_map_watch_overview.h", "GTA_MAP_WATCH_OVERVIEW", overview)
    print(f"Rendered close and overview headers to {args.out_dir}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
