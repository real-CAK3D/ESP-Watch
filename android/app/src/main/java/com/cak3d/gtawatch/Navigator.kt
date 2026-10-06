package com.cak3d.gtawatch

import org.json.JSONObject
import java.nio.ByteBuffer
import java.nio.ByteOrder
import kotlin.math.hypot
import kotlin.math.roundToInt
import kotlin.math.sqrt

/** OSRM route + turn-by-turn progress tracking. Port of tools/phonesim.py Navigator. */
class Navigator private constructor(
    val walking: Boolean,
    val dest: String,
    val destLat: Double,
    val destLon: Double,
    route: JSONObject,
) {
    val coords: List<DoubleArray>  // [lat, lon]
    private val lat0: Double
    private val lon0: Double
    private val xy: List<DoubleArray>
    private val cum: DoubleArray
    val total: Double
    private val duration: Double
    private val steps: List<Pair<Double, JSONObject>>
    private var offRouteCount = 0

    init {
        val c = route.getJSONObject("geometry").getJSONArray("coordinates")
        coords = (0 until c.length()).map { val p = c.getJSONArray(it); doubleArrayOf(p.getDouble(1), p.getDouble(0)) }
        lat0 = coords[0][0]
        lon0 = coords[0][1]
        val ml = MapPacker.mLon(lat0)
        xy = coords.map { doubleArrayOf((it[1] - lon0) * ml, (it[0] - lat0) * MapPacker.M_LAT) }
        cum = DoubleArray(xy.size)
        for (i in 1 until xy.size) cum[i] = cum[i - 1] + hypot(xy[i][0] - xy[i - 1][0], xy[i][1] - xy[i - 1][1])
        total = cum.last()
        duration = route.getDouble("duration")
        val st = ArrayList<Pair<Double, JSONObject>>()
        val legs = route.getJSONArray("legs")
        for (l in 0 until legs.length()) {
            val s = legs.getJSONObject(l).getJSONArray("steps")
            for (i in 0 until s.length()) {
                val step = s.getJSONObject(i)
                val loc = step.getJSONObject("maneuver").getJSONArray("location")
                st.add(along((loc.getDouble(0) - lon0) * ml, (loc.getDouble(1) - lat0) * MapPacker.M_LAT).first to step)
            }
        }
        steps = st
    }

    companion object {
        fun route(fromLat: Double, fromLon: Double, toLat: Double, toLon: Double, walking: Boolean, dest: String): Navigator {
            val base = if (walking) "https://routing.openstreetmap.de/routed-foot" else "https://router.project-osrm.org"
            val r = JSONObject(Net.get("$base/route/v1/driving/$fromLon,$fromLat;$toLon,$toLat?overview=full&geometries=geojson&steps=true"))
            if (r.optString("code") != "Ok") throw RuntimeException("No route found")
            return Navigator(walking, dest, toLat, toLon, r.getJSONArray("routes").getJSONObject(0))
        }

        private val MOD = mapOf("straight" to 0, "slight left" to 1, "left" to 2, "sharp left" to 3,
            "slight right" to 4, "right" to 5, "sharp right" to 6, "uturn" to 7)

        fun maneuverCode(step: JSONObject): Int {
            val m = step.getJSONObject("maneuver")
            val t = m.optString("type")
            val mod = m.optString("modifier", "straight")
            return when {
                t == "arrive" -> 8
                t.startsWith("roundabout") || t.startsWith("rotary") || t.startsWith("exit r") -> 9
                t == "merge" -> 10
                t == "fork" -> if ("left" in mod) 11 else 12
                t == "depart" -> 13
                else -> MOD[mod] ?: 0
            }
        }

        fun instruction(step: JSONObject): String {
            val m = step.getJSONObject("maneuver")
            val t = m.optString("type")
            val mod = m.optString("modifier", "")
            val name = step.optString("name").ifBlank { "the road" }
            return when {
                t == "arrive" -> "Arrive at destination"
                t == "depart" -> "Head out on $name"
                t == "roundabout" || t == "rotary" -> "Roundabout, exit ${m.optInt("exit", 1)} to $name"
                mod == "uturn" -> "Make a U-turn"
                (t == "continue" || t == "new name") && (mod == "straight" || mod.isEmpty()) -> "Continue on $name"
                else -> "${mod.replaceFirstChar { it.uppercase() }} onto $name".trim()
            }
        }
    }

    private fun along(x: Double, y: Double): Pair<Double, Double> {
        var bestD = 1e18
        var bestA = 0.0
        for (i in 0 until xy.size - 1) {
            val a = xy[i]
            val b = xy[i + 1]
            val dx = b[0] - a[0]
            val dy = b[1] - a[1]
            val l2 = (dx * dx + dy * dy).takeIf { it > 1e-9 } ?: 1e-9
            val t = (((x - a[0]) * dx + (y - a[1]) * dy) / l2).coerceIn(0.0, 1.0)
            val d = hypot(a[0] + t * dx - x, a[1] + t * dy - y)
            if (d < bestD) {
                bestD = d
                bestA = cum[i] + t * sqrt(l2)
            }
        }
        return bestA to bestD
    }

    fun routePayload(): ByteArray {
        val b = ByteBuffer.allocate(10 + xy.size * 4).order(ByteOrder.LITTLE_ENDIAN)
        b.putInt((lat0 * 1e7).roundToInt()).putInt((lon0 * 1e7).roundToInt()).putShort(xy.size.toShort())
        xy.forEach {
            b.putShort((it[0] * 2).roundToInt().coerceIn(-32767, 32767).toShort())
            b.putShort((it[1] * 2).roundToInt().coerceIn(-32767, 32767).toShort())
        }
        return b.array()
    }

    class Update(val json: JSONObject, val ui: NavUi, val offRoute: Boolean, val arrived: Boolean)

    fun update(lat: Double, lon: Double, s: Settings): Update {
        val ml = MapPacker.mLon(lat0)
        val (prog, off) = along((lon - lon0) * ml, (lat - lat0) * MapPacker.M_LAT)
        offRouteCount = if (off > (if (walking) 35 else 50)) offRouteCount + 1 else 0
        val next = steps.firstOrNull { it.first > prog + 3 } ?: steps.last()
        val remain = (total - prog).coerceAtLeast(0.0)
        val dist = (next.first - prog).coerceAtLeast(0.0)
        val eta = (duration * remain / total.coerceAtLeast(1.0)).roundToInt()
        val cost = if (walking) "" else "$%.2f".format(remain / 1609.34 / s.mpg * s.gasPrice)
        val man = maneuverCode(next.second)
        val street = next.second.optString("name")
        val instr = instruction(next.second)
        val json = JSONObject().put("active", true).put("mode", if (walking) "walk" else "drive").put("man", man)
            .put("dist", (dist * 10).roundToInt() / 10.0).put("remain", remain.roundToInt()).put("total", total.roundToInt())
            .put("eta", eta).put("instr", instr).put("street", street).put("dest", dest).put("cost", cost)
        val ui = NavUi(true, walking, man, dist, remain, eta, instr, street, dest, cost, coords)
        return Update(json, ui, offRouteCount >= 3, remain < 20)
    }
}
