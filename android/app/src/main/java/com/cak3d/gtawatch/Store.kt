package com.cak3d.gtawatch

import android.content.Context
import android.content.SharedPreferences
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.update
import org.json.JSONArray
import org.json.JSONObject

data class Place(val name: String, val kind: String, val lat: Double, val lon: Double, val address: String = "")

/** GTA-style place types. key = id sent to the watch (icon + colour are drawn there too). */
data class PlaceKind(val key: String, val label: String, val color: Long)

val PLACE_KINDS = listOf(
    PlaceKind("safehouse", "Safe House", 0xFF5DA9E9),
    PlaceKind("mechanic", "Mechanic", 0xFF9CD08F),
    PlaceKind("parking", "Parked Car", 0xFFF0C850),
    PlaceKind("carwash", "Pay 'n' Spray", 0xFFE06C4C),
    PlaceKind("food", "Cluckin' Bell", 0xFFF0A040),
    PlaceKind("gas", "Gas Station", 0xFFE8903A),
    PlaceKind("bank", "Maze Bank", 0xFF72CC72),
    PlaceKind("hospital", "Hospital", 0xFFE05A5A),
    PlaceKind("police", "Police", 0xFF4C8CC9),
    PlaceKind("work", "Work", 0xFFC9A0F0),
    PlaceKind("shop", "Shop", 0xFFE0E0E0),
    PlaceKind("custom", "Custom", 0xFFFFFFFF),
)

fun kindOf(key: String) = PLACE_KINDS.firstOrNull { it.key == key } ?: PLACE_KINDS.last()

data class Telemetry(
    val fw: String = "", val battery: Int = -1, val millivolts: Int = 0, val charging: Boolean = false,
    val usb: Boolean = false, val steps: Int = 0, val uptimeSec: Long = 0, val heap: Int = 0,
    val page: String = "", val mtu: Int = 0, val receivedAt: Long = 0,
)

enum class LinkState { OFF, SCANNING, CONNECTING, CONNECTED }

data class NavUi(
    val active: Boolean = false, val walking: Boolean = false, val maneuver: Int = 0,
    val distNext: Double = 0.0, val remain: Double = 0.0, val etaSec: Int = 0,
    val instruction: String = "", val street: String = "", val dest: String = "", val cost: String = "",
    val route: List<DoubleArray> = emptyList(),  // [lat, lon]
)

data class Fix(val lat: Double, val lon: Double, val heading: Float, val speed: Float, val accuracy: Float, val street: String, val area: String)

data class Settings(
    val h24: Boolean = false, val metric: Boolean = false, val screenTimeout: Int = 15, val brightness: Int = 200,
    val mpg: Double = 25.0, val gasPrice: Double = 3.29, val forwardNotifications: Boolean = true,
    val walkDefault: Boolean = false,
)

/** Process-wide state shared by the service and the UI. */
object Store {
    private lateinit var prefs: SharedPreferences

    private val _places = MutableStateFlow<List<Place>>(emptyList())
    val places: StateFlow<List<Place>> = _places.asStateFlow()
    private val _settings = MutableStateFlow(Settings())
    val settings: StateFlow<Settings> = _settings.asStateFlow()

    val link = MutableStateFlow(LinkState.OFF)
    val linkDetail = MutableStateFlow("")
    val rssi = MutableStateFlow(0)
    val telemetry = MutableStateFlow(Telemetry())
    val fix = MutableStateFlow<Fix?>(null)
    val nav = MutableStateFlow(NavUi())
    val mapStatus = MutableStateFlow("")
    val log = MutableStateFlow<List<String>>(emptyList())
    /** Bumped when the watch asks to open the big map. */
    val openMapRequest = MutableStateFlow(0L)
    val serviceRunning = MutableStateFlow(false)

    var deviceAddress: String?
        get() = prefs.getString("device", null)
        set(v) = prefs.edit().putString("device", v).apply()
    var deviceName: String?
        get() = prefs.getString("deviceName", null)
        set(v) = prefs.edit().putString("deviceName", v).apply()

    fun init(ctx: Context) {
        prefs = ctx.getSharedPreferences("gtawatch", Context.MODE_PRIVATE)
        _places.value = parsePlaces(prefs.getString("places", null))
        _settings.value = parseSettings(prefs.getString("settings", null))
    }

    fun addLog(line: String) {
        val t = java.text.SimpleDateFormat("HH:mm:ss", java.util.Locale.US).format(java.util.Date())
        log.update { (listOf("$t  $line") + it).take(200) }
    }

    // ---- places
    fun setPlaces(list: List<Place>) {
        _places.value = list
        prefs.edit().putString("places", placesJson(list).toString()).apply()
    }
    fun addPlace(p: Place) = setPlaces(_places.value + p)
    fun removePlace(p: Place) = setPlaces(_places.value - p)
    fun replacePlace(old: Place, new: Place) = setPlaces(_places.value.map { if (it == old) new else it })

    fun placesJson(list: List<Place> = _places.value): JSONArray = JSONArray().apply {
        list.forEach { put(JSONObject().put("n", it.name).put("k", it.kind).put("lat", it.lat).put("lon", it.lon).put("a", it.address)) }
    }

    private fun parsePlaces(s: String?): List<Place> = try {
        val a = JSONArray(s ?: "[]")
        (0 until a.length()).map {
            val o = a.getJSONObject(it)
            Place(o.getString("n"), o.optString("k", "custom"), o.getDouble("lat"), o.getDouble("lon"), o.optString("a"))
        }
    } catch (e: Exception) {
        emptyList()
    }

    // ---- settings
    fun updateSettings(f: (Settings) -> Settings) {
        val s = f(_settings.value)
        _settings.value = s
        prefs.edit().putString("settings", JSONObject()
            .put("h24", s.h24).put("metric", s.metric).put("timeout", s.screenTimeout).put("bright", s.brightness)
            .put("mpg", s.mpg).put("gas", s.gasPrice).put("fwd", s.forwardNotifications).put("walk", s.walkDefault)
            .toString()).apply()
    }

    private fun parseSettings(s: String?): Settings = try {
        val o = JSONObject(s ?: "{}")
        Settings(
            o.optBoolean("h24", false), o.optBoolean("metric", false), o.optInt("timeout", 15), o.optInt("bright", 200),
            o.optDouble("mpg", 25.0), o.optDouble("gas", 3.29), o.optBoolean("fwd", true), o.optBoolean("walk", false),
        )
    } catch (e: Exception) {
        Settings()
    }
}
