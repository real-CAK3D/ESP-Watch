"""Phone simulator: does everything the Android app does, but over USB to the watch.

Reference implementation of the map packing, routing and turn-by-turn logic that the
app (android/.../MapPacker.kt, Navigator.kt) mirrors.

  python phonesim.py map LAT LON                 push OSM map + GPS fix
  python phonesim.py drive LAT LON DLAT DLON     route there and simulate the trip
  python phonesim.py walk  LAT LON DLAT DLON     same, walking
  python phonesim.py demo                        weather, places, notification, time
"""
import json
import math
import struct
import sys
import time
import urllib.parse
import urllib.request

from watchctl import Watch

UA = {"User-Agent": "GTA-Watch/1.0 (personal smartwatch project)"}
MSG_TIME, MSG_WEATHER, MSG_GPS, MSG_MAP, MSG_NAV, MSG_ROUTE, MSG_NOTIFY, MSG_PLACES, MSG_SETTINGS, MSG_PHONE = range(1, 11)

KIND = {"motorway": 1, "primary": 2, "secondary": 3, "street": 4, "service": 5, "path": 6, "rail": 7,
        "water": 20, "river": 21, "park": 22, "beach": 23}
HIGHWAY = {
    "motorway": "motorway", "motorway_link": "motorway", "trunk": "motorway", "trunk_link": "motorway",
    "primary": "primary", "primary_link": "primary",
    "secondary": "secondary", "secondary_link": "secondary", "tertiary": "secondary", "tertiary_link": "secondary",
    "residential": "street", "unclassified": "street", "living_street": "street", "road": "street",
    "service": "service", "track": "service",
    "footway": "path", "path": "path", "cycleway": "path", "pedestrian": "path", "steps": "path", "bridleway": "path",
}
AREA_TAGS = [("natural", "water", "water"), ("landuse", "reservoir", "water"), ("landuse", "basin", "water"),
             ("leisure", "park", "park"), ("leisure", "garden", "park"), ("leisure", "golf_course", "park"),
             ("leisure", "pitch", "park"), ("landuse", "grass", "park"), ("landuse", "forest", "park"),
             ("landuse", "meadow", "park"), ("landuse", "recreation_ground", "park"), ("landuse", "cemetery", "park"),
             ("natural", "wood", "park"), ("natural", "scrub", "park"), ("natural", "wetland", "park"),
             ("natural", "beach", "beach")]

M_LAT = 110574.0


def m_lon(lat):
    return 111320.0 * math.cos(math.radians(lat))


def get_json(url, data=None):
    req = urllib.request.Request(url, data=data, headers=UA)
    with urllib.request.urlopen(req, timeout=60) as r:
        return json.loads(r.read())


# ---------------------------------------------------------------- map packing
def overpass(lat, lon, radius):
    a = f"(around:{radius},{lat},{lon})"
    q = f"""[out:json][timeout:40];
(
  way["highway"]{a};
  way["railway"="rail"]{a};
  way["waterway"~"^(river|stream|canal)$"]{a};
  way["natural"~"^(water|wood|scrub|wetland|beach)$"]{a};
  way["landuse"~"^(reservoir|basin|grass|forest|meadow|recreation_ground|cemetery)$"]{a};
  way["leisure"~"^(park|garden|golf_course|pitch)$"]{a};
  relation["natural"="water"]{a};
  relation["landuse"~"^(reservoir|forest)$"]{a};
  relation["leisure"="park"]{a};
);
out geom;"""
    import os
    cache = os.path.join(os.path.dirname(os.path.abspath(__file__)), "osm_cache", f"{lat:.4f}_{lon:.4f}_{radius}.json")
    if os.path.exists(cache):
        return json.load(open(cache, encoding="utf-8"))
    for host in ("https://overpass-api.de/api/interpreter", "https://overpass.private.coffee/api/interpreter",
                 "https://overpass.kumi.systems/api/interpreter", "https://maps.mail.ru/osm/tools/overpass/api/interpreter"):
        try:
            data = get_json(host, urllib.parse.urlencode({"data": q}).encode())
            os.makedirs(os.path.dirname(cache), exist_ok=True)
            json.dump(data, open(cache, "w", encoding="utf-8"))
            return data
        except Exception as e:  # noqa: BLE001
            print("overpass failed on", host, e)
    raise RuntimeError("overpass unavailable")


