package com.cak3d.gtawatch.ui

import android.location.Location
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ExperimentalLayoutApi
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.DirectionsWalk
import androidx.compose.material.icons.filled.Add
import androidx.compose.material.icons.filled.Delete
import androidx.compose.material.icons.filled.DirectionsCar
import androidx.compose.material.icons.filled.Edit
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.ExtendedFloatingActionButton
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.cak3d.gtawatch.Net
import com.cak3d.gtawatch.PLACE_KINDS
import com.cak3d.gtawatch.Place
import com.cak3d.gtawatch.Store
import com.cak3d.gtawatch.WatchService
import com.cak3d.gtawatch.fmtDist
import com.cak3d.gtawatch.kindOf
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext

@Composable
fun PlacesScreen(onNavigate: () -> Unit) {
    val places by Store.places.collectAsState()
    val fix by Store.fix.collectAsState()
    var editing by remember { mutableStateOf<Place?>(null) }
    var adding by remember { mutableStateOf(false) }

    Box(Modifier.fillMaxSize()) {
        LazyColumn(Modifier.fillMaxSize().padding(horizontal = 14.dp), verticalArrangement = Arrangement.spacedBy(10.dp)) {
            item {
                Text("Places", fontSize = 30.sp, fontWeight = FontWeight.Bold, modifier = Modifier.padding(top = 18.dp, start = 4.dp))
                Text("Your spots show up as GTA blips on the watch minimap. Tap one on the watch to start a route.",
                    color = Gta.dim, fontSize = 14.sp, modifier = Modifier.padding(4.dp))
            }
            if (places.isEmpty()) item {
                Card { Text("No places yet. Add your Safe House, your Mechanic, or where you park.", color = Gta.dim) }
            }
            items(places) { p ->
                val dist = fix?.let { f ->
                    val r = FloatArray(1)
                    Location.distanceBetween(f.lat, f.lon, p.lat, p.lon, r)
                    fmtDist(r[0].toDouble())
                }
                Row(
                    Modifier.fillMaxWidth().background(Gta.card, RoundedCornerShape(22.dp)).padding(14.dp),
                    verticalAlignment = Alignment.CenterVertically,
                ) {
                    Blip(p.kind)
                    Column(Modifier.weight(1f).padding(horizontal = 12.dp)) {
                        Text(p.name, fontWeight = FontWeight.SemiBold, fontSize = 17.sp)
                        Text(listOfNotNull(kindOf(p.kind).label, dist).joinToString(" · "), color = Gta.dim, fontSize = 13.sp)
                    }
                    IconButton({ WatchService.instance?.startNav(p.name, p.lat, p.lon, false); onNavigate() }) {
                        Icon(Icons.Filled.DirectionsCar, "Drive", tint = Gta.purple)
                    }
                    IconButton({ WatchService.instance?.startNav(p.name, p.lat, p.lon, true); onNavigate() }) {
                        Icon(Icons.AutoMirrored.Filled.DirectionsWalk, "Walk", tint = Gta.blue)
                    }
                    IconButton({ editing = p }) { Icon(Icons.Filled.Edit, "Edit", tint = Gta.dim) }
                }
            }
            item { Spacer(Modifier.size(90.dp)) }
        }
        ExtendedFloatingActionButton(
            onClick = { adding = true },
            icon = { Icon(Icons.Filled.Add, null) }, text = { Text("Add place") },
            containerColor = Gta.green, contentColor = Color.Black,
            modifier = Modifier.align(Alignment.BottomEnd).padding(18.dp),
        )
    }

    if (adding) PlaceDialog(null, onDismiss = { adding = false }) {
        Store.addPlace(it)
        WatchService.instance?.sendPlaces()
        adding = false
    }
    editing?.let { old ->
        PlaceDialog(old, onDismiss = { editing = null }, onDelete = {
            Store.removePlace(old)
            WatchService.instance?.sendPlaces()
            editing = null
        }) {
            Store.replacePlace(old, it)
            WatchService.instance?.sendPlaces()
            editing = null
        }
    }
}

