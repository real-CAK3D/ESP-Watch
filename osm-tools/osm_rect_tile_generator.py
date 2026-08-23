#!/usr/bin/env python3
"""
Generate lightweight rectangular GTA-Nav map tiles from OpenStreetMap XML.

Input:
  .osm XML exported from Overpass, JOSM, BBBike, or another OSM source.

Output:
  gta_tiles/
    manifest.json
    z<zoom>_<x>_<y>.json

The output format is intentionally simple JSON so the dashboard can preview it
and the ESP32 firmware can later switch to a compact binary reader.
"""

from __future__ import annotations

import argparse
import json
import math
import xml.etree.ElementTree as ET
from collections import defaultdict
from pathlib import Path


ROAD_CLASSES = {
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
}


def lon_to_tile_x(lon: float, zoom: int) -> int:
    return int((lon + 180.0) / 360.0 * (1 << zoom))


def lat_to_tile_y(lat: float, zoom: int) -> int:
    lat_rad = math.radians(lat)
    return int((1.0 - math.asinh(math.tan(lat_rad)) / math.pi) / 2.0 * (1 << zoom))


def lonlat_to_pixel(lon: float, lat: float, zoom: int, tile_x: int, tile_y: int) -> tuple[int, int]:
    n = 1 << zoom
    x = ((lon + 180.0) / 360.0 * n - tile_x) * 256.0
    lat_rad = math.radians(lat)
    y = ((1.0 - math.asinh(math.tan(lat_rad)) / math.pi) / 2.0 * n - tile_y) * 256.0
    return int(round(x)), int(round(y))


def parse_osm(path: Path) -> tuple[dict[str, tuple[float, float]], list[dict]]:
    nodes: dict[str, tuple[float, float]] = {}
    roads: list[dict] = []

    for event, elem in ET.iterparse(path, events=("end",)):
        if elem.tag == "node":
            nodes[elem.attrib["id"]] = (float(elem.attrib["lat"]), float(elem.attrib["lon"]))
            elem.clear()
        elif elem.tag == "way":
            tags = {tag.attrib.get("k"): tag.attrib.get("v") for tag in elem.findall("tag")}
            highway = tags.get("highway")
            if highway in ROAD_CLASSES:
                refs = [nd.attrib["ref"] for nd in elem.findall("nd")]
                roads.append(
                    {
                        "name": tags.get("name", ""),
                        "class": highway,
                        "weight": ROAD_CLASSES[highway],
                        "refs": refs,
                    }
                )
            elem.clear()

    return nodes, roads


def build_tiles(nodes: dict[str, tuple[float, float]], roads: list[dict], zoom: int) -> dict[tuple[int, int], list[dict]]:
    tiles: dict[tuple[int, int], list[dict]] = defaultdict(list)

    for road in roads:
        points = [nodes[ref] for ref in road["refs"] if ref in nodes]
        for a, b in zip(points, points[1:]):
            lat1, lon1 = a
            lat2, lon2 = b
            tx = lon_to_tile_x((lon1 + lon2) / 2.0, zoom)
            ty = lat_to_tile_y((lat1 + lat2) / 2.0, zoom)
            x1, y1 = lonlat_to_pixel(lon1, lat1, zoom, tx, ty)
            x2, y2 = lonlat_to_pixel(lon2, lat2, zoom, tx, ty)
            tiles[(tx, ty)].append(
                {
                    "kind": "road",
                    "name": road["name"],
                    "class": road["class"],
                    "weight": road["weight"],
                    "line": [x1, y1, x2, y2],
                }
            )

    return tiles


def write_tiles(tiles: dict[tuple[int, int], list[dict]], out_dir: Path, zoom: int) -> None:
    out_dir.mkdir(parents=True, exist_ok=True)
    manifest = {
        "format": "gta-nav-rect-json-v1",
        "zoom": zoom,
        "tile_size": 256,
        "tiles": [],
        "styles": {
            "dark": {"background": "#1f2b23", "road": "#756f65", "route": "#ffd24a"},
            "light": {"background": "#d7dfc2", "road": "#7d8279", "route": "#1768ff"},
        },
    }

    for (tx, ty), features in sorted(tiles.items()):
        filename = f"z{zoom}_{tx}_{ty}.json"
        payload = {"z": zoom, "x": tx, "y": ty, "features": features}
        (out_dir / filename).write_text(json.dumps(payload, separators=(",", ":")), encoding="utf-8")
        manifest["tiles"].append({"x": tx, "y": ty, "file": filename, "features": len(features)})

    (out_dir / "manifest.json").write_text(json.dumps(manifest, indent=2), encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(description="Generate GTA-Nav rectangular OSM road tiles.")
    parser.add_argument("osm_xml", type=Path, help="Input .osm XML file")
    parser.add_argument("--out", type=Path, default=Path("gta_tiles"), help="Output folder")
    parser.add_argument("--zoom", type=int, default=16, help="Slippy-map zoom level")
    args = parser.parse_args()

    nodes, roads = parse_osm(args.osm_xml)
    tiles = build_tiles(nodes, roads, args.zoom)
    write_tiles(tiles, args.out, args.zoom)
    print(f"Wrote {len(tiles)} tiles from {len(roads)} roads to {args.out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
