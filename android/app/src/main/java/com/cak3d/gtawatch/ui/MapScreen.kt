package com.cak3d.gtawatch.ui

import android.graphics.Bitmap
import android.graphics.Canvas
import android.graphics.Paint
import android.graphics.Path
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
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.text.KeyboardActions
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.DirectionsWalk
import androidx.compose.material.icons.filled.Close
import androidx.compose.material.icons.filled.DirectionsCar
import androidx.compose.material.icons.filled.MyLocation
import androidx.compose.material.icons.filled.Navigation
import androidx.compose.material.icons.filled.Search
import androidx.compose.material.icons.filled.StarOutline
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Text
import androidx.compose.material3.TextField
import androidx.compose.material3.TextFieldDefaults
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.CornerRadius
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.LocalLifecycleOwner
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.input.ImeAction
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.compose.ui.viewinterop.AndroidView
import androidx.lifecycle.Lifecycle
import androidx.lifecycle.LifecycleEventObserver
import com.cak3d.gtawatch.Net
import com.cak3d.gtawatch.Place
import com.cak3d.gtawatch.Store
import com.cak3d.gtawatch.WatchService
import com.cak3d.gtawatch.fmtDist
import com.cak3d.gtawatch.kindOf
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import org.maplibre.android.camera.CameraPosition
import org.maplibre.android.camera.CameraUpdateFactory
import org.maplibre.android.geometry.LatLng
import org.maplibre.android.maps.MapLibreMap
import org.maplibre.android.maps.MapView
import org.maplibre.android.maps.Style
import org.maplibre.android.style.expressions.Expression
import org.maplibre.android.style.layers.CircleLayer
import org.maplibre.android.style.layers.LineLayer
import org.maplibre.android.style.layers.Property
import org.maplibre.android.style.layers.PropertyFactory
import org.maplibre.android.style.layers.SymbolLayer
import org.maplibre.android.style.sources.GeoJsonSource
import org.maplibre.geojson.Feature
import org.maplibre.geojson.FeatureCollection
import org.maplibre.geojson.LineString
import org.maplibre.geojson.Point

private data class Target(val name: String, val address: String, val lat: Double, val lon: Double)

private fun blipBitmap(): Bitmap {
    val s = 96
    val b = Bitmap.createBitmap(s, s, Bitmap.Config.ARGB_8888)
    val c = Canvas(b)
    fun arrow(scale: Float) = Path().apply {
        val m = s / 2f
        moveTo(m, m - 34 * scale)
        lineTo(m + 24 * scale, m + 26 * scale)
        lineTo(m, m + 12 * scale)
        lineTo(m - 24 * scale, m + 26 * scale)
        close()
    }
    c.drawPath(arrow(1.15f), Paint(Paint.ANTI_ALIAS_FLAG).apply { color = 0xFF1A1A1A.toInt() })
    c.drawPath(arrow(0.9f), Paint(Paint.ANTI_ALIAS_FLAG).apply { color = 0xFFFFFFFF.toInt() })
    return b
}