def classify(tags):
    hw = tags.get("highway")
    if hw:
        if tags.get("footway") in ("sidewalk", "crossing") or tags.get("area") == "yes":
            return None
        if hw == "service" and tags.get("service") in ("parking_aisle", "driveway", "drive-through"):
            return None
        k = HIGHWAY.get(hw)
        return k
    if tags.get("railway") == "rail" and tags.get("service") is None:
        return "rail"
    if tags.get("waterway") in ("river", "stream", "canal"):
        return "river"
    for key, val, kind in AREA_TAGS:
        if tags.get(key) == val:
            return kind
    return None


def stitch(ways):
    """Join way segments (lists of (lat,lon)) into closed rings."""
    ways = [list(w) for w in ways if len(w) >= 2]
    rings = []
    while ways:
        ring = ways.pop(0)
        changed = True
        while ring[0] != ring[-1] and changed:
            changed = False
            for i, w in enumerate(ways):
                if w[0] == ring[-1]:
                    ring += w[1:]
                elif w[-1] == ring[-1]:
                    ring += w[-2::-1]
                elif w[-1] == ring[0]:
                    ring = w[:-1] + ring
                elif w[0] == ring[0]:
                    ring = w[:0:-1] + ring
                else:
                    continue
                ways.pop(i)
                changed = True
                break
        if len(ring) >= 4:
            rings.append(ring)
    return rings


def simplify(pts, tol):
    """Douglas-Peucker on (x,y) meters."""
    if len(pts) < 3:
        return pts
    keep = [False] * len(pts)
    keep[0] = keep[-1] = True
    stack = [(0, len(pts) - 1)]
    while stack:
        a, b = stack.pop()
        ax, ay = pts[a]
        bx, by = pts[b]
        dx, dy = bx - ax, by - ay
        L = math.hypot(dx, dy) or 1e-9
        best, bi = -1, -1
        for i in range(a + 1, b):
            px, py = pts[i]
            d = abs(dy * px - dx * py + bx * ay - by * ax) / L
            if d > best:
                best, bi = d, i
        if best > tol:
            keep[bi] = True
            stack += [(a, bi), (bi, b)]
    return [p for p, k in zip(pts, keep) if k]


def clip_poly(pts, lim):
    """Sutherland-Hodgman clip against the square [-lim, lim]^2."""
    def clip(points, inside, inter):
        out = []
        for i in range(len(points)):
            cur, prev = points[i], points[i - 1]
            if inside(cur):
                if not inside(prev):
                    out.append(inter(prev, cur))
                out.append(cur)
            elif inside(prev):
                out.append(inter(prev, cur))
        return out

    def ix(a, b, x):
        t = (x - a[0]) / (b[0] - a[0])
        return (x, a[1] + t * (b[1] - a[1]))

    def iy(a, b, y):
        t = (y - a[1]) / (b[1] - a[1])
        return (a[0] + t * (b[0] - a[0]), y)

    for inside, inter in ((lambda p: p[0] >= -lim, lambda a, b: ix(a, b, -lim)),
                          (lambda p: p[0] <= lim, lambda a, b: ix(a, b, lim)),
                          (lambda p: p[1] >= -lim, lambda a, b: iy(a, b, -lim)),
                          (lambda p: p[1] <= lim, lambda a, b: iy(a, b, lim))):
        if not pts:
            break
        pts = clip(pts, inside, inter)
    return pts


