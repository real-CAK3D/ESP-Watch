package com.cak3d.gtawatch.ui

import android.content.ClipData
import android.content.ClipboardManager
import android.content.Context
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.IntentSenderRequest
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.LinearProgressIndicator
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateListOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.cak3d.gtawatch.BleLink
import com.cak3d.gtawatch.Companion
import com.cak3d.gtawatch.CrashLog
import com.cak3d.gtawatch.FoundWatch
import com.cak3d.gtawatch.LinkState
import com.cak3d.gtawatch.Protocol
import com.cak3d.gtawatch.Store
import com.cak3d.gtawatch.Updater
import com.cak3d.gtawatch.WatchService
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext

private fun pair(ctx: Context, address: String, name: String) {
    Store.deviceAddress = address
    Store.deviceName = name
    Companion.observe(ctx, address)
    WatchService.start(ctx)
    WatchService.instance?.link?.connect(address)
}

@Composable
fun WatchScreen() {
    val ctx = LocalContext.current
    val link by Store.link.collectAsState()
    val detail by Store.linkDetail.collectAsState()
    val rssi by Store.rssi.collectAsState()
    val t by Store.telemetry.collectAsState()
    val mapStatus by Store.mapStatus.collectAsState()
    val log by Store.log.collectAsState()
    val running by Store.serviceRunning.collectAsState()
    val history by Store.batteryHistory.collectAsState()
    var pairError by remember { mutableStateOf("") }
    var scanning by remember { mutableStateOf(false) }
    var crash by remember { mutableStateOf(CrashLog.read()) }
    val found = remember { mutableStateListOf<FoundWatch>() }
    val scanner = remember { BleLink(ctx, object : BleLink.Listener {
        override fun onReady() {}
        override fun onMessage(type: Int, payload: ByteArray) {}
        override fun onDisconnected() {}
    }) }

    // Android's companion pairing sheet (same mechanism Galaxy Wearable uses)
    val pairLauncher = rememberLauncherForActivityResult(ActivityResultContracts.StartIntentSenderForResult()) { r ->
        val dev = Companion.parseResult(r.data)
        if (dev != null) pair(ctx, dev.first, dev.second) else pairError = "No watch selected"
    }

    LaunchedEffect(scanning) {
        if (scanning) {
            found.clear()
            try {
                scanner.scan { w -> if (found.none { it.address == w.address }) found.add(w) }
            } catch (e: SecurityException) {
                Store.addLog("Bluetooth permission missing")
            }
            delay(15_000)
            scanner.stopScan()
            scanning = false
        }
    }

    Column(Modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(horizontal = 14.dp)) {
        Text("Watch", fontSize = 30.sp, fontWeight = FontWeight.Bold, modifier = Modifier.padding(top = 18.dp, start = 4.dp))

        crash?.let { c ->
            Card(Modifier.padding(top = 12.dp)) {
                Text("The app crashed last time", color = Gta.red, fontWeight = FontWeight.Bold)
                Text(c.lines().take(6).joinToString("\n"), fontFamily = FontFamily.Monospace, fontSize = 10.sp, color = Gta.dim)
                Row {
                    TextButton({
                        (ctx.getSystemService(Context.CLIPBOARD_SERVICE) as ClipboardManager).setPrimaryClip(ClipData.newPlainText("GTA-Watch crash", c))
                    }) { Text("Copy report") }
                    TextButton({ CrashLog.clear(); crash = null }) { Text("Dismiss") }
                }
            }
        }

        // ---- connection
        Card(Modifier.padding(top = 12.dp)) {
            Row(verticalAlignment = Alignment.CenterVertically) {
                val c = when (link) {
                    LinkState.CONNECTED -> Gta.green
                    LinkState.CONNECTING, LinkState.SCANNING -> Gta.yellow
                    LinkState.OFF -> Gta.red
                }
                Box(Modifier.size(14.dp).background(c, CircleShape))
                Column(Modifier.padding(start = 12.dp).weight(1f)) {
                    Text(Store.deviceName ?: "No watch paired", fontWeight = FontWeight.SemiBold, fontSize = 18.sp)
                    Text(when (link) {
                        LinkState.CONNECTED -> "Connected · $detail · $rssi dBm"
                        LinkState.CONNECTING -> detail.ifBlank { "Connecting…" }
                        LinkState.SCANNING -> "Scanning…"
                        LinkState.OFF -> if (running) "Disconnected" else "Service stopped"
                    }, color = Gta.dim, fontSize = 13.sp)
                }
                SignalBars(if (link == LinkState.CONNECTED) rssi else -200)
            }
            Spacer(Modifier.height(12.dp))
            Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                Button({
                    pairError = ""
                    Companion.associate(ctx, { pairLauncher.launch(IntentSenderRequest.Builder(it).build()) }, { pairError = it })
                }, colors = ButtonDefaults.buttonColors(containerColor = Gta.green, contentColor = Color.Black)) {
                    Text(if (Store.deviceAddress == null) "Pair watch" else "Pair again")
                }
                if (Store.deviceAddress != null) {
                    if (running) OutlinedButton({ WatchService.stop(ctx) }) { Text("Stop") }
                    else OutlinedButton({ WatchService.start(ctx) }) { Text("Start") }
                }
            }
            if (Store.deviceAddress != null && !Companion.isAssociated(ctx, Store.deviceAddress)) {
                Text("Tip: tap Pair again once to register the watch with Android's companion system, so it stays connected in the background like a Galaxy Watch.",
                    color = Gta.yellow, fontSize = 12.sp, modifier = Modifier.padding(top = 8.dp))
            }
            if (pairError.isNotEmpty()) Text(pairError, color = Gta.dim, fontSize = 12.sp, modifier = Modifier.padding(top = 6.dp))
            TextButton({ scanning = true }, enabled = !scanning) { Text(if (scanning) "Scanning…" else "Pairing not working? Scan manually") }
            found.forEach { w ->
                Row(Modifier.fillMaxWidth().padding(top = 6.dp).background(Gta.cardHi, RoundedCornerShape(14.dp)).clickable {
                    scanner.stopScan()
                    scanning = false
                    found.clear()
                    pair(ctx, w.address, w.name)
                }.padding(14.dp), verticalAlignment = Alignment.CenterVertically) {
                    Column(Modifier.weight(1f)) {
                        Text(w.name, fontWeight = FontWeight.SemiBold)
                        Text(w.address, color = Gta.dim, fontSize = 12.sp)
                    }
                    Text("${w.rssi} dBm", color = Gta.dim, fontSize = 12.sp)
                }
            }
        }

        // ---- battery
        SectionTitle("Watch battery")
        Card {
            val fresh = t.receivedAt > 0
            Row(verticalAlignment = Alignment.CenterVertically) {
                Text(if (fresh && t.battery >= 0) "${t.battery}%" else "–", fontSize = 40.sp, fontWeight = FontWeight.Bold,
                    color = if (t.battery in 0..15) Gta.red else Gta.green)
                Column(Modifier.padding(start = 14.dp)) {
                    Text(when {
                        !fresh -> "Waiting for the watch"
                        t.charging -> "Charging"
                        t.usb -> "Plugged in · full"
                        t.battery in 0..15 -> "Low · power saver on"
                        else -> "On battery"
                    }, fontWeight = FontWeight.SemiBold)
                    if (fresh) Text("${t.millivolts} mV", color = Gta.dim, fontSize = 12.sp)
                }
            }
            if (history.size >= 2) {
                Spacer(Modifier.height(10.dp))
                BatteryChart(history)
                Text("Last ${((history.last().first - history.first().first) / 3_600_000.0).let { if (it < 1) "${(it * 60).toInt()} min" else "%.1f h".format(it) }}",
                    color = Gta.dim, fontSize = 11.sp)
            }
        }

        // ---- telemetry
        SectionTitle("From the watch")
        Card {
            val fresh = t.receivedAt > 0
            Row {
                Stat("Steps", if (fresh) "%,d".format(t.steps) else "–", "today", Modifier.weight(1f))
                Stat("Uptime", if (fresh) "${t.uptimeSec / 3600}h ${(t.uptimeSec / 60) % 60}m" else "–", if (fresh) "fw ${t.fw}" else "", Modifier.weight(1f))
            }
            if (fresh) Text("Screen: ${t.page} · MTU ${t.mtu} · free RAM ${t.heap / 1024} KB", color = Gta.dim, fontSize = 12.sp, modifier = Modifier.padding(top = 10.dp))
            if (mapStatus.isNotEmpty()) Text(mapStatus, color = Gta.dim, fontSize = 12.sp, modifier = Modifier.padding(top = 4.dp))
        }

        // ---- actions
        SectionTitle("Actions")
        Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            OutlinedButton({ WatchService.instance?.syncAll() }) { Text("Sync now") }
            OutlinedButton({ WatchService.instance?.refreshWeather() }) { Text("Weather") }
            OutlinedButton({
                WatchService.instance?.link?.send(Protocol.NOTIFY, """{"id":99,"app":"GTA-Watch","title":"Lamar","body":"Yo dog, you good? Swing by the hood later."}""".toByteArray())
            }) { Text("Test alert") }
        }
        Row(horizontalArrangement = Arrangement.spacedBy(8.dp), modifier = Modifier.padding(top = 4.dp)) {
            OutlinedButton({ Store.fix.value?.let { WatchService.instance?.refreshMap(it.lat, it.lon) } }) { Text("Reload map") }
            OutlinedButton({ WatchService.instance?.link?.send(Protocol.NOTIFY_CLEAR, ByteArray(0)) }) { Text("Clear alerts") }
        }

        SectionTitle("Updates")
        UpdateCard()

        SectionTitle("Link log")
        Card {
            if (log.isEmpty()) Text("Nothing yet", color = Gta.dim, fontSize = 12.sp)
            log.take(40).forEach { Text(it, fontFamily = FontFamily.Monospace, fontSize = 11.sp, color = Gta.dim) }
        }
        Spacer(Modifier.height(30.dp))
    }
}

