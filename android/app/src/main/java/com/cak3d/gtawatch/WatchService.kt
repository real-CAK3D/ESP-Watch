package com.cak3d.gtawatch

import android.Manifest
import android.annotation.SuppressLint
import android.app.Notification
import android.app.NotificationManager
import android.app.PendingIntent
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.content.pm.PackageManager
import android.content.pm.ServiceInfo
import android.hardware.Sensor
import android.hardware.SensorEvent
import android.hardware.SensorEventListener
import android.hardware.SensorManager
import android.location.Location
import android.media.AudioAttributes
import android.media.Ringtone
import android.media.RingtoneManager
import android.os.BatteryManager
import android.os.Build
import android.os.Looper
import android.os.VibrationEffect
import android.os.Vibrator
import android.provider.Settings as AndroidSettings
import androidx.core.app.NotificationCompat
import androidx.core.app.ServiceCompat
import androidx.core.content.ContextCompat
import androidx.lifecycle.LifecycleService
import androidx.lifecycle.lifecycleScope
import com.google.android.gms.location.LocationCallback
import com.google.android.gms.location.LocationRequest
import com.google.android.gms.location.LocationResult
import com.google.android.gms.location.LocationServices
import com.google.android.gms.location.Priority
import android.speech.tts.TextToSpeech
import kotlinx.coroutines.CompletableDeferred
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.channels.Channel
import kotlinx.coroutines.withTimeout
import kotlinx.coroutines.Job
import kotlinx.coroutines.delay
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import org.json.JSONArray
import org.json.JSONObject
import java.util.TimeZone
import kotlin.math.abs

/**
 * Foreground service: owns the watch link and the phone sensors, and keeps the watch fed with
 * GPS, map data, navigation, weather, time, notifications and phone battery.
 */
class WatchService : LifecycleService(), BleLink.Listener, SensorEventListener {

    companion object {
        @Volatile var instance: WatchService? = null
        private const val NOTIF_ID = 7

        fun start(ctx: Context) {
            ContextCompat.startForegroundService(ctx, Intent(ctx, WatchService::class.java))
        }
        fun stop(ctx: Context) {
            ctx.stopService(Intent(ctx, WatchService::class.java))
        }
        const val ACTION_STOP_RING = "com.cak3d.gtawatch.STOP_RING"
        const val ACTION_STOP_NAV = "com.cak3d.gtawatch.STOP_NAV"
    }

    lateinit var link: BleLink
    private val fused by lazy { LocationServices.getFusedLocationProviderClient(this) }
    private lateinit var sensors: SensorManager

    private var map: MapPacker.Result? = null
    private var mapJob: Job? = null
    private var navigator: Navigator? = null
    private var routing = false
    private var lastLoc: Location? = null
    private var compass = 0f
    private var lastGpsSent = 0L
    private var lastHeadingSent = 0f
    private var area = ""
    private var lastAreaLookup: Location? = null
    private var weatherJson: JSONObject? = null
    private var weatherAt = 0L
    private var phoneBattery = -1
    private var phoneCharging = false
    private var ringtone: Ringtone? = null
    private var terrainBlob: ByteArray? = null
    private val otaStatus = Channel<JSONObject>(Channel.CONFLATED)
    private var tts: TextToSpeech? = null
    private var ttsReady = false
    private var lastSpoken = ""