def build_map(lat0, lon0, radius=1600):
    data = overpass(lat0, lon0, radius)
    ml = m_lon(lat0)
    proj = lambda la, lo: ((lo - lon0) * ml, (la - lat0) * M_LAT)  # noqa: E731
    lim = radius * 1.25
    feats = []  # (kind, [(x,y)], name)
    for el in data.get("elements", []):
        tags = el.get("tags", {})
        kind = classify(tags)
        if not kind:
            continue
        if el["type"] == "way" and "geometry" in el:
            pts = [proj(g["lat"], g["lon"]) for g in el["geometry"]]
            if kind in ("water", "park", "beach"):
                if len(pts) < 4:
                    continue
                pts = clip_poly(simplify(pts, 1.5), lim)
                if len(pts) >= 3:
                    feats.append((kind, pts, None))
            else:
                feats.append((kind, simplify(pts, 1.0), tags.get("name")))
        elif el["type"] == "relation":
            outers = [[(g["lat"], g["lon"]) for g in m.get("geometry", [])]
                      for m in el.get("members", []) if m.get("role") == "outer" and m.get("geometry")]
            for ring in stitch(outers):
                pts = clip_poly(simplify([proj(a, b) for a, b in ring], 2.0), lim)
                if len(pts) >= 3:
                    feats.append((kind, pts, None))
    order = {"water": 0, "park": 1, "beach": 1, "river": 2, "path": 3, "rail": 3, "service": 4, "street": 5,
             "secondary": 6, "primary": 7, "motorway": 8}
    feats.sort(key=lambda f: order[f[0]])

    out = bytearray(b"GM\x01\x00")
    out += struct.pack("<iiH", round(lat0 * 1e7), round(lon0 * 1e7), 0)
    count = 0
    for kind, pts, _ in feats:
        q = [(max(-32767, min(32767, round(x * 2))), max(-32767, min(32767, round(y * 2)))) for x, y in pts]
        # split very long features so npts fits comfortably
        for i in range(0, len(q), 4000):
            chunk = q[i:i + 4001] if kind not in ("water", "park", "beach") else q
            out += struct.pack("<BBH", KIND[kind], 0, len(chunk))
            for x, y in chunk:
                out += struct.pack("<hh", x, y)
            count += 1
            if kind in ("water", "park", "beach"):
                break
    struct.pack_into("<H", out, 12, count)
    named = [(f[1], f[2]) for f in feats if f[2]]
    return bytes(out), named


def nearest_street(named, x, y):
    best, name = 1e9, ""
    for pts, nm in named:
        for (ax, ay), (bx, by) in zip(pts, pts[1:]):
            dx, dy = bx - ax, by - ay
            L2 = dx * dx + dy * dy or 1e-9
            t = max(0, min(1, ((x - ax) * dx + (y - ay) * dy) / L2))
            d = math.hypot(ax + t * dx - x, ay + t * dy - y)
            if d < best:
                best, name = d, nm
    return name if best < 40 else ""


# ---------------------------------------------------------------- routing + turn-by-turn
def osrm_route(lat, lon, dlat, dlon, walking):
    base = "https://routing.openstreetmap.de/routed-foot" if walking else "https://router.project-osrm.org"
    url = f"{base}/route/v1/driving/{lon},{lat};{dlon},{dlat}?overview=full&geometries=geojson&steps=true"
    r = get_json(url)
    if r.get("code") != "Ok":
        raise RuntimeError(r)
    return r["routes"][0]


MOD = {"straight": 0, "slight left": 1, "left": 2, "sharp left": 3, "slight right": 4, "right": 5,
       "sharp right": 6, "uturn": 7}


def maneuver_code(step):
    m = step["maneuver"]
    t, mod = m.get("type"), m.get("modifier", "straight")
    if t == "arrive":
        return 8
    if t in ("roundabout", "rotary", "roundabout turn", "exit roundabout", "exit rotary"):
        return 9
    if t == "merge":
        return 10
    if t == "fork":
        return 11 if "left" in mod else 12
    if t == "depart":
        return 13
    return MOD.get(mod, 0)


