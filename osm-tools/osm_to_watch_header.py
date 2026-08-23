#!/usr/bin/env python3
"""Convert a small OSM XML road extract into a GTA-Nav ESP32 header."""

from __future__ import annotations

import argparse
import math
import xml.etree.ElementTree as ET
from pathlib import Path


ROAD_WEIGHTS = {
    "motorway": 7,
    "trunk": 7,
    "primary": 6,
    "secondary": 5,
    "tertiary": 4,
    "residential": 3,
    "service": 2,
    "unclassified": 2,
    "living_street": 2,
    "cycleway": 1,
    "path": 1,
    "footway": 1,
}


def main() -> int:
    parser = argparse.ArgumentParser(description="Generate a compact C++ road header from OSM XML.")
    parser.add_argument("osm_xml", type=Path)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--center-lat", type=float, required=True)
    parser.add_argument("--center-lon", type=float, required=True)
    parser.add_argument("--max-segments", type=int, default=1100)
    args = parser.parse_args()

    nodes: dict[str, tuple[float, float]] = {}
    roads: list[tuple[int, int, int, int, int]] = []
    root = ET.parse(args.osm_xml).getroot()
    meters_per_lon = 111320.0 * math.cos(math.radians(args.center_lat))

    for node in root.findall("node"):
        nodes[node.attrib["id"]] = (float(node.attrib["lat"]), float(node.attrib["lon"]))

    for way in root.findall("way"):
        tags = {tag.attrib.get("k"): tag.attrib.get("v") for tag in way.findall("tag")}
        highway = tags.get("highway")
        if highway not in ROAD_WEIGHTS:
            continue
        points = [nodes[ref.attrib["ref"]] for ref in way.findall("nd") if ref.attrib.get("ref") in nodes]
        for (lat1, lon1), (lat2, lon2) in zip(points, points[1:]):
            x1 = round((lon1 - args.center_lon) * meters_per_lon)
            y1 = round((args.center_lat - lat1) * 111320.0)
            x2 = round((lon2 - args.center_lon) * meters_per_lon)
            y2 = round((args.center_lat - lat2) * 111320.0)
            if abs(x1 - x2) + abs(y1 - y2) >= 3 and all(-32760 < v < 32760 for v in (x1, y1, x2, y2)):
                roads.append((x1, y1, x2, y2, ROAD_WEIGHTS[highway]))

    roads.sort(key=lambda road: (-road[4], abs(road[0]) + abs(road[1]) + abs(road[2]) + abs(road[3])))
    roads = roads[: args.max_segments]

    lines = [
        "#pragma once",
        "#include <stdint.h>",
        "",
        "struct OsmRoadSegment { int16_t x1; int16_t y1; int16_t x2; int16_t y2; uint8_t weight; };",
        f"static const float LEWISTON_CENTER_LAT = {args.center_lat:.6f}f;",
        f"static const float LEWISTON_CENTER_LON = {args.center_lon:.6f}f;",
        f"static const uint16_t LEWISTON_OSM_ROAD_COUNT = {len(roads)};",
        "static const OsmRoadSegment LEWISTON_OSM_ROADS[] = {",
    ]
    lines.extend(f"  {{{x1}, {y1}, {x2}, {y2}, {weight}}}," for x1, y1, x2, y2, weight in roads)
    lines.extend(["};", ""])
    args.out.write_text("\n".join(lines), encoding="utf-8")
    print(f"Wrote {len(roads)} road segments to {args.out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
