package com.cak3d.gtawatch.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ColumnScope
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.AttachMoney
import androidx.compose.material.icons.filled.Build
import androidx.compose.material.icons.filled.DirectionsCar
import androidx.compose.material.icons.filled.Favorite
import androidx.compose.material.icons.filled.Home
import androidx.compose.material.icons.filled.LocalGasStation
import androidx.compose.material.icons.filled.LocalPolice
import androidx.compose.material.icons.filled.Restaurant
import androidx.compose.material.icons.filled.ShoppingCart
import androidx.compose.material.icons.filled.Star
import androidx.compose.material.icons.filled.Water
import androidx.compose.material.icons.filled.Work
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.material3.darkColorScheme
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.cak3d.gtawatch.kindOf

object Gta {
    val bg = Color(0xFF000000)
    val card = Color(0xFF121517)
    val cardHi = Color(0xFF1E2326)
    val text = Color(0xFFFFFFFF)
    val dim = Color(0xFF98A2A6)
    val green = Color(0xFF72CC72)
    val blue = Color(0xFF5DADE2)
    val yellow = Color(0xFFF0C850)
    val purple = Color(0xFFA64CF2)
    val red = Color(0xFFE05A5A)
}

@Composable
fun GtaTheme(content: @Composable () -> Unit) {
    MaterialTheme(
        colorScheme = darkColorScheme(
            primary = Gta.green, onPrimary = Color.Black, secondary = Gta.blue, background = Gta.bg,
            surface = Gta.card, surfaceVariant = Gta.cardHi, onSurface = Gta.text, onBackground = Gta.text,
            error = Gta.red,
        ),
        content = content,
    )
}

fun kindIcon(kind: String): ImageVector = when (kind) {
    "safehouse" -> Icons.Filled.Home
    "mechanic" -> Icons.Filled.Build
    "parking" -> Icons.Filled.DirectionsCar
    "carwash" -> Icons.Filled.Water
    "food" -> Icons.Filled.Restaurant
    "gas" -> Icons.Filled.LocalGasStation
    "bank" -> Icons.Filled.AttachMoney
    "hospital" -> Icons.Filled.Favorite
    "police" -> Icons.Filled.LocalPolice
    "work" -> Icons.Filled.Work
    "shop" -> Icons.Filled.ShoppingCart
    else -> Icons.Filled.Star
}

/** GTA map blip: black disc, coloured ring and glyph. */
@Composable
fun Blip(kind: String, size: Dp = 44.dp) {
    val c = Color(kindOf(kind).color)
    Box(
        Modifier.size(size).background(Color.Black, CircleShape).border(2.dp, c, CircleShape),
        contentAlignment = Alignment.Center,
    ) {
        Icon(kindIcon(kind), null, tint = c, modifier = Modifier.size(size * 0.55f))
    }
}

@Composable
fun Card(modifier: Modifier = Modifier, content: @Composable ColumnScope.() -> Unit) {
    Column(
        modifier.fillMaxWidth().background(Gta.card, RoundedCornerShape(22.dp)).padding(18.dp),
        content = content,
    )
}

@Composable
fun SectionTitle(text: String) {
    Text(
        text.uppercase(), color = Gta.dim, fontSize = 13.sp, fontWeight = FontWeight.SemiBold, letterSpacing = 1.5.sp,
        modifier = Modifier.padding(start = 6.dp, top = 18.dp, bottom = 8.dp),
    )
}
