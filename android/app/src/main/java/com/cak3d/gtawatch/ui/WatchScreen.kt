package com.cak3d.gtawatch.ui

import android.content.Intent
import android.net.Uri
import android.provider.Settings as AndroidSettings
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
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Slider
import androidx.compose.material3.SliderDefaults
import androidx.compose.material3.Switch
import androidx.compose.material3.SwitchDefaults
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateListOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.cak3d.gtawatch.BleLink
import com.cak3d.gtawatch.FoundWatch
import com.cak3d.gtawatch.LinkState
import com.cak3d.gtawatch.Protocol
import com.cak3d.gtawatch.Settings
import com.cak3d.gtawatch.Store
import com.cak3d.gtawatch.WatchService
import kotlinx.coroutines.delay

@Composable
fun WatchScreen() {
    val ctx = LocalContext.current
    val link by Store.link.collectAsState()
    val detail by Store.linkDetail.collectAsState()
    val rssi by Store.rssi.collectAsState()
    val t by Store.telemetry.collectAsState()
    val settings by Store.settings.collectAsState()
    val mapStatus by Store.mapStatus.collectAsState()
    val log by Store.log.collectAsState()
    val running by Store.serviceRunning.collectAsState()
    var scanning by remember { mutableStateOf(false) }
    val found = remember { mutableStateListOf<FoundWatch>() }
    val scanner = remember { BleLink(ctx, object : BleLink.Listener {
        override fun onReady() {}
        override fun onMessage(type: Int, payload: ByteArray) {}
        override fun onDisconnected() {}
    }) }

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
                        LinkState.CONNECTED -> "Connected · $detail · ${rssi} dBm"
                        LinkState.CONNECTING -> detail.ifBlank { "Connecting…" }
                        LinkState.SCANNING -> "Scanning…"
                        LinkState.OFF -> if (running) "Disconnected" else "Service stopped"
                    }, color = Gta.dim, fontSize = 13.sp)
                }
                SignalBars(if (link == LinkState.CONNECTED) rssi else -200)
            }
            Spacer(Modifier.height(12.dp))
            Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                Button({ scanning = true }, enabled = !scanning,
                    colors = ButtonDefaults.buttonColors(containerColor = Gta.green, contentColor = Color.Black)) {
                    Text(if (scanning) "Scanning…" else if (Store.deviceAddress == null) "Find watch" else "Change watch")
                }
                if (Store.deviceAddress != null) {
                    if (running) OutlinedButton({ WatchService.stop(ctx) }) { Text("Stop") }
                    else OutlinedButton({ WatchService.start(ctx) }) { Text("Start") }
                }
            }
            found.forEach { w ->
                Row(Modifier.fillMaxWidth().padding(top = 8.dp).background(Gta.cardHi, RoundedCornerShape(14.dp)).clickable {
                    Store.deviceAddress = w.address
                    Store.deviceName = w.name
                    scanner.stopScan()
                    scanning = false
                    found.clear()
                    WatchService.start(ctx)
                    WatchService.instance?.link?.connect(w.address)
                }.padding(14.dp), verticalAlignment = Alignment.CenterVertically) {
                    Column(Modifier.weight(1f)) {
                        Text(w.name, fontWeight = FontWeight.SemiBold)
                        Text(w.address, color = Gta.dim, fontSize = 12.sp)
                    }
                    Text("${w.rssi} dBm", color = Gta.dim, fontSize = 12.sp)
                }
            }
            if (scanning && found.isEmpty()) Text("Make sure the watch is on and nearby.", color = Gta.dim, fontSize = 13.sp, modifier = Modifier.padding(top = 8.dp))
        }

        // ---- telemetry from the watch
        SectionTitle("From the watch")
        Card {
            val fresh = t.receivedAt > 0
            Row {
                Stat("Battery", if (t.battery >= 0) "${t.battery}%" else "–", if (t.charging) "charging" else if (fresh) "${t.millivolts} mV" else "", Modifier.weight(1f))
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

        // ---- settings
        SectionTitle("Watch settings")
        Card {
            fun push(f: (Settings) -> Settings) {
                Store.updateSettings(f)
                WatchService.instance?.sendSettings()
            }
            Toggle("24-hour clock", settings.h24) { v -> push { it.copy(h24 = v) } }
            Toggle("Metric units", settings.metric) { v -> push { it.copy(metric = v) } }
            Toggle("Walk by default (watch routes)", settings.walkDefault) { v -> push { it.copy(walkDefault = v) } }
            Toggle("Forward phone notifications", settings.forwardNotifications) { v -> push { it.copy(forwardNotifications = v) } }
            Text("Screen timeout: ${settings.screenTimeout}s", modifier = Modifier.padding(top = 8.dp))
            Slider(settings.screenTimeout.toFloat(), { v -> Store.updateSettings { it.copy(screenTimeout = v.toInt()) } },
                valueRange = 5f..60f, onValueChangeFinished = { WatchService.instance?.sendSettings() }, colors = sliderColors())
            Text("Brightness")
            Slider(settings.brightness.toFloat(), { v -> Store.updateSettings { it.copy(brightness = v.toInt()) } },
                valueRange = 20f..255f, onValueChangeFinished = { WatchService.instance?.sendSettings() }, colors = sliderColors())
        }

        SectionTitle("Vehicle (gas cost on routes)")
        Card {
            Text("Fuel economy: ${settings.mpg.toInt()} mpg")
            Slider(settings.mpg.toFloat(), { v -> Store.updateSettings { it.copy(mpg = v.toDouble()) } }, valueRange = 8f..60f, colors = sliderColors())
            Text("Gas price: $%.2f / gal".format(settings.gasPrice))
            Slider(settings.gasPrice.toFloat(), { v -> Store.updateSettings { it.copy(gasPrice = (v * 100).toInt() / 100.0) } }, valueRange = 2f..7f, colors = sliderColors())
        }

        SectionTitle("Phone permissions")
        Card {
            Text("Notification access lets texts and app alerts appear on the watch.", color = Gta.dim, fontSize = 13.sp)
            OutlinedButton({ ctx.startActivity(Intent("android.settings.ACTION_NOTIFICATION_LISTENER_SETTINGS")) }) { Text("Notification access") }
            Spacer(Modifier.height(8.dp))
            Text("\"Display over other apps\" lets a tap on the watch's minimap pop the big map open.", color = Gta.dim, fontSize = 13.sp)
            OutlinedButton({
                ctx.startActivity(Intent(AndroidSettings.ACTION_MANAGE_OVERLAY_PERMISSION, Uri.parse("package:${ctx.packageName}")))
            }) { Text("Allow map pop-up") }
            Spacer(Modifier.height(8.dp))
            Text("Battery: set GTA-Watch to \"Unrestricted\" so the watch stays connected with the screen off.", color = Gta.dim, fontSize = 13.sp)
            OutlinedButton({
                ctx.startActivity(Intent(AndroidSettings.ACTION_APPLICATION_DETAILS_SETTINGS, Uri.parse("package:${ctx.packageName}")))
            }) { Text("App battery settings") }
        }

        SectionTitle("Link log")
        Card {
            if (log.isEmpty()) Text("Nothing yet", color = Gta.dim, fontSize = 12.sp)
            log.take(40).forEach { Text(it, fontFamily = FontFamily.Monospace, fontSize = 11.sp, color = Gta.dim) }
        }
        Spacer(Modifier.height(30.dp))
    }
}

@Composable
private fun sliderColors() = SliderDefaults.colors(thumbColor = Gta.green, activeTrackColor = Gta.green)

@Composable
private fun Toggle(label: String, value: Boolean, onChange: (Boolean) -> Unit) {
    Row(Modifier.fillMaxWidth().padding(vertical = 2.dp), verticalAlignment = Alignment.CenterVertically) {
        Text(label, Modifier.weight(1f))
        Switch(value, onChange, colors = SwitchDefaults.colors(checkedTrackColor = Gta.green, checkedThumbColor = Color.Black))
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