def instruction(step):
    m = step["maneuver"]
    t, mod, name = m.get("type"), m.get("modifier", ""), step.get("name") or "the road"
    if t == "arrive":
        return "Arrive at destination"
    if t == "depart":
        return f"Head out on {name}"
    if t in ("roundabout", "rotary"):
        return f"Roundabout, exit {m.get('exit', 1)} to {name}"
    if mod == "uturn":
        return "Make a U-turn"
    if t in ("continue", "new name") and mod in ("straight", ""):
        return f"Continue on {name}"
    return f"{mod.capitalize()} onto {name}".strip()


class Navigator:
    """Tracks progress along an OSRM route and produces the watch MSG_NAV payload."""

    def __init__(self, route, walking, dest_name):
        self.walking = walking
        self.dest = dest_name
        coords = route["geometry"]["coordinates"]  # [lon, lat]
        self.lat0, self.lon0 = coords[0][1], coords[0][0]
        ml = m_lon(self.lat0)
        self.xy = [((c[0] - self.lon0) * ml, (c[1] - self.lat0) * M_LAT) for c in coords]
        self.cum = [0.0]
        for (ax, ay), (bx, by) in zip(self.xy, self.xy[1:]):
            self.cum.append(self.cum[-1] + math.hypot(bx - ax, by - ay))
        self.total = self.cum[-1]
        self.duration = route["duration"]
        self.steps = []
        for leg in route["legs"]:
            for s in leg["steps"]:
                lo, la = s["maneuver"]["location"]
                self.steps.append((self.along((lo - self.lon0) * ml, (la - self.lat0) * M_LAT)[0], s))

    def along(self, x, y):
        best = (1e18, 0.0)
        for i, ((ax, ay), (bx, by)) in enumerate(zip(self.xy, self.xy[1:])):
            dx, dy = bx - ax, by - ay
            L2 = dx * dx + dy * dy or 1e-9
            t = max(0, min(1, ((x - ax) * dx + (y - ay) * dy) / L2))
            d = math.hypot(ax + t * dx - x, ay + t * dy - y)
            if d < best[0]:
                best = (d, self.cum[i] + t * math.sqrt(L2))
        return best[1], best[0]  # distance along route, off-route distance

    def route_payload(self):
        out = bytearray(struct.pack("<iiH", round(self.lat0 * 1e7), round(self.lon0 * 1e7), len(self.xy)))
        for x, y in self.xy:
            out += struct.pack("<hh", round(x * 2), round(y * 2))
        return bytes(out)

    def update(self, lat, lon):
        ml = m_lon(self.lat0)
        prog, off = self.along((lon - self.lon0) * ml, (lat - self.lat0) * M_LAT)
        nxt = next((s for s in self.steps if s[0] > prog + 3), self.steps[-1])
        remain = max(0.0, self.total - prog)
        return {
            "active": True, "mode": "walk" if self.walking else "drive",
            "man": maneuver_code(nxt[1]), "dist": round(max(0.0, nxt[0] - prog), 1),
            "remain": round(remain), "total": round(self.total),
            "eta": round(self.duration * remain / max(self.total, 1)),
            "instr": instruction(nxt[1]), "street": nxt[1].get("name", ""),
            "dest": self.dest, "cost": "" if self.walking else fuel_cost(remain),
        }, off


def fuel_cost(meters, mpg=25.0, price=3.29):
    return f"${meters / 1609.34 / mpg * price:.2f}"


# ---------------------------------------------------------------- commands
def jmsg(w, t, obj):
    return w.msg(t, json.dumps(obj, separators=(",", ":")).encode())


def push_time(w):
    tz = -time.altzone if time.localtime().tm_isdst else -time.timezone
    jmsg(w, MSG_TIME, {"t": int(time.time()), "tz": tz})