@OptIn(ExperimentalLayoutApi::class)
@Composable
private fun PlaceDialog(initial: Place?, onDismiss: () -> Unit, onDelete: (() -> Unit)? = null, onSave: (Place) -> Unit) {
    val scope = rememberCoroutineScope()
    var name by remember { mutableStateOf(initial?.name ?: "") }
    var kind by remember { mutableStateOf(initial?.kind ?: "safehouse") }
    var lat by remember { mutableStateOf(initial?.lat) }
    var lon by remember { mutableStateOf(initial?.lon) }
    var address by remember { mutableStateOf(initial?.address ?: "") }
    var query by remember { mutableStateOf("") }
    var hits by remember { mutableStateOf<List<Net.SearchHit>>(emptyList()) }
    var status by remember { mutableStateOf("") }

    AlertDialog(
        onDismissRequest = onDismiss,
        containerColor = Gta.card,
        title = { Text(if (initial == null) "New place" else "Edit place") },
        text = {
            Column(verticalArrangement = Arrangement.spacedBy(10.dp)) {
                OutlinedTextField(name, { name = it }, label = { Text("Name") }, singleLine = true, modifier = Modifier.fillMaxWidth())
                Text("Blip", color = Gta.dim, fontSize = 13.sp)
                FlowRow(horizontalArrangement = Arrangement.spacedBy(8.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
                    PLACE_KINDS.forEach { k ->
                        Box(
                            Modifier.clickable {
                                if (name.isBlank() || PLACE_KINDS.any { it.label == name }) name = k.label
                                kind = k.key
                            }.then(if (kind == k.key) Modifier.border(2.dp, Gta.green, RoundedCornerShape(26.dp)) else Modifier).padding(3.dp),
                        ) { Blip(k.key, 40.dp) }
                    }
                }
                Text(kindOf(kind).label, color = Color(kindOf(kind).color), fontSize = 13.sp)
                Row(verticalAlignment = Alignment.CenterVertically) {
                    TextButton({
                        Store.fix.value?.let {
                            lat = it.lat; lon = it.lon; address = it.street.ifBlank { "Current location" }; status = "Using current location"
                        } ?: run { status = "No GPS fix yet" }
                    }) { Text("Use my location") }
                }
                OutlinedTextField(query, { query = it }, label = { Text("…or search an address") }, singleLine = true,
                    modifier = Modifier.fillMaxWidth(),
                    keyboardActions = androidx.compose.foundation.text.KeyboardActions(onSearch = {
                        scope.launch {
                            status = "Searching…"
                            hits = try { withContext(Dispatchers.IO) { Net.search(query, Store.fix.value?.lat, Store.fix.value?.lon) } } catch (e: Exception) { emptyList() }
                            status = if (hits.isEmpty()) "Nothing found" else ""
                        }
                    }),
                    keyboardOptions = androidx.compose.foundation.text.KeyboardOptions(imeAction = androidx.compose.ui.text.input.ImeAction.Search))
                if (hits.isNotEmpty()) LazyColumn(Modifier.heightIn(max = 180.dp)) {
                    items(hits) { h ->
                        Text(h.address, maxLines = 2, overflow = TextOverflow.Ellipsis, fontSize = 13.sp,
                            modifier = Modifier.fillMaxWidth().clickable {
                                lat = h.lat; lon = h.lon; address = h.address; hits = emptyList(); status = "Address set"
                            }.padding(8.dp))
                    }
                }
                if (address.isNotEmpty()) Text(address, color = Gta.dim, fontSize = 12.sp, maxLines = 2, overflow = TextOverflow.Ellipsis)
                if (status.isNotEmpty()) Text(status, color = Gta.yellow, fontSize = 12.sp)
            }
        },
        confirmButton = {
            TextButton({
                val la = lat
                val lo = lon
                if (la != null && lo != null) onSave(Place(name.ifBlank { kindOf(kind).label }, kind, la, lo, address))
                else status = "Pick a location first"
            }) { Text("Save") }
        },
        dismissButton = {
            Row {
                if (onDelete != null) IconButton(onDelete) { Icon(Icons.Filled.Delete, "Delete", tint = Gta.red) }
                Spacer(Modifier.width(4.dp))
                TextButton(onDismiss) { Text("Cancel") }
            }
        },
    )
}
