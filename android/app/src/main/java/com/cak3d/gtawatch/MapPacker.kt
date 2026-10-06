package com.cak3d.gtawatch

import org.json.JSONArray
import org.json.JSONObject
import java.io.ByteArrayOutputStream
import java.nio.ByteBuffer
import java.nio.ByteOrder
import kotlin.math.abs
import kotlin.math.cos
import kotlin.math.hypot
import kotlin.math.roundToInt

/**
 * Downloads OpenStreetMap roads / water / parks around a point (Overpass API) and packs them into
 * the watch's MSG_MAP format (see firmware/src/mapdata.h). Port of tools/phonesim.py build_map().
 */
object MapPacker {
    const val M_LAT = 110574.0
    fun mLon(lat: Double) = 111320.0 * cos(Math.toRadians(lat))

    private val KIND = mapOf("motorway" to 1, "primary" to 2, "secondary" to 3, "street" to 4, "service" to 5,
        "path" to 6, "rail" to 7, "water" to 20, "river" to 21, "park" to 22, "beach" to 23)
    private val HIGHWAY = mapOf(
        "motorway" to "motorway", "motorway_link" to "motorway", "trunk" to "motorway", "trunk_link" to "motorway",
        "primary" to "primary", "primary_link" to "primary",
        "secondary" to "secondary", "secondary_link" to "secondary", "tertiary" to "secondary", "tertiary_link" to "secondary",
        "residential" to "street", "unclassified" to "street", "living_street" to "street", "road" to "street",
        "service" to "service", "track" to "service",
        "footway" to "path", "path" to "path", "cycleway" to "path", "pedestrian" to "path", "steps" to "path", "bridleway" to "path")
    private val AREA = listOf(
        Triple("natural", "water", "water"), Triple("landuse", "reservoir", "water"), Triple("landuse", "basin", "water"),
        Triple("leisure", "park", "park"), Triple("leisure", "garden", "park"), Triple("leisure", "golf_course", "park"),
        Triple("leisure", "pitch", "park"), Triple("landuse", "grass", "park"), Triple("landuse", "forest", "park"),
        Triple("landuse", "meadow", "park"), Triple("landuse", "recreation_ground", "park"), Triple("landuse", "cemetery", "park"),
        Triple("natural", "wood", "park"), Triple("natural", "scrub", "park"), Triple("natural", "wetland", "park"),
        Triple("natural", "beach", "beach"))
    private val ORDER = mapOf("water" to 0, "park" to 1, "beach" to 1, "river" to 2, "path" to 3, "rail" to 3,
        "service" to 4, "street" to 5, "secondary" to 6, "primary" to 7, "motorway" to 8)

    private val OVERPASS_HOSTS = listOf(
        "https://overpass-api.de/api/interpreter",
        "https://overpass.private.coffee/api/interpreter",
        "https://overpass.kumi.systems/api/interpreter",
        "https://maps.mail.ru/osm/tools/overpass/api/interpreter",
    )

    /** Raw Overpass responses are cached on the phone, so revisiting an area needs no download. */
    private fun cachedOverpass(lat: Double, lon: Double, radius: Int, dir: java.io.File?): Triple<Double, Double, JSONObject> {
        if (dir != null) {
            dir.mkdirs()
            val hit = dir.listFiles { f -> f.name.startsWith("osm_") }?.firstOrNull { f ->
                val p = f.name.removePrefix("osm_").removeSuffix(".json").split("_")
                p.size == 2 && run {
                    val la = p[0].toDouble()
                    val lo = p[1].toDouble()
                    hypot((lo - lon) * mLon(lat), (la - lat) * M_LAT) < 350
                }
            }
            if (hit != null) {
                val p = hit.name.removePrefix("osm_").removeSuffix(".json").split("_")
                try {
                    return Triple(p[0].toDouble(), p[1].toDouble(), JSONObject(hit.readText()))
                } catch (_: Exception) {
                    hit.delete()
                }
            }
        }
        val data = overpass(lat, lon, radius)
        if (dir != null) {
            val f = java.io.File(dir, "osm_%.5f_%.5f.json".format(java.util.Locale.US, lat, lon))
            f.writeText(data.toString())
            dir.listFiles { x -> x.name.startsWith("osm_") }?.sortedBy { it.lastModified() }?.dropLast(40)?.forEach { it.delete() }
        }
        return Triple(lat, lon, data)
    }

    class Feature(val kind: String, val pts: List<DoubleArray>, val name: String?)

    class Result(val lat0: Double, val lon0: Double, val blob: ByteArray, val named: List<Feature>, val featureCount: Int) {
        fun streetAt(lat: Double, lon: Double): String {
            val x = (lon - lon0) * mLon(lat0)
            val y = (lat - lat0) * M_LAT
            var best = 1e9
            var name = ""
            for (f in named) for (i in 0 until f.pts.size - 1) {
                val a = f.pts[i]
                val b = f.pts[i + 1]
                val d = segDist(x, y, a[0], a[1], b[0], b[1])
                if (d < best) {
                    best = d
                    name = f.name ?: ""
                }
            }
            return if (best < 40) name else ""
        }
    }

