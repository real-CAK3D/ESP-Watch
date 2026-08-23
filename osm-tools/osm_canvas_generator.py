#!/usr/bin/env python3
"""Convert an OSM XML extract into the GTA-Nav dashboard canvas map format."""

from __future__ import annotations

import argparse
import json
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


def feature_kind(tags: dict[str, str]) -> str | None:
    highway = tags.get("highway")
    if highway in ROAD_WEIGHTS:
        return "road"
    if tags.get("natural") == "water" or tags.get("waterway") in {"riverbank", "stream", "river"}:
        return "water"
    if tags.get("leisure") == "park" or tags.get("landuse") in {"grass", "recreation_ground", "forest", "meadow"}:
        return "land"
    if "building" in tags:
        return "building"
    return None


def parse_osm(path: Path) -> tuple[dict[str, tuple[float, float]], list[dict]]:
    nodes: dict[str, tuple[float, float]] = {}
    features: list[dict] = []
    root = ET.parse(path).getroot()

    for node in root.findall("node"):
        nodes[node.attrib["id"]] = (float(node.attrib["lat"]), float(node.attrib["lon"]))

    for way in root.findall("way"):
        tags = {tag.attrib.get("k", ""): tag.attrib.get("v", "") for tag in way.findall("tag")}
        kind = feature_kind(tags)
        if not kind:
            continue
        points = [nodes[nd.attrib["ref"]] for nd in way.findall("nd") if nd.attrib.get("ref") in nodes]
        if len(points) < 2:
            continue
        feature = {
            "kind": kind,
            "weight": ROAD_WEIGHTS.get(tags.get("highway", ""), 1),
            "name": tags.get("name", ""),
            "points": points,
        }
        features.append(feature)

    return nodes, features


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("osm_xml", type=Path)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--center-lat", type=float, required=True)
    parser.add_argument("--center-lon", type=float, required=True)
    parser.add_argument("--min-lat", type=float, required=True)
    parser.add_argument("--max-lat", type=float, required=True)
    parser.add_argument("--min-lon", type=float, required=True)
    parser.add_argument("--max-lon", type=float, required=True)
    args = parser.parse_args()

    _, features = parse_osm(args.osm_xml)
    payload = {
        "format": "gta-nav-osm-canvas-v1",
        "center": {"lat": args.center_lat, "lon": args.center_lon},
        "bounds": {
            "minLat": args.min_lat,
            "maxLat": args.max_lat,
            "minLon": args.min_lon,
            "maxLon": args.max_lon,
        },
        "features": features,
        "counts": {
            "road": sum(1 for item in features if item["kind"] == "road"),
            "building": sum(1 for item in features if item["kind"] == "building"),
            "water": sum(1 for item in features if item["kind"] == "water"),
            "land": sum(1 for item in features if item["kind"] == "land"),
        },
    }
    args.out.write_text(json.dumps(payload, separators=(",", ":")), encoding="utf-8")
    print(f"Wrote {len(features)} features to {args.out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