    override fun onCreate() {
        super.onCreate()
        instance = this
        Store.serviceRunning.value = true
        link = BleLink(this, this)
        sensors = getSystemService(SENSOR_SERVICE) as SensorManager
        goForeground()
        startLocation()
        sensors.getDefaultSensor(Sensor.TYPE_ROTATION_VECTOR)?.let {
            sensors.registerListener(this, it, SensorManager.SENSOR_DELAY_UI)
        }
        registerReceiver(batteryReceiver, IntentFilter(Intent.ACTION_BATTERY_CHANGED))
        registerReceiver(tzReceiver, IntentFilter(Intent.ACTION_TIMEZONE_CHANGED))
        Store.deviceAddress?.let { link.connect(it) }
        lifecycleScope.launch { periodic() }
        tts = TextToSpeech(this) { st -> ttsReady = st == TextToSpeech.SUCCESS }
        if (Store.settings.value.autoUpdateCheck) checkForUpdates(notify = true)
    }

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        super.onStartCommand(intent, flags, startId)
        when (intent?.action) {
            ACTION_STOP_RING -> stopRing()
            ACTION_STOP_NAV -> stopNav()
        }
        return START_STICKY
    }

    override fun onDestroy() {
        instance = null
        Store.serviceRunning.value = false
        link.disconnect()
        fused.removeLocationUpdates(locationCb)
        sensors.unregisterListener(this)
        unregisterReceiver(batteryReceiver)
        unregisterReceiver(tzReceiver)
        stopRing()
        tts?.shutdown()
        super.onDestroy()
    }

    // ------------------------------------------------------------------ foreground
    /**
     * Android 14+ refuses a *location* foreground service started while the app is in the background
     * (e.g. when the system restarts this sticky service after killing the app). Fall back to a
     * connected-device-only service instead of crashing; location is re-added once the app is opened.
     */
    private fun goForeground() {
        val base = ServiceInfo.FOREGROUND_SERVICE_TYPE_CONNECTED_DEVICE
        val withLocation = if (hasLocation()) base or ServiceInfo.FOREGROUND_SERVICE_TYPE_LOCATION else base
        for (types in listOf(withLocation, base).distinct()) {
            try {
                ServiceCompat.startForeground(this, NOTIF_ID, buildNotification("Starting…"), types)
                return
            } catch (e: Exception) {
                Store.addLog("foreground start refused (${e.javaClass.simpleName}), retrying")
            }
        }
    }

    private fun buildNotification(text: String): Notification {
        val open = PendingIntent.getActivity(this, 0, Intent(this, MainActivity::class.java), PendingIntent.FLAG_IMMUTABLE)
        val b = NotificationCompat.Builder(this, GtaWatchApp.CH_LINK)
            .setSmallIcon(android.R.drawable.ic_menu_mylocation)
            .setContentTitle("GTA-Watch")
            .setContentText(text)
            .setContentIntent(open)
            .setOngoing(true)
            .setSilent(true)
        if (navigator != null) {
            val stop = PendingIntent.getService(this, 2, Intent(this, WatchService::class.java).setAction(ACTION_STOP_NAV), PendingIntent.FLAG_IMMUTABLE)
            b.addAction(0, "Stop route", stop)
        }
        return b.build()
    }

    private fun updateNotification() {
        val nav = Store.nav.value
        val text = when {
            nav.active -> "${fmtDist(nav.distNext)} · ${nav.instruction}"
            Store.link.value == LinkState.CONNECTED -> "Watch connected"
            Store.deviceAddress == null -> "No watch paired"
            else -> "Looking for your watch…"
        }
        getSystemService(NotificationManager::class.java).notify(NOTIF_ID, buildNotification(text))
    }

    // ------------------------------------------------------------------ link events
    override fun onReady() {
        updateNotification()
        syncAll()
    }

    override fun onDisconnected() = updateNotification()

    override fun onMessage(type: Int, payload: ByteArray) {
        val text = String(payload)
        when (type) {
            Protocol.TELEMETRY -> try {
                val o = JSONObject(text)
                Store.telemetry.value = Telemetry(
                    o.optString("fw"), o.optInt("bat", -1), o.optInt("mv"), o.optBoolean("chg"), o.optBoolean("usb"),
                    o.optInt("steps"), o.optLong("up"), o.optInt("heap"), o.optString("page"), o.optInt("mtu"),
                    System.currentTimeMillis())
                recordBattery(o.optInt("bat", -1), o.optBoolean("chg"))
            } catch (_: Exception) {
            }
            Protocol.EVENT -> try {
                handleEvent(JSONObject(text))
            } catch (_: Exception) {
            }
            Protocol.LOG -> Store.addLog("watch: $text")
            Protocol.OTA_STATUS -> try {
                otaStatus.trySend(JSONObject(text))
            } catch (_: Exception) {
            }
        }
    }

    private fun handleEvent(o: JSONObject) {
        val e = o.optString("e")
        Store.addLog("watch event: $e")
        when (e) {
            "hello" -> syncAll()
            "open_map" -> openBigMap()
            "navigate" -> Store.places.value.getOrNull(o.optInt("i", -1))?.let { startNav(it.name, it.lat, it.lon, Store.settings.value.walkDefault) }
            "save_parking" -> saveParking()
            "nav_stop" -> stopNav()
            "find_phone" -> ring()
            "battery_low" -> if (Store.settings.value.lowBatteryAlerts) alert(13, "Watch battery low",
                "Your GTA-Watch is at ${o.optInt("i", 0)}%. Power saver is on below 15%.")
            "charged" -> if (Store.settings.value.chargeAlerts) alert(13, "Watch fully charged", "Your GTA-Watch is ready to go.")
        }
    }

    /** Push everything the watch needs after (re)connecting. */
    fun syncAll() {
        sendTime()
        sendSettings()
        sendPlaces()
        sendPhone()
        weatherJson?.let { link.send(Protocol.WEATHER, it.toString().toByteArray()) }
        map?.let { link.send(Protocol.MAP, it.blob) }
        terrainBlob?.let { link.send(Protocol.TERRAIN, it) }
        lastLoc?.let { sendGps(it, force = true) }
        navigator?.let {
            link.send(Protocol.ROUTE, it.routePayload())
            lastLoc?.let { l -> sendNav(l) }
        }
        if (weatherJson == null || System.currentTimeMillis() - weatherAt > 30 * 60_000) refreshWeather()
    }

    fun sendTime() {
        val now = System.currentTimeMillis()
        link.send(Protocol.TIME, JSONObject().put("t", now / 1000).put("tz", TimeZone.getDefault().getOffset(now) / 1000).toString().toByteArray())
    }

    fun sendSettings() {
        val s = Store.settings.value
        link.send(Protocol.SETTINGS, JSONObject().put("h24", s.h24).put("metric", s.metric)
            .put("timeout", s.screenTimeout).put("bright", s.brightness).put("zoom", s.radarZoom)
            .put("terrain", s.watchTerrain).put("raise", s.raiseToWake).put("face", s.watchFace)
            .put("keepOnNav", s.keepMapOn).toString().toByteArray())
    }

    fun sendPlaces() {
        val arr = JSONArray()
        Store.places.value.forEach { arr.put(JSONObject().put("n", it.name).put("k", it.kind).put("lat", it.lat).put("lon", it.lon)) }
        link.send(Protocol.PLACES, arr.toString().toByteArray())
    }

    private fun sendPhone() {
        if (phoneBattery >= 0) link.send(Protocol.PHONE, JSONObject().put("bat", phoneBattery).put("chg", phoneCharging).toString().toByteArray())
    }

    fun forwardNotification(app: String, title: String, body: String, id: Int) {
        if (!Store.settings.value.forwardNotifications) return
        link.send(Protocol.NOTIFY, JSONObject().put("id", id).put("app", app).put("title", title.take(46))
            .put("body", body.take(158)).toString().toByteArray())
    }

    // ------------------------------------------------------------------ location + heading
    private fun hasLocation() = ContextCompat.checkSelfPermission(this, Manifest.permission.ACCESS_FINE_LOCATION) == PackageManager.PERMISSION_GRANTED

    private val locationCb = object : LocationCallback() {
        override fun onLocationResult(r: LocationResult) {
            r.lastLocation?.let { onLocation(it) }
        }
    }

    @SuppressLint("MissingPermission")
    fun startLocation() {
        if (!hasLocation()) return
        val req = LocationRequest.Builder(Priority.PRIORITY_HIGH_ACCURACY, 1000).setMinUpdateIntervalMillis(500).build()
        fused.requestLocationUpdates(req, locationCb, Looper.getMainLooper())
        goForeground()
    }

    private fun heading(l: Location): Float =
        if (l.hasBearing() && l.hasSpeed() && l.speed > 1.8f) l.bearing else compass

    private fun onLocation(l: Location) {
        lastLoc = l
        val m = map
        val street = m?.streetAt(l.latitude, l.longitude) ?: ""
        Store.fix.value = Fix(l.latitude, l.longitude, heading(l), if (l.hasSpeed()) l.speed else 0f, l.accuracy, street, area)
        sendGps(l)
        if (navigator != null) sendNav(l)
        maybeRefreshMap(l)
        maybeLookupArea(l)
    }

    private fun sendGps(l: Location, force: Boolean = false) {
        val now = System.currentTimeMillis()
        if (!force && now - lastGpsSent < 450) return
        lastGpsSent = now
        val h = heading(l)
        lastHeadingSent = h
        val f = Store.fix.value
        link.send(Protocol.GPS, JSONObject().put("lat", l.latitude).put("lon", l.longitude)
            .put("hdg", Math.round(h * 10) / 10.0).put("spd", if (l.hasSpeed()) Math.round(l.speed * 10) / 10.0 else 0.0)
            .put("acc", Math.round(l.accuracy)).put("street", f?.street ?: "").put("area", area).toString().toByteArray())
    }

    private val rot = FloatArray(9)
    private val ori = FloatArray(3)
    override fun onSensorChanged(e: SensorEvent) {
        SensorManager.getRotationMatrixFromVector(rot, e.values)
        SensorManager.getOrientation(rot, ori)
        val az = ((Math.toDegrees(ori[0].toDouble()) + 360) % 360).toFloat()
        val d = ((az - compass + 540) % 360) - 180
        compass = (compass + d * 0.25f + 360) % 360
        // while standing still the compass turns the minimap, like the GTA camera
        val l = lastLoc ?: return
        val moving = l.hasSpeed() && l.speed > 1.8f
        if (!moving && abs(((compass - lastHeadingSent + 540) % 360) - 180) > 8 && System.currentTimeMillis() - lastGpsSent > 250) {
            Store.fix.value = Store.fix.value?.copy(heading = compass)
            sendGps(l, force = true)
        }
    }

    override fun onAccuracyChanged(s: Sensor?, a: Int) {}

    // ------------------------------------------------------------------ map tiles for the watch
    private fun maybeRefreshMap(l: Location) {
        if (mapJob?.isActive == true) return
        val m = map
        if (m != null) {
            val dx = (l.longitude - m.lon0) * MapPacker.mLon(m.lat0)
            val dy = (l.latitude - m.lat0) * MapPacker.M_LAT
            if (dx * dx + dy * dy < 700.0 * 700.0) return
        }
        refreshMap(l.latitude, l.longitude)
    }

    fun refreshMap(lat: Double, lon: Double) {
        mapJob = lifecycleScope.launch {
            Store.mapStatus.value = "Downloading map…"
            try {
                val r = withContext(Dispatchers.IO) { MapPacker.build(lat, lon, java.io.File(cacheDir, "osm")) }
                map = r
                link.send(Protocol.MAP, r.blob)
                Store.mapStatus.value = "Map: ${r.featureCount} roads & areas (${r.blob.size / 1024} KB)"
                Store.addLog("map sent: ${r.blob.size} bytes")
                try {
                    val t = withContext(Dispatchers.IO) { Terrain.build(r.lat0, r.lon0, java.io.File(cacheDir, "dem")) }
                    terrainBlob = t
                    link.send(Protocol.TERRAIN, t)
                    Store.mapStatus.value += " · terrain"
                } catch (e: Exception) {
                    Store.addLog("terrain failed: ${e.message}")
                }
            } catch (e: Exception) {
                Store.mapStatus.value = "Map download failed: ${e.message}"
                Store.addLog("map failed: ${e.message}")
                delay(20_000)
            }
        }
    }

    private fun maybeLookupArea(l: Location) {
        val prev = lastAreaLookup
        if (prev != null && prev.distanceTo(l) < 400) return
        lastAreaLookup = l
        lifecycleScope.launch {
            try {
                area = withContext(Dispatchers.IO) { Net.reverseArea(l.latitude, l.longitude) }
            } catch (_: Exception) {
            }
        }
    }

    // ------------------------------------------------------------------ navigation
    fun startNav(name: String, lat: Double, lon: Double, walking: Boolean) {
        val from = lastLoc ?: run {
            Store.addLog("no GPS fix yet, can't route")
            return
        }
        if (routing) return
        routing = true
        lifecycleScope.launch {
            try {
                val n = withContext(Dispatchers.IO) { Navigator.route(from.latitude, from.longitude, lat, lon, walking, name) }
                navigator = n
                link.send(Protocol.ROUTE, n.routePayload())
                sendNav(from)
                Store.addLog("route to $name: ${fmtDist(n.total)}")
            } catch (e: Exception) {
                Store.addLog("routing failed: ${e.message}")
            } finally {
                routing = false
                updateNotification()
            }
        }
    }

    fun stopNav() {
        navigator = null
        Store.nav.value = NavUi()
        link.send(Protocol.NAV, """{"active":false}""".toByteArray())
        updateNotification()
    }

    private var lastNavNotif = 0L
    private fun sendNav(l: Location) {
        val n = navigator ?: return
        val u = n.update(l.latitude, l.longitude, Store.settings.value)
        Store.nav.value = u.ui
        link.send(Protocol.NAV, u.json.toString().toByteArray())
        speakGuidance(u.ui)
        if (System.currentTimeMillis() - lastNavNotif > 5000) {
            lastNavNotif = System.currentTimeMillis()
            updateNotification()
        }
        if (u.arrived) {
            lifecycleScope.launch {
                delay(15_000)
                if (navigator === n) stopNav()
            }
        } else if (u.offRoute && !routing) {
            Store.addLog("off route, recalculating")
            startNav(n.dest, n.destLat, n.destLon, n.walking)
        }
    }

    // ------------------------------------------------------------------ watch requests
    private fun openBigMap() {
        Store.openMapRequest.value = System.currentTimeMillis()
        val i = Intent(this, MainActivity::class.java).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK or Intent.FLAG_ACTIVITY_SINGLE_TOP)
            .putExtra("open", "radar")
        if (AndroidSettings.canDrawOverlays(this)) {
            startActivity(i)
        } else {
            // Android blocks background activity starts; offer a one-tap notification instead.
            val pi = PendingIntent.getActivity(this, 3, i, PendingIntent.FLAG_IMMUTABLE or PendingIntent.FLAG_UPDATE_CURRENT)
            val n = NotificationCompat.Builder(this, GtaWatchApp.CH_ALERT)
                .setSmallIcon(android.R.drawable.ic_dialog_map)
                .setContentTitle("Open the big map")
                .setContentText("Tap to view the map from your watch")
                .setPriority(NotificationCompat.PRIORITY_HIGH)
                .setContentIntent(pi).setAutoCancel(true).setTimeoutAfter(30_000).build()
            getSystemService(NotificationManager::class.java).notify(11, n)
        }
    }

    private fun saveParking() {
        val l = lastLoc ?: return
        val old = Store.places.value.filter { it.kind == "parking" }
        Store.setPlaces(Store.places.value - old.toSet() + Place("Parked Car", "parking", l.latitude, l.longitude, "Saved from watch"))
        sendPlaces()
    }

    private fun ring() {
        stopRing()
        val uri = RingtoneManager.getDefaultUri(RingtoneManager.TYPE_ALARM) ?: RingtoneManager.getDefaultUri(RingtoneManager.TYPE_RINGTONE)
        ringtone = RingtoneManager.getRingtone(this, uri)?.apply {
            audioAttributes = AudioAttributes.Builder().setUsage(AudioAttributes.USAGE_ALARM).build()
            play()
        }
        val vib = getSystemService(Vibrator::class.java)
        vib?.vibrate(VibrationEffect.createWaveform(longArrayOf(0, 600, 400), 0))
        val stop = PendingIntent.getService(this, 4, Intent(this, WatchService::class.java).setAction(ACTION_STOP_RING), PendingIntent.FLAG_IMMUTABLE)
        val n = NotificationCompat.Builder(this, GtaWatchApp.CH_ALERT)
            .setSmallIcon(android.R.drawable.ic_lock_idle_alarm).setContentTitle("Your watch is looking for this phone")
            .setPriority(NotificationCompat.PRIORITY_MAX).addAction(0, "Found it", stop).setContentIntent(stop).setOngoing(true).build()
        getSystemService(NotificationManager::class.java).notify(12, n)
        lifecycleScope.launch {
            delay(30_000)
            stopRing()
        }
    }

    private fun stopRing() {
        ringtone?.stop()
        ringtone = null
        getSystemService(Vibrator::class.java)?.cancel()
        getSystemService(NotificationManager::class.java).cancel(12)
    }

    // ------------------------------------------------------------------ alerts, battery history, voice
    private fun alert(id: Int, title: String, text: String) {
        val n = NotificationCompat.Builder(this, GtaWatchApp.CH_ALERT)
            .setSmallIcon(android.R.drawable.ic_lock_idle_charging).setContentTitle(title).setContentText(text)
            .setPriority(NotificationCompat.PRIORITY_HIGH).setAutoCancel(true).build()
        getSystemService(NotificationManager::class.java).notify(id, n)
    }

    private fun recordBattery(pct: Int, charging: Boolean) {
        if (pct < 0) return
        val now = System.currentTimeMillis()
        val h = Store.batteryHistory.value
        val last = h.lastOrNull()
        if (last != null && last.second == pct && last.third == charging && now - last.first < 10 * 60_000) return
        Store.batteryHistory.value = (h + Triple(now, pct, charging)).filter { now - it.first < 24 * 3600_000L }
    }

    private fun speakGuidance(n: NavUi) {
        if (!Store.settings.value.voice || !ttsReady) return
        // GTA-style short callouts at two distances before each turn, then on arrival
        val far = if (n.walking) 60.0 else 400.0
        val near = if (n.walking) 15.0 else 80.0
        val key = when {
            n.maneuver == 8 && n.distNext < near -> "arrive"
            n.distNext < near -> "near:${n.instruction}"
            n.distNext < far -> "far:${n.instruction}"
            else -> return
        }
        if (key == lastSpoken) return
        lastSpoken = key
        val text = when {
            key == "arrive" -> "You have arrived."
            key.startsWith("near") -> n.instruction
            else -> "In ${fmtDist(n.distNext)}, ${n.instruction}"
        }
        tts?.speak(text, TextToSpeech.QUEUE_FLUSH, null, "nav")
    }

    // ------------------------------------------------------------------ updates (GitHub Releases)
    fun checkForUpdates(notify: Boolean) {
        lifecycleScope.launch {
            try {
                val r = withContext(Dispatchers.IO) { Updater.latest() }
                val appNew = Updater.newer(r.version, Updater.appVersion(this@WatchService))
                val fw = Store.telemetry.value.fw
                val watchNew = fw.isNotEmpty() && Updater.newer(r.version, fw)
                Store.update.value = r
                Store.updateStatus.value = when {
                    appNew || watchNew -> "Version ${r.version} is available"
                    else -> "Up to date (${r.version})"
                }
                if (notify && (appNew || watchNew)) alert(14, "GTA-Watch ${r.version} is available", "Open the app's Settings to update.")
            } catch (e: Exception) {
                Store.updateStatus.value = "Update check failed: ${e.message}"
            }
        }
    }

    /** Firmware update over BLE: 8 KB blocks, each ACKed by the watch with its write offset. */
    fun updateWatch(image: ByteArray, version: String) {
        if (Store.otaProgress.value >= 0) return
        lifecycleScope.launch {
            Store.otaProgress.value = 0
            try {
                while (otaStatus.tryReceive().isSuccess) Unit
                link.send(Protocol.OTA_BEGIN, JSONObject().put("size", image.size).put("ver", version).toString().toByteArray())
                var st = withTimeout(90_000) { otaStatus.receive() }
                if (st.optString("st") != "ready") throw RuntimeException(st.optString("err", "watch refused"))
                var off = 0
                var retries = 0
                while (off < image.size) {
                    val n = minOf(8192, image.size - off)
                    val pkt = java.nio.ByteBuffer.allocate(4 + n).order(java.nio.ByteOrder.LITTLE_ENDIAN).putInt(off).put(image, off, n).array()
                    link.send(Protocol.OTA_DATA, pkt)
                    st = try {
                        withTimeout(20_000) { otaStatus.receive() }
                    } catch (e: Exception) {
                        if (++retries > 5) throw RuntimeException("watch stopped answering")
                        continue
                    }
                    if (st.optString("st") == "error") throw RuntimeException(st.optString("err"))
                    off = st.optInt("off", off)
                    Store.otaProgress.value = off * 100 / image.size
                }
                link.send(Protocol.OTA_END, ByteArray(0))
                st = withTimeout(60_000) { otaStatus.receive() }
                if (st.optString("st") != "done") throw RuntimeException(st.optString("err", "verify failed"))
                Store.updateStatus.value = "Watch updated to $version — restarting"
            } catch (e: Exception) {
                Store.updateStatus.value = "Watch update failed: ${e.message}"
            } finally {
                Store.otaProgress.value = -1
            }
        }
    }

    // ------------------------------------------------------------------ periodic + receivers
    private suspend fun periodic() {
        var tick = 0
        while (lifecycleScope.isActive) {
            delay(60_000)
            tick++
            if (System.currentTimeMillis() - weatherAt > 30 * 60_000) refreshWeather()
            if (tick % 360 == 0) sendTime()
            if (tick % 1440 == 0 && Store.settings.value.autoUpdateCheck) checkForUpdates(notify = true)
        }
    }

    fun refreshWeather() {
        val l = lastLoc ?: return
        lifecycleScope.launch {
            try {
                val city = area.ifBlank { withContext(Dispatchers.IO) { Net.reverseArea(l.latitude, l.longitude) } }
                val w = withContext(Dispatchers.IO) { Net.weather(l.latitude, l.longitude, city, !Store.settings.value.metric) }
                weatherJson = w
                weatherAt = System.currentTimeMillis()
                link.send(Protocol.WEATHER, w.toString().toByteArray())
            } catch (e: Exception) {
                Store.addLog("weather failed: ${e.message}")
            }
        }
    }

    private val batteryReceiver = object : BroadcastReceiver() {
        override fun onReceive(c: Context, i: Intent) {
            val lvl = i.getIntExtra(BatteryManager.EXTRA_LEVEL, -1)
            val scale = i.getIntExtra(BatteryManager.EXTRA_SCALE, 100)
            val pct = if (lvl >= 0) lvl * 100 / scale else -1
            val chg = i.getIntExtra(BatteryManager.EXTRA_STATUS, -1).let { it == BatteryManager.BATTERY_STATUS_CHARGING || it == BatteryManager.BATTERY_STATUS_FULL }
            if (pct != phoneBattery || chg != phoneCharging) {
                phoneBattery = pct
                phoneCharging = chg
                sendPhone()
            }
        }
    }

    private val tzReceiver = object : BroadcastReceiver() {
        override fun onReceive(c: Context, i: Intent) = sendTime()
    }
}

fun fmtDist(m: Double): String {
    return if (Store.settings.value.metric) {
        if (m < 1000) "${(m / 10).toInt() * 10} m" else "%.1f km".format(m / 1000)
    } else {
        val ft = m * 3.28084
        if (ft < 1000) "${(ft / 10).toInt() * 10} ft" else "%.1f mi".format(m / 1609.34)
    }
}
