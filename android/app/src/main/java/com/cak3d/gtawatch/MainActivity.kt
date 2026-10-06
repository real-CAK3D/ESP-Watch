package com.cak3d.gtawatch

import android.Manifest
import android.content.Intent
import android.os.Build
import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.padding
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Map
import androidx.compose.material.icons.filled.Place
import androidx.compose.material.icons.filled.Watch
import androidx.compose.material3.Icon
import androidx.compose.material3.NavigationBar
import androidx.compose.material3.NavigationBarItem
import androidx.compose.material3.NavigationBarItemDefaults
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import com.cak3d.gtawatch.ui.Gta
import com.cak3d.gtawatch.ui.GtaTheme
import com.cak3d.gtawatch.ui.MapScreen
import com.cak3d.gtawatch.ui.PlacesScreen
import com.cak3d.gtawatch.ui.WatchScreen

class MainActivity : ComponentActivity() {
    private val radarRequest = mutableStateOf(0L)

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        enableEdgeToEdge()
        handleIntent(intent)
        setContent { GtaTheme { App(radarRequest.value) } }
    }

    override fun onNewIntent(intent: Intent) {
        super.onNewIntent(intent)
        handleIntent(intent)
    }

    private fun handleIntent(i: Intent?) {
        if (i?.getStringExtra("open") == "radar") radarRequest.value = System.currentTimeMillis()
    }
}

private fun neededPermissions(): Array<String> {
    val p = mutableListOf(Manifest.permission.ACCESS_FINE_LOCATION, Manifest.permission.ACCESS_COARSE_LOCATION)
    if (Build.VERSION.SDK_INT >= 31) p += listOf(Manifest.permission.BLUETOOTH_SCAN, Manifest.permission.BLUETOOTH_CONNECT)
    if (Build.VERSION.SDK_INT >= 33) p += Manifest.permission.POST_NOTIFICATIONS
    return p.toTypedArray()
}

@Composable
private fun App(radarRequest: Long) {
    var tab by rememberSaveable { mutableIntStateOf(0) }
    var radarTick by remember { mutableStateOf(0L) }
    val watchMapReq by Store.openMapRequest.collectAsState()
    var permsAsked by rememberSaveable { mutableStateOf(false) }

    val launcher = rememberLauncherForActivityResult(ActivityResultContracts.RequestMultiplePermissions()) {
        WatchService.instance?.startLocation()
    }
    val ctx = androidx.compose.ui.platform.LocalContext.current
    LaunchedEffect(Unit) {
        if (!permsAsked) {
            permsAsked = true
            launcher.launch(neededPermissions())
        }
        if (Store.deviceAddress != null) WatchService.start(ctx)
    }
    // A tap on the watch's minimap (or the "open" intent) jumps to the radar view.
    LaunchedEffect(radarRequest, watchMapReq) {
        val t = maxOf(radarRequest, watchMapReq)
        if (t > 0) {
            tab = 0
            radarTick = t
        }
    }

    Scaffold(
        containerColor = Gta.bg,
        bottomBar = {
            NavigationBar(containerColor = Gta.card) {
                val colors = NavigationBarItemDefaults.colors(
                    selectedIconColor = Gta.green, selectedTextColor = Gta.green, indicatorColor = Gta.cardHi,
                    unselectedIconColor = Gta.dim, unselectedTextColor = Gta.dim,
                )
                NavigationBarItem(tab == 0, { tab = 0 }, { Icon(Icons.Filled.Map, null) }, label = { Text("Map") }, colors = colors)
                NavigationBarItem(tab == 1, { tab = 1 }, { Icon(Icons.Filled.Place, null) }, label = { Text("Places") }, colors = colors)
                NavigationBarItem(tab == 2, { tab = 2 }, { Icon(Icons.Filled.Watch, null) }, label = { Text("Watch") }, colors = colors)
            }
        },
    ) { pad ->
        Box(Modifier.fillMaxSize().padding(pad)) {
            when (tab) {
                0 -> MapScreen(radarTick)
                1 -> PlacesScreen(onNavigate = { tab = 0 })
                else -> WatchScreen()
            }
        }
    }
}
