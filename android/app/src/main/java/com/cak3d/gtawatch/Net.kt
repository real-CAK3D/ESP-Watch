package com.cak3d.gtawatch

import org.json.JSONArray
import org.json.JSONObject
import java.net.HttpURLConnection
import java.net.URL
import java.net.URLEncoder

/** Free, key-less web services: Overpass (map), OSRM (routes), Nominatim (search), Open-Meteo (weather). */
object Net {
    private const val UA = "GTA-Watch/1.0 (personal smartwatch companion)"

    fun get(url: String, timeoutMs: Int = 30_000): String = request(url, null, timeoutMs)

    fun post(url: String, form: String, timeoutMs: Int = 60_000): String = request(url, form, timeoutMs)

    private fun request(url: String, form: String?, timeoutMs: Int): String {
        val c = URL(url).openConnection() as HttpURLConnection
        c.connectTimeout = 15_000
        c.readTimeout = timeoutMs
        c.setRequestProperty("User-Agent", UA)
        if (form != null) {
            c.requestMethod = "POST"
            c.doOutput = true
            c.setRequestProperty("Content-Type", "application/x-www-form-urlencoded")
            c.outputStream.use { it.write(form.toByteArray()) }
        }
        try {
            if (c.responseCode !in 200..299) throw java.io.IOException("HTTP ${c.responseCode} from ${URL(url).host}")
            return c.inputStream.bufferedReader().use { it.readText() }
        } finally {
            c.disconnect()
        }
    }

    fun enc(s: String): String = URLEncoder.encode(s, "UTF-8")

    data class SearchHit(val name: String, val address: String, val lat: Double, val lon: Double)

    /** Address / place search, biased to the area around the user. */
    fun search(query: String, nearLat: Double?, nearLon: Double?): List<SearchHit> {
        var url = "https://nominatim.openstreetmap.org/search?format=jsonv2&limit=8&addressdetails=0&q=${enc(query)}"
        if (nearLat != null && nearLon != null) {
            val d = 0.6
            url += "&viewbox=${nearLon - d},${nearLat + d},${nearLon + d},${nearLat - d}"
        }
        val a = JSONArray(get(url))
        return (0 until a.length()).map {
            val o = a.getJSONObject(it)
            val display = o.getString("display_name")
            SearchHit(o.optString("name").ifBlank { display.substringBefore(",") }, display, o.getDouble("lat"), o.getDouble("lon"))
        }
    }

    /** Town / neighbourhood name for the GTA-style area label. */
    fun reverseArea(lat: Double, lon: Double): String {
        val o = JSONObject(get("https://nominatim.openstreetmap.org/reverse?format=jsonv2&zoom=14&lat=$lat&lon=$lon"))
        val a = o.optJSONObject("address") ?: return ""
        for (k in listOf("neighbourhood", "suburb", "village", "town", "city", "hamlet", "county")) {
            val v = a.optString(k)
            if (v.isNotBlank()) return v
        }
        return ""
    }

    data class Weather(val json: JSONObject)

    fun weather(lat: Double, lon: Double, city: String, imperial: Boolean): JSONObject {
        val units = if (imperial) "&temperature_unit=fahrenheit&wind_speed_unit=mph" else ""
        val o = JSONObject(get(
            "https://api.open-meteo.com/v1/forecast?latitude=$lat&longitude=$lon" +
                "&current=temperature_2m,apparent_temperature,relative_humidity_2m,weather_code,wind_speed_10m" +
                "&hourly=temperature_2m,weather_code&daily=temperature_2m_max,temperature_2m_min" +
                "&timezone=auto&forecast_days=2$units"))
        val cur = o.getJSONObject("current")
        val daily = o.getJSONObject("daily")
        val hourly = o.getJSONObject("hourly")
        val times = hourly.getJSONArray("time")
        val nowHour = cur.getString("time").substring(0, 13)
        var start = 0
        for (i in 0 until times.length()) if (times.getString(i).startsWith(nowHour)) { start = i + 1; break }
        val hours = JSONArray()
        for (i in start until minOf(start + 5, times.length())) {
            hours.put(JSONArray()
                .put(times.getString(i).substring(11, 13).toInt())
                .put(Math.round(hourly.getJSONArray("temperature_2m").getDouble(i)))
                .put(hourly.getJSONArray("weather_code").getInt(i)))
        }
        return JSONObject()
            .put("city", city)
            .put("t", Math.round(cur.getDouble("temperature_2m")))
            .put("feels", Math.round(cur.getDouble("apparent_temperature")))
            .put("hum", cur.getInt("relative_humidity_2m"))
            .put("wind", Math.round(cur.getDouble("wind_speed_10m")))
            .put("code", cur.getInt("weather_code"))
            .put("hi", Math.round(daily.getJSONArray("temperature_2m_max").getDouble(0)))
            .put("lo", Math.round(daily.getJSONArray("temperature_2m_min").getDouble(0)))
            .put("hours", hours)
    }
}