/** App + watch updates from GitHub Releases. Shared by the Watch and Settings tabs. */
@Composable
fun UpdateCard() {
    val ctx = LocalContext.current
    val scope = rememberCoroutineScope()
    val rel by Store.update.collectAsState()
    val status by Store.updateStatus.collectAsState()
    val ota by Store.otaProgress.collectAsState()
    val t by Store.telemetry.collectAsState()
    var dl by remember { mutableStateOf(-1) }
    val appVer = Updater.appVersion(ctx)

    Card {
        Text("App $appVer · Watch ${t.fw.ifEmpty { "–" }}", fontWeight = FontWeight.SemiBold)
        if (status.isNotEmpty()) Text(status, color = Gta.dim, fontSize = 13.sp)
        rel?.let { r ->
            if (Updater.newer(r.version, appVer) && r.apkUrl != null) {
                Button({
                    scope.launch {
                        try {
                            val f = withContext(Dispatchers.IO) { Updater.download(r.apkUrl, java.io.File(ctx.cacheDir, "update.apk")) { dl = it } }
                            Updater.installApk(ctx, f)
                        } catch (e: Exception) {
                            Store.updateStatus.value = "Download failed: ${e.message}"
                        } finally {
                            dl = -1
                        }
                    }
                }, enabled = dl < 0, colors = ButtonDefaults.buttonColors(containerColor = Gta.green, contentColor = Color.Black)) {
                    Text("Update app to ${r.version}")
                }
            }
            if (t.fw.isNotEmpty() && Updater.newer(r.version, t.fw) && r.firmwareUrl != null) {
                Button({
                    scope.launch {
                        try {
                            val f = withContext(Dispatchers.IO) { Updater.download(r.firmwareUrl, java.io.File(ctx.cacheDir, "watch.bin")) { dl = it } }
                            dl = -1
                            WatchService.instance?.updateWatch(f.readBytes(), r.version)
                        } catch (e: Exception) {
                            Store.updateStatus.value = "Download failed: ${e.message}"
                            dl = -1
                        }
                    }
                }, enabled = dl < 0 && ota < 0 && Store.link.value == LinkState.CONNECTED,
                    colors = ButtonDefaults.buttonColors(containerColor = Gta.blue, contentColor = Color.Black)) {
                    Text("Update watch to ${r.version}")
                }
            }
        }
        if (dl >= 0) {
            Text("Downloading… $dl%", color = Gta.dim, fontSize = 12.sp)
            LinearProgressIndicator(progress = { dl / 100f }, modifier = Modifier.fillMaxWidth(), color = Gta.green)
        }
        if (ota >= 0) {
            Text("Sending to watch… $ota% (keep the phone close)", color = Gta.dim, fontSize = 12.sp)
            LinearProgressIndicator(progress = { ota / 100f }, modifier = Modifier.fillMaxWidth(), color = Gta.blue)
        }
        OutlinedButton({ WatchService.instance?.checkForUpdates(notify = false) ?: run { Store.updateStatus.value = "Start the watch service first" } },
            modifier = Modifier.padding(top = 6.dp)) { Text("Check for updates") }
    }
}