    fun segDist(px: Double, py: Double, ax: Double, ay: Double, bx: Double, by: Double): Double {
        val dx = bx - ax
        val dy = by - ay
        val l2 = (dx * dx + dy * dy).takeIf { it > 1e-9 } ?: 1e-9
        val t = (((px - ax) * dx + (py - ay) * dy) / l2).coerceIn(0.0, 1.0)
        return hypot(ax + t * dx - px, ay + t * dy - py)
    }

    private fun overpass(lat: Double, lon: Double, radius: Int): JSONObject {
        val a = "(around:$radius,$lat,$lon)"
        val q = """[out:json][timeout:40];
(
  way["highway"]$a;
  way["railway"="rail"]$a;
  way["waterway"~"^(river|stream|canal)$"]$a;
  way["natural"~"^(water|wood|scrub|wetland|beach)$"]$a;
  way["landuse"~"^(reservoir|basin|grass|forest|meadow|recreation_ground|cemetery)$"]$a;
  way["leisure"~"^(park|garden|golf_course|pitch)$"]$a;
  relation["natural"="water"]$a;
  relation["landuse"~"^(reservoir|forest)$"]$a;
  relation["leisure"="park"]$a;
);
out geom;"""
        var last: Exception? = null
        for (host in OVERPASS_HOSTS) {
            try {
                return JSONObject(Net.post(host, "data=" + Net.enc(q)))
            } catch (e: Exception) {
                last = e
            }
        }
        throw last ?: RuntimeException("overpass unavailable")
    }

    private fun classify(t: JSONObject): String? {
        val hw = t.optString("highway")
        if (hw.isNotEmpty()) {
            if (t.optString("footway") in setOf("sidewalk", "crossing") || t.optString("area") == "yes") return null
            if (hw == "service" && t.optString("service") in setOf("parking_aisle", "driveway", "drive-through")) return null
            return HIGHWAY[hw]
        }
        if (t.optString("railway") == "rail" && !t.has("service")) return "rail"
        if (t.optString("waterway") in setOf("river", "stream", "canal")) return "river"
        for ((k, v, kind) in AREA) if (t.optString(k) == v) return kind
        return null
    }

    fun simplify(pts: List<DoubleArray>, tol: Double): List<DoubleArray> {
        if (pts.size < 3) return pts
        val keep = BooleanArray(pts.size)
        keep[0] = true
        keep[pts.size - 1] = true
        val stack = ArrayDeque<IntArray>()
        stack.add(intArrayOf(0, pts.size - 1))
        while (stack.isNotEmpty()) {
            val (a, b) = stack.removeLast().let { it[0] to it[1] }
            var best = -1.0
            var bi = -1
            for (i in a + 1 until b) {
                val d = segDistLine(pts[i], pts[a], pts[b])
                if (d > best) {
                    best = d
                    bi = i
                }
            }
            if (best > tol) {
                keep[bi] = true
                stack.add(intArrayOf(a, bi))
                stack.add(intArrayOf(bi, b))
            }
        }
        return pts.filterIndexed { i, _ -> keep[i] }
    }

    private fun segDistLine(p: DoubleArray, a: DoubleArray, b: DoubleArray): Double {
        val dx = b[0] - a[0]
        val dy = b[1] - a[1]
        val l = hypot(dx, dy).takeIf { it > 1e-9 } ?: 1e-9
        return abs(dy * p[0] - dx * p[1] + b[0] * a[1] - b[1] * a[0]) / l
    }

    /** Sutherland–Hodgman clip of a polygon to the square [-lim, lim]². */
    fun clip(input: List<DoubleArray>, lim: Double): List<DoubleArray> {
        var pts = input
        val edges = listOf<Pair<(DoubleArray) -> Boolean, (DoubleArray, DoubleArray) -> DoubleArray>>(
            Pair({ p -> p[0] >= -lim }, { a, b -> ix(a, b, -lim) }),
            Pair({ p -> p[0] <= lim }, { a, b -> ix(a, b, lim) }),
            Pair({ p -> p[1] >= -lim }, { a, b -> iy(a, b, -lim) }),
            Pair({ p -> p[1] <= lim }, { a, b -> iy(a, b, lim) }),
        )
        for ((inside, inter) in edges) {
            if (pts.isEmpty()) break
            val out = ArrayList<DoubleArray>()
            for (i in pts.indices) {
                val cur = pts[i]
                val prev = pts[(i - 1 + pts.size) % pts.size]
                if (inside(cur)) {
                    if (!inside(prev)) out.add(inter(prev, cur))
                    out.add(cur)
                } else if (inside(prev)) out.add(inter(prev, cur))
            }
            pts = out
        }
        return pts
    }

    private fun ix(a: DoubleArray, b: DoubleArray, x: Double): DoubleArray {
        val t = (x - a[0]) / (b[0] - a[0]); return doubleArrayOf(x, a[1] + t * (b[1] - a[1]))
    }
    private fun iy(a: DoubleArray, b: DoubleArray, y: Double): DoubleArray {
        val t = (y - a[1]) / (b[1] - a[1]); return doubleArrayOf(a[0] + t * (b[0] - a[0]), y)
    }