@Composable
fun MapScreen(radarRequest: Long) {
    val ctx = LocalContext.current
    val lifecycle = LocalLifecycleOwner.current.lifecycle
    val scope = rememberCoroutineScope()
    val fix by Store.fix.collectAsState()
    val nav by Store.nav.collectAsState()
    val places by Store.places.collectAsState()
    val telemetry by Store.telemetry.collectAsState()

    var radar by remember { mutableStateOf(true) }  // GTA expanded radar vs. pause map
    var map by remember { mutableStateOf<MapLibreMap?>(null) }
    var style by remember { mutableStateOf<Style?>(null) }
    var query by remember { mutableStateOf("") }
    var results by remember { mutableStateOf<List<Net.SearchHit>>(emptyList()) }
    var target by remember { mutableStateOf<Target?>(null) }
    var busy by remember { mutableStateOf("") }

    LaunchedEffect(radarRequest) { if (radarRequest > 0) radar = true }

    val mapView = remember {
        MapView(ctx).apply {
            onCreate(null)
            getMapAsync { m ->
                map = m
                m.uiSettings.isCompassEnabled = false
                m.uiSettings.isAttributionEnabled = true
                m.uiSettings.isLogoEnabled = false
                m.setStyle(Style.Builder().fromJson(ctx.assets.open("gta_style.json").bufferedReader().readText())) { st ->
                    st.addImage("blip", blipBitmap())
                    st.addSource(GeoJsonSource("route"))
                    st.addSource(GeoJsonSource("places"))
                    st.addSource(GeoJsonSource("player"))
                    st.addLayer(LineLayer("route-casing", "route").withProperties(
                        PropertyFactory.lineColor("#4a1a7a"), PropertyFactory.lineWidth(11f),
                        PropertyFactory.lineCap(Property.LINE_CAP_ROUND), PropertyFactory.lineJoin(Property.LINE_JOIN_ROUND)))
                    st.addLayer(LineLayer("route-line", "route").withProperties(
                        PropertyFactory.lineColor("#a64cf2"), PropertyFactory.lineWidth(7f),
                        PropertyFactory.lineCap(Property.LINE_CAP_ROUND), PropertyFactory.lineJoin(Property.LINE_JOIN_ROUND)))
                    st.addLayer(CircleLayer("places-dot", "places").withProperties(
                        PropertyFactory.circleRadius(11f), PropertyFactory.circleColor("#000000"),
                        PropertyFactory.circleStrokeWidth(3f), PropertyFactory.circleStrokeColor(Expression.get("color"))))
                    st.addLayer(SymbolLayer("places-label", "places").withProperties(
                        PropertyFactory.textField(Expression.get("name")), PropertyFactory.textFont(arrayOf("Noto Sans Bold")),
                        PropertyFactory.textSize(12f), PropertyFactory.textColor("#ffffff"), PropertyFactory.textHaloColor("#000000"),
                        PropertyFactory.textHaloWidth(1.5f), PropertyFactory.textOffset(arrayOf(0f, 1.6f)),
                        PropertyFactory.textAnchor(Property.TEXT_ANCHOR_TOP)))
                    st.addLayer(SymbolLayer("player-blip", "player").withProperties(
                        PropertyFactory.iconImage("blip"), PropertyFactory.iconSize(0.55f),
                        PropertyFactory.iconRotate(Expression.get("hdg")),
                        PropertyFactory.iconRotationAlignment(Property.ICON_ROTATION_ALIGNMENT_MAP),
                        PropertyFactory.iconAllowOverlap(true), PropertyFactory.iconIgnorePlacement(true)))
                    style = st
                }
                m.addOnMapLongClickListener { p ->
                    // GTA: set a waypoint anywhere on the map
                    target = Target("Waypoint", "%.5f, %.5f".format(p.latitude, p.longitude), p.latitude, p.longitude)
                    true
                }
                m.addOnCameraMoveStartedListener { reason ->
                    if (reason == MapLibreMap.OnCameraMoveStartedListener.REASON_API_GESTURE) radar = false
                }
            }
        }
    }

    DisposableEffect(lifecycle) {
        val obs = LifecycleEventObserver { _, e ->
            when (e) {
                Lifecycle.Event.ON_START -> mapView.onStart()
                Lifecycle.Event.ON_RESUME -> mapView.onResume()
                Lifecycle.Event.ON_PAUSE -> mapView.onPause()
                Lifecycle.Event.ON_STOP -> mapView.onStop()
                else -> {}
            }
        }
        lifecycle.addObserver(obs)
        if (lifecycle.currentState.isAtLeast(Lifecycle.State.STARTED)) mapView.onStart()
        if (lifecycle.currentState.isAtLeast(Lifecycle.State.RESUMED)) mapView.onResume()
        onDispose {
            lifecycle.removeObserver(obs)
            mapView.onPause()
            mapView.onStop()
            mapView.onDestroy()
        }
    }

    // ---- data -> map sources
    LaunchedEffect(style, fix) {
        val f = fix ?: return@LaunchedEffect
        val st = style ?: return@LaunchedEffect
        val feat = Feature.fromGeometry(Point.fromLngLat(f.lon, f.lat)).apply { addNumberProperty("hdg", f.heading) }
        (st.getSource("player") as? GeoJsonSource)?.setGeoJson(feat)
    }
    LaunchedEffect(style, nav.route) {
        val st = style ?: return@LaunchedEffect
        val src = st.getSource("route") as? GeoJsonSource ?: return@LaunchedEffect
        if (nav.route.size >= 2) src.setGeoJson(Feature.fromGeometry(LineString.fromLngLats(nav.route.map { Point.fromLngLat(it[1], it[0]) })))
        else src.setGeoJson(FeatureCollection.fromFeatures(emptyList()))
    }
    LaunchedEffect(style, places) {
        val st = style ?: return@LaunchedEffect
        val fc = FeatureCollection.fromFeatures(places.map {
            Feature.fromGeometry(Point.fromLngLat(it.lon, it.lat)).apply {
                addStringProperty("name", it.name)
                addStringProperty("color", "#%06x".format(kindOf(it.kind).color and 0xFFFFFF))
            }
        })
        (st.getSource("places") as? GeoJsonSource)?.setGeoJson(fc)
    }
    // ---- camera
    LaunchedEffect(map, fix, radar) {
        val m = map ?: return@LaunchedEffect
        val f = fix ?: return@LaunchedEffect
        if (radar) {
            val zoom = if (f.speed > 15) 15.6 else if (f.speed > 6) 16.2 else 16.8
            m.easeCamera(CameraUpdateFactory.newCameraPosition(
                CameraPosition.Builder().target(LatLng(f.lat, f.lon)).zoom(zoom).bearing(f.heading.toDouble()).tilt(0.0).build()), 900)
        }
    }

    Box(Modifier.fillMaxSize().background(Gta.bg)) {
        AndroidView({ mapView }, Modifier.fillMaxSize())

        // GTA radar frame: health (watch battery) + armor (phone link) bars
        if (radar) {
            androidx.compose.foundation.Canvas(Modifier.fillMaxSize()) {
                val w = 10.dp.toPx()
                val bat = (telemetry.battery.coerceIn(0, 100)) / 100f
                drawRoundRect(Color.Black, Offset(w / 2, w / 2), Size(size.width - w, size.height - w), CornerRadius(26.dp.toPx()), style = Stroke(w))
                val barY = size.height - w * 1.6f
                val half = (size.width - w * 4) / 2
                drawRect(Color(0xFF24421F), Offset(w * 1.5f, barY), Size(half, w * 0.9f))
                drawRect(Color(0xFF5FB65A), Offset(w * 1.5f, barY), Size(half * bat, w * 0.9f))
                drawRect(Color(0xFF1D3A4E), Offset(w * 2.5f + half, barY), Size(half, w * 0.9f))
                drawRect(Color(0xFF5DADE2), Offset(w * 2.5f + half, barY), Size(half * if (Store.link.value.name == "CONNECTED") 1f else 0.15f, w * 0.9f))
            }
        }

        Column(Modifier.fillMaxWidth().padding(12.dp)) {
            // search bar
            TextField(
                value = query, onValueChange = { query = it },
                placeholder = { Text("Search address or place") },
                leadingIcon = { Icon(Icons.Filled.Search, null, tint = Gta.dim) },
                trailingIcon = {
                    if (query.isNotEmpty()) IconButton({ query = ""; results = emptyList() }) { Icon(Icons.Filled.Close, null) }
                },
                singleLine = true,
                keyboardOptions = KeyboardOptions(imeAction = ImeAction.Search),
                keyboardActions = KeyboardActions(onSearch = {
                    scope.launch {
                        busy = "Searching…"
                        results = try {
                            withContext(Dispatchers.IO) { Net.search(query, fix?.lat, fix?.lon) }
                        } catch (e: Exception) {
                            emptyList()
                        }
                        busy = if (results.isEmpty()) "Nothing found" else ""
                    }
                }),
                shape = RoundedCornerShape(28.dp),
                colors = TextFieldDefaults.colors(
                    focusedContainerColor = Color(0xEE121517), unfocusedContainerColor = Color(0xDD121517),
                    focusedIndicatorColor = Color.Transparent, unfocusedIndicatorColor = Color.Transparent,
                ),
                modifier = Modifier.fillMaxWidth(),
            )
            if (busy.isNotEmpty()) Text(busy, color = Gta.dim, modifier = Modifier.padding(10.dp))
            if (results.isNotEmpty()) {
                LazyColumn(Modifier.padding(top = 6.dp).heightIn(max = 300.dp).background(Color(0xF0121517), RoundedCornerShape(20.dp))) {
                    items(results) { r ->
                        Column(Modifier.fillMaxWidth().clickable {
                            target = Target(r.name, r.address, r.lat, r.lon)
                            results = emptyList()
                            radar = false
                            map?.animateCamera(CameraUpdateFactory.newLatLngZoom(LatLng(r.lat, r.lon), 15.0))
                        }.padding(14.dp)) {
                            Text(r.name, fontWeight = FontWeight.SemiBold)
                            Text(r.address, color = Gta.dim, fontSize = 12.sp, maxLines = 1, overflow = TextOverflow.Ellipsis)
                        }
                    }
                }
            }
        }

        // map mode + recenter
        Column(Modifier.align(Alignment.CenterEnd).padding(14.dp), verticalArrangement = Arrangement.spacedBy(10.dp)) {
            RoundButton(if (radar) Icons.Filled.Navigation else Icons.Filled.MyLocation, if (radar) Gta.green else Gta.text) {
                radar = !radar
                if (!radar) map?.animateCamera(CameraUpdateFactory.newCameraPosition(
                    CameraPosition.Builder().zoom(14.0).bearing(0.0).also { b -> fix?.let { b.target(LatLng(it.lat, it.lon)) } }.build()))
            }
        }

        // bottom: active route or chosen destination
        Column(Modifier.align(Alignment.BottomCenter).fillMaxWidth().padding(12.dp)) {
            val f = fix
            if (f != null && f.street.isNotEmpty() && !nav.active && target == null) {
                Column(Modifier.align(Alignment.CenterHorizontally).background(Color(0xCC000000), RoundedCornerShape(16.dp)).padding(horizontal = 16.dp, vertical = 6.dp),
                    horizontalAlignment = Alignment.CenterHorizontally) {
                    Text(f.street, fontWeight = FontWeight.Bold, fontSize = 17.sp)
                    if (f.area.isNotEmpty()) Text(f.area, color = Gta.yellow, fontSize = 13.sp)
                }
            }
            if (nav.active) NavCard(nav.instruction, fmtDist(nav.distNext), "${(nav.etaSec + 59) / 60} min · ${fmtDist(nav.remain)}" +
                (if (nav.cost.isNotEmpty()) " · ${nav.cost} gas" else "")) { WatchService.instance?.stopNav() }
            target?.let { t ->
                Card {
                    Text(t.name, fontWeight = FontWeight.Bold, fontSize = 19.sp)
                    Text(t.address, color = Gta.dim, fontSize = 13.sp, maxLines = 2, overflow = TextOverflow.Ellipsis)
                    Spacer(Modifier.height(12.dp))
                    Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                        Button({
                            WatchService.instance?.startNav(t.name, t.lat, t.lon, false); target = null; radar = true
                        }, colors = ButtonDefaults.buttonColors(containerColor = Gta.purple, contentColor = Color.White)) {
                            Icon(Icons.Filled.DirectionsCar, null); Spacer(Modifier.width(6.dp)); Text("Drive")
                        }
                        Button({
                            WatchService.instance?.startNav(t.name, t.lat, t.lon, true); target = null; radar = true
                        }, colors = ButtonDefaults.buttonColors(containerColor = Gta.cardHi, contentColor = Color.White)) {
                            Icon(Icons.AutoMirrored.Filled.DirectionsWalk, null); Spacer(Modifier.width(6.dp)); Text("Walk")
                        }
                        OutlinedButton({
                            Store.addPlace(Place(t.name, "custom", t.lat, t.lon, t.address))
                            WatchService.instance?.sendPlaces()
                            target = null
                        }) { Icon(Icons.Filled.StarOutline, null) }
                        IconButton({ target = null }) { Icon(Icons.Filled.Close, null) }
                    }
                }
            }
            if (WatchService.instance == null) {
                Text("Pair your watch in the Watch tab to start GPS", color = Gta.dim, fontSize = 13.sp,
                    modifier = Modifier.align(Alignment.CenterHorizontally).background(Color(0xCC000000), RoundedCornerShape(12.dp)).padding(10.dp))
            }
        }
    }
}

@Composable
private fun RoundButton(icon: androidx.compose.ui.graphics.vector.ImageVector, tint: Color, onClick: () -> Unit) {
    Box(Modifier.size(52.dp).background(Color(0xEE121517), CircleShape).clickable(onClick = onClick), contentAlignment = Alignment.Center) {
        Icon(icon, null, tint = tint)
    }
}

@Composable
private fun NavCard(instruction: String, dist: String, meta: String, onStop: () -> Unit) {
    Row(
        Modifier.fillMaxWidth().background(Color(0xF0121517), RoundedCornerShape(22.dp)).padding(16.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Column(Modifier.weight(1f)) {
            Text(dist, fontSize = 30.sp, fontWeight = FontWeight.Bold)
            Text(instruction, fontSize = 16.sp, maxLines = 2, overflow = TextOverflow.Ellipsis)
            Text(meta, color = Color(0xFFB98AF0), fontSize = 13.sp)
        }
        IconButton(onStop) { Icon(Icons.Filled.Close, "Stop route", tint = Gta.red) }
    }
}