@Composable
private fun BatteryChart(h: List<Triple<Long, Int, Boolean>>) {
    Canvas(Modifier.fillMaxWidth().height(70.dp)) {
        val t0 = h.first().first
        val span = (h.last().first - t0).coerceAtLeast(1)
        fun pt(s: Triple<Long, Int, Boolean>) = Offset((s.first - t0).toFloat() / span * size.width, size.height * (1 - s.second / 100f))
        for (y in listOf(0.25f, 0.5f, 0.75f)) drawLine(Color(0xFF23282B), Offset(0f, size.height * y), Offset(size.width, size.height * y), 1f)
        for (i in 1 until h.size) {
            val a = pt(h[i - 1])
            val b = pt(h[i])
            drawLine(if (h[i].third) Gta.yellow else Gta.green, a, b, 5f)
        }
        val p = Path().apply {
            moveTo(0f, size.height)
            h.forEach { val o = pt(it); lineTo(o.x, o.y) }
            lineTo(size.width, size.height)
            close()
        }
        drawPath(p, Gta.green.copy(alpha = 0.12f))
        drawRect(Color(0xFF23282B), style = Stroke(1f))
    }
}

@Composable
private fun Stat(label: String, value: String, sub: String, modifier: Modifier) {
    Column(modifier) {
        Text(label, color = Gta.dim, fontSize = 12.sp)
        Text(value, fontSize = 24.sp, fontWeight = FontWeight.Bold)
        if (sub.isNotEmpty()) Text(sub, color = Gta.dim, fontSize = 12.sp)
    }
}

/** GTA-style signal bars for the watch link strength. */
@Composable
private fun SignalBars(rssi: Int) {
    val bars = when {
        rssi > -60 -> 4
        rssi > -72 -> 3
        rssi > -84 -> 2
        rssi > -96 -> 1
        else -> 0
    }
    Row(verticalAlignment = Alignment.Bottom, horizontalArrangement = Arrangement.spacedBy(3.dp)) {
        for (i in 1..4) Box(Modifier.size(5.dp, (6 + i * 5).dp).background(if (i <= bars) Gta.green else Gta.cardHi, RoundedCornerShape(1.dp)))
    }
}