    /** Joins multipolygon member ways into closed rings. */
    private fun stitch(ways: List<List<DoubleArray>>): List<List<DoubleArray>> {
        val pool = ways.filter { it.size >= 2 }.map { it.toMutableList() }.toMutableList()
        val rings = ArrayList<List<DoubleArray>>()
        fun same(a: DoubleArray, b: DoubleArray) = a[0] == b[0] && a[1] == b[1]
        while (pool.isNotEmpty()) {
            var ring = pool.removeAt(0)
            var changed = true
            while (!same(ring.first(), ring.last()) && changed) {
                changed = false
                for (i in pool.indices) {
                    val w = pool[i]
                    ring = when {
                        same(w.first(), ring.last()) -> (ring + w.drop(1)).toMutableList()
                        same(w.last(), ring.last()) -> (ring + w.reversed().drop(1)).toMutableList()
                        same(w.last(), ring.first()) -> (w.dropLast(1) + ring).toMutableList()
                        same(w.first(), ring.first()) -> (w.reversed().dropLast(1) + ring).toMutableList()
                        else -> continue
                    }
                    pool.removeAt(i)
                    changed = true
                    break
                }
            }
            if (ring.size >= 4) rings.add(ring)
        }
        return rings
    }

    fun build(lat: Double, lon: Double, cacheDir: java.io.File? = null, radius: Int = 1600): Result {
        val (lat0, lon0, data) = cachedOverpass(lat, lon, radius, cacheDir)
        val ml = mLon(lat0)
        fun proj(la: Double, lo: Double) = doubleArrayOf((lo - lon0) * ml, (la - lat0) * M_LAT)
        fun geom(a: JSONArray) = (0 until a.length()).map { a.getJSONObject(it).let { g -> proj(g.getDouble("lat"), g.getDouble("lon")) } }
        val lim = radius * 1.25
        val feats = ArrayList<Feature>()
        val els = data.optJSONArray("elements") ?: JSONArray()
        for (i in 0 until els.length()) {
            val el = els.getJSONObject(i)
            val tags = el.optJSONObject("tags") ?: continue
            val kind = classify(tags) ?: continue
            val area = kind == "water" || kind == "park" || kind == "beach"
            if (el.getString("type") == "way") {
                val g = el.optJSONArray("geometry") ?: continue
                var pts = geom(g)
                if (area) {
                    if (pts.size < 4) continue
                    pts = clip(simplify(pts, 1.5), lim)
                    if (pts.size >= 3) feats.add(Feature(kind, pts, null))
                } else {
                    feats.add(Feature(kind, simplify(pts, 1.0), tags.optString("name").ifBlank { null }))
                }
            } else if (el.getString("type") == "relation") {
                val members = el.optJSONArray("members") ?: continue
                val outers = (0 until members.length()).map { members.getJSONObject(it) }
                    .filter { it.optString("role") == "outer" && it.has("geometry") }
                    .map { geom(it.getJSONArray("geometry")) }
                for (ring in stitch(outers)) {
                    val pts = clip(simplify(ring, 2.0), lim)
                    if (pts.size >= 3) feats.add(Feature(kind, pts, null))
                }
            }
        }
        feats.sortBy { ORDER[it.kind] ?: 9 }

        val bos = ByteArrayOutputStream()
        val hdr = ByteBuffer.allocate(14).order(ByteOrder.LITTLE_ENDIAN)
        hdr.put('G'.code.toByte()).put('M'.code.toByte()).put(1).put(0)
        hdr.putInt((lat0 * 1e7).roundToInt()).putInt((lon0 * 1e7).roundToInt()).putShort(0)
        bos.write(hdr.array())
        var count = 0
        for (f in feats) {
            val area = f.kind == "water" || f.kind == "park" || f.kind == "beach"
            val q = f.pts.map { intArrayOf(q16(it[0] * 2), q16(it[1] * 2)) }
            var start = 0
            while (start < q.size) {
                val end = if (area) q.size else minOf(q.size, start + 4001)
                val chunk = q.subList(start, end)
                val b = ByteBuffer.allocate(4 + chunk.size * 4).order(ByteOrder.LITTLE_ENDIAN)
                b.put(KIND.getValue(f.kind).toByte()).put(0).putShort(chunk.size.toShort())
                chunk.forEach { b.putShort(it[0].toShort()).putShort(it[1].toShort()) }
                bos.write(b.array())
                count++
                if (area || end >= q.size) break
                start = end - 1
            }
        }
        val blob = bos.toByteArray()
        blob[12] = (count and 0xFF).toByte()
        blob[13] = ((count shr 8) and 0xFF).toByte()
        return Result(lat0, lon0, blob, feats.filter { it.name != null && it.kind != "path" }, count)
    }

    private fun q16(v: Double) = v.roundToInt().coerceIn(-32767, 32767)
}
