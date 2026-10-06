package com.cak3d.gtawatch.ui

import android.content.Intent
import android.net.Uri
import android.provider.Settings as AndroidSettings
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Slider
import androidx.compose.material3.SliderDefaults
import androidx.compose.material3.Switch
import androidx.compose.material3.SwitchDefaults
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.cak3d.gtawatch.Settings
import com.cak3d.gtawatch.Store
import com.cak3d.gtawatch.WatchService

/** Push a settings change to storage and, if it affects the watch, to the watch. */
private fun change(f: (Settings) -> Settings) {
    Store.updateSettings(f)
    WatchService.instance?.sendSettings()
}

@Composable
fun SettingsScreen() {
    val ctx = LocalContext.current
    val s by Store.settings.collectAsState()

    Column(Modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(horizontal = 14.dp)) {
        Text("Settings", fontSize = 30.sp, fontWeight = FontWeight.Bold, modifier = Modifier.padding(top = 18.dp, start = 4.dp))

        SectionTitle("Map")
        Card {
            Text("Phone map style")
            Spacer(Modifier.height(8.dp))
            Segmented(listOf("Road", "Atlas"), s.mapStyle) { v -> change { it.copy(mapStyle = v) } }
            Text(if (s.mapStyle == 0) "GTA V pause map: dark land, grey road tiers, olive parks." else "GTA V atlas: cream land, orange highways, shaded hills.",
                color = Gta.dim, fontSize = 12.sp, modifier = Modifier.padding(top = 6.dp))
            Toggle("Terrain on phone map", s.phoneTerrain) { v -> change { it.copy(phoneTerrain = v) } }
            Toggle("Terrain on watch minimap", s.watchTerrain) { v -> change { it.copy(watchTerrain = v) } }
            Text("Watch radar zoom", modifier = Modifier.padding(top = 8.dp))
            Spacer(Modifier.height(8.dp))
            Segmented(listOf("Close", "Normal", "Far"), s.radarZoom) { v -> change { it.copy(radarZoom = v) } }
            Text("The radar still zooms out by itself when you drive fast, like in GTA.", color = Gta.dim, fontSize = 12.sp, modifier = Modifier.padding(top = 6.dp))
        }

        SectionTitle("Watch")
        Card {
            Text("Watch face")
            Spacer(Modifier.height(8.dp))
            Segmented(listOf("Digital", "Analog"), s.watchFace) { v -> change { it.copy(watchFace = v) } }
            Toggle("Raise to wake", s.raiseToWake) { v -> change { it.copy(raiseToWake = v) } }
            Toggle("Keep minimap on while navigating", s.keepMapOn) { v -> change { it.copy(keepMapOn = v) } }
            Toggle("24-hour clock", s.h24) { v -> change { it.copy(h24 = v) } }
            Toggle("Metric units", s.metric) { v -> change { it.copy(metric = v) } }
            Text("Screen timeout: ${s.screenTimeout}s", modifier = Modifier.padding(top = 8.dp))
            Slider(s.screenTimeout.toFloat(), { v -> Store.updateSettings { it.copy(screenTimeout = v.toInt()) } },
                valueRange = 5f..60f, onValueChangeFinished = { WatchService.instance?.sendSettings() }, colors = sliderColors())
            Text("Brightness")
            Slider(s.brightness.toFloat(), { v -> Store.updateSettings { it.copy(brightness = v.toInt()) } },
                valueRange = 20f..255f, onValueChangeFinished = { WatchService.instance?.sendSettings() }, colors = sliderColors())
        }

        SectionTitle("Navigation")
        Card {
            Toggle("Spoken turn directions", s.voice) { v -> change { it.copy(voice = v) } }
            Toggle("Walk by default (routes from the watch)", s.walkDefault) { v -> change { it.copy(walkDefault = v) } }
            Text("Fuel economy: ${s.mpg.toInt()} mpg", modifier = Modifier.padding(top = 8.dp))
            Slider(s.mpg.toFloat(), { v -> Store.updateSettings { it.copy(mpg = v.toDouble()) } }, valueRange = 8f..60f, colors = sliderColors())
            Text("Gas price: $%.2f / gal".format(s.gasPrice))
            Slider(s.gasPrice.toFloat(), { v -> Store.updateSettings { it.copy(gasPrice = (v * 100).toInt() / 100.0) } }, valueRange = 2f..7f, colors = sliderColors())
        }

        SectionTitle("Alerts & charging")
        Card {
            Toggle("Forward phone notifications", s.forwardNotifications) { v -> change { it.copy(forwardNotifications = v) } }
            Toggle("Tell me when the watch is charged", s.chargeAlerts) { v -> change { it.copy(chargeAlerts = v) } }
            Toggle("Warn me when the watch battery is low", s.lowBatteryAlerts) { v -> change { it.copy(lowBatteryAlerts = v) } }
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
            Text("Allow installs from this app so updates from GitHub can be installed.", color = Gta.dim, fontSize = 13.sp)
            OutlinedButton({
                ctx.startActivity(Intent(AndroidSettings.ACTION_MANAGE_UNKNOWN_APP_SOURCES, Uri.parse("package:${ctx.packageName}")))
            }) { Text("Allow app updates") }
        }

        SectionTitle("Updates")
        UpdateCard()

        SectionTitle("About")
        Card {
            Text("GTA-Watch ${com.cak3d.gtawatch.Updater.appVersion(ctx)}", fontWeight = FontWeight.SemiBold)
            Text("Watch firmware ${Store.telemetry.collectAsState().value.fw.ifEmpty { "–" }}", color = Gta.dim, fontSize = 13.sp)
            Text("Map data © OpenStreetMap contributors · Tiles: OpenFreeMap · Terrain: AWS Terrain Tiles · Routes: OSRM · Weather: Open-Meteo",
                color = Gta.dim, fontSize = 11.sp, modifier = Modifier.padding(top = 6.dp))
            OutlinedButton({ ctx.startActivity(Intent(Intent.ACTION_VIEW, Uri.parse("https://github.com/real-CAK3D/ESP-Watch"))) },
                modifier = Modifier.padding(top = 6.dp)) { Text("GitHub") }
        }
        Spacer(Modifier.height(30.dp))
    }
}

@Composable
fun sliderColors() = SliderDefaults.colors(thumbColor = Gta.green, activeTrackColor = Gta.green)

@Composable
fun Toggle(label: String, value: Boolean, onChange: (Boolean) -> Unit) {
    Row(Modifier.fillMaxWidth().padding(vertical = 2.dp), verticalAlignment = Alignment.CenterVertically) {
        Text(label, Modifier.weight(1f))
        Switch(value, onChange, colors = SwitchDefaults.colors(checkedTrackColor = Gta.green, checkedThumbColor = Color.Black))
    }
}

@Composable
fun Segmented(options: List<String>, selected: Int, onSelect: (Int) -> Unit) {
    Row(Modifier.fillMaxWidth().background(Gta.cardHi, RoundedCornerShape(14.dp)).padding(4.dp), horizontalArrangement = Arrangement.spacedBy(4.dp)) {
        options.forEachIndexed { i, o ->
            Text(
                o, textAlign = TextAlign.Center, fontWeight = if (i == selected) FontWeight.Bold else FontWeight.Normal,
                color = if (i == selected) Color.Black else Gta.text,
                modifier = Modifier.weight(1f)
                    .background(if (i == selected) Gta.green else Color.Transparent, RoundedCornerShape(11.dp))
                    .clickable { onSelect(i) }.padding(vertical = 9.dp),
            )
        }
    }
}