def push_map(w, lat, lon):
    t0 = time.time()
    blob, named = build_map(lat, lon)
    print(f"map: {len(blob)} bytes, built in {time.time() - t0:.1f}s ->", w.msg(MSG_MAP, blob))
    return named


def bearing(a, b):
    return (math.degrees(math.atan2(b[0] - a[0], b[1] - a[1])) + 360) % 360


def simulate(w, lat, lon, dlat, dlon, walking, speed=None, shots_every=0):
    push_time(w)
    named = push_map(w, lat, lon)
    route = osrm_route(lat, lon, dlat, dlon, walking)
    nav = Navigator(route, walking, "Destination")
    print("route:", round(nav.total), "m,", len(nav.xy), "points ->", w.msg(MSG_ROUTE, nav.route_payload()))
    speed = speed or (1.5 if walking else 11.0)
    ml = m_lon(nav.lat0)
    d, i, n = 0.0, 0, 0
    while d <= nav.total:
        while i < len(nav.cum) - 2 and nav.cum[i + 1] < d:
            i += 1
        a, b = nav.xy[i], nav.xy[i + 1]
        seg = nav.cum[i + 1] - nav.cum[i] or 1
        t = (d - nav.cum[i]) / seg
        x, y = a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t
        plat, plon = nav.lat0 + y / M_LAT, nav.lon0 + x / ml
        # street lookup uses the map anchor frame
        mx, my = (plon - lon) * m_lon(lat), (plat - lat) * M_LAT
        jmsg(w, MSG_GPS, {"lat": plat, "lon": plon, "hdg": round(bearing(a, b), 1), "spd": speed, "acc": 5,
                          "street": nearest_street(named, mx, my), "area": "Lewiston"})
        payload, _ = nav.update(plat, plon)
        jmsg(w, MSG_NAV, payload)
        n += 1
        if shots_every and n % shots_every == 0:
            print("shot", w.shot(f"shots/nav_{n:03d}.png"))
        time.sleep(1.0)
        d += speed
    jmsg(w, MSG_NAV, {"active": False})


def demo(w):
    push_time(w)
    jmsg(w, MSG_WEATHER, {"city": "Lewiston", "t": 58, "hi": 63, "lo": 44, "feels": 55, "hum": 62, "wind": 8,
                          "code": 2, "hours": [[13, 59, 2], [14, 61, 1], [15, 62, 1], [16, 60, 3], [17, 57, 61]]})
    jmsg(w, MSG_PHONE, {"bat": 76, "chg": False})
    jmsg(w, MSG_PLACES, [
        {"n": "Safe House", "k": "safehouse", "lat": 44.0955, "lon": -70.2105},
        {"n": "Los Santos Customs", "k": "mechanic", "lat": 44.0921, "lon": -70.2052},
        {"n": "Cluckin' Bell", "k": "food", "lat": 44.0989, "lon": -70.2183},
        {"n": "Maze Bank", "k": "bank", "lat": 44.0978, "lon": -70.2136},
        {"n": "Pay 'n' Spray", "k": "carwash", "lat": 44.1003, "lon": -70.2094},
    ])
    jmsg(w, MSG_NOTIFY, {"id": 1, "app": "Messages", "title": "Lester",
                         "body": "I've got a job for you. Come to the factory when you can."})


def main():
    a = sys.argv[1:]
    w = Watch()
    if a[0] == "map":
        push_time(w)
        lat, lon = float(a[1]), float(a[2])
        named = push_map(w, lat, lon)
        jmsg(w, MSG_GPS, {"lat": lat, "lon": lon, "hdg": float(a[3]) if len(a) > 3 else 0, "spd": 0, "acc": 5,
                          "street": nearest_street(named, 0, 0), "area": "Lewiston"})
    elif a[0] in ("drive", "walk"):
        simulate(w, *map(float, a[1:5]), walking=a[0] == "walk", shots_every=int(a[5]) if len(a) > 5 else 0)
    elif a[0] == "demo":
        demo(w)


if __name__ == "__main__":
    main()
