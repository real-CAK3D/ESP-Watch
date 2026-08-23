#!/usr/bin/env python3
"""Static GTA-Nav dashboard server with a tiny same-origin geocode proxy."""

from __future__ import annotations

import json
import math
import urllib.parse
import urllib.request
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path


ROOT = Path(__file__).resolve().parent


def distance_meters(lat1: float, lon1: float, lat2: float, lon2: float) -> float:
    radius = 6371000.0
    p1 = math.radians(lat1)
    p2 = math.radians(lat2)
    d_lat = math.radians(lat2 - lat1)
    d_lon = math.radians(lon2 - lon1)
    h = math.sin(d_lat / 2) ** 2 + math.cos(p1) * math.cos(p2) * math.sin(d_lon / 2) ** 2
    return 2 * radius * math.atan2(math.sqrt(h), math.sqrt(1 - h))


class DashboardHandler(SimpleHTTPRequestHandler):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, directory=str(ROOT), **kwargs)

    def end_headers(self) -> None:
        self.send_header("Cache-Control", "no-store")
        super().end_headers()

    def do_GET(self) -> None:
        parsed = urllib.parse.urlparse(self.path)
        if parsed.path == "/api/geocode":
            self.handle_geocode(parsed)
            return
        super().do_GET()

    def handle_geocode(self, parsed: urllib.parse.ParseResult) -> None:
        params = urllib.parse.parse_qs(parsed.query)
        query = (params.get("q") or [""])[0].strip()
        if not query:
            self.send_json({"error": "Missing q"}, 400)
            return
        bias_lat = float((params.get("lat") or ["44.097327"])[0])
        bias_lon = float((params.get("lon") or ["-70.162827"])[0])
        has_region = any(token in query.lower() for token in [" maine", ", me", ",me", " lewiston", " auburn"])
        queries = [query] if has_region else [f"{query}, Lewiston, Maine", f"{query}, Maine", query]
        hits: list[dict] = []
        for query_index, item in enumerate(queries):
            search_params = {
                "q": item,
                "format": "jsonv2",
                "limit": "8",
                "addressdetails": "1",
                "countrycodes": "us",
                "viewbox": f"{bias_lon - 0.45},{bias_lat + 0.35},{bias_lon + 0.45},{bias_lat - 0.35}",
                "bounded": "0",
            }
            url = "https://nominatim.openstreetmap.org/search?" + urllib.parse.urlencode(search_params)
            request = urllib.request.Request(url, headers={"User-Agent": "GTA-Nav local dashboard"})
            try:
                with urllib.request.urlopen(request, timeout=12) as response:
                    results = json.loads(response.read().decode("utf-8"))
            except OSError:
                results = []
            for result in results:
                lat = float(result["lat"])
                lon = float(result["lon"])
                result["distance"] = distance_meters(bias_lat, bias_lon, lat, lon)
                result["queryIndex"] = query_index
                hits.append(result)
            if any(hit["distance"] < 30000 for hit in hits):
                break
        hits.sort(key=lambda hit: hit["distance"] + hit["queryIndex"] * 50000)
        self.send_json({"results": hits[:8]})

    def send_json(self, payload: dict, status: int = 200) -> None:
        body = json.dumps(payload).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)


def main() -> int:
    server = ThreadingHTTPServer(("127.0.0.1", 8765), DashboardHandler)
    print("GTA-Nav dashboard listening on http://127.0.0.1:8765")
    server.serve_forever()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
