package com.cak3d.gtawatch

import android.graphics.Bitmap
import android.graphics.BitmapFactory
import java.io.File
import java.net.URL
import java.nio.ByteBuffer
import java.nio.ByteOrder
import kotlin.math.asinh
import kotlin.math.atan
import kotlin.math.atan2
import kotlin.math.cos
import kotlin.math.hypot
import kotlin.math.roundToInt
import kotlin.math.sin
import kotlin.math.tan

/**
 * Hill-shade grid for the watch minimap (MSG_TERRAIN, see firmware/src/terrain.h), computed from the free
 * AWS Terrain Tiles (terrarium encoding). Port of tools/phonesim.py build_terrain().
 */
object Terrain {
    private const val URL_FMT = "https://s3.amazonaws.com/elevation-tiles-prod/terrarium/%d/%d/%d.png"

    fun build(lat0: Double, lon0: Double, cacheDir: File, radius: Double = 1600.0, n: Int = 128, z: Int = 13): ByteArray {
        val cell = 2 * radius / n
        val ml = MapPacker.mLon(lat0)
        val tiles = HashMap<Long, Bitmap>()
        cacheDir.mkdirs()

        fun tile(tx: Int, ty: Int): Bitmap = tiles.getOrPut(tx.toLong() shl 32 or ty.toLong()) {
            val f = File(cacheDir, "dem_${z}_${tx}_$ty.png")
            if (!f.exists()) URL(URL_FMT.format(z, tx, ty)).openStream().use { inp -> f.outputStream().use { inp.copyTo(it) } }
            BitmapFactory.decodeFile(f.absolutePath) ?: throw java.io.IOException("bad elevation tile")
        }

        fun elev(lat: Double, lon: Double): Double {
            val nn = (1 shl z).toDouble()
            val fx = (lon + 180) / 360 * nn
            val fy = (1 - asinh(tan(Math.toRadians(lat))) / Math.PI) / 2 * nn
            val tx = fx.toInt()
            val ty = fy.toInt()
            val px = tile(tx, ty).getPixel(((fx - tx) * 256).toInt().coerceIn(0, 255), ((fy - ty) * 256).toInt().coerceIn(0, 255))
            val r = (px shr 16) and 0xFF
            val g = (px shr 8) and 0xFF
            val b = px and 0xFF
            return r * 256.0 + g + b / 256.0 - 32768
        }

        val e = Array(n + 2) { j -> DoubleArray(n + 2) { i -> elev(lat0 + (radius - (j - 0.5) * cell) / MapPacker.M_LAT, lon0 + (-radius + (i - 0.5) * cell) / ml) } }
        val az = Math.toRadians(315.0)
        val alt = Math.toRadians(45.0)
        val flat = sin(alt)
        val exaggerate = 2.5
        val out = ByteBuffer.allocate(12 + n * n).order(ByteOrder.LITTLE_ENDIAN)
        out.putInt((lat0 * 1e7).roundToInt()).putInt((lon0 * 1e7).roundToInt()).putShort(n.toShort()).putShort((cell * 10).roundToInt().toShort())
        for (j in 1..n) for (i in 1..n) {
            val dzdx = (e[j][i + 1] - e[j][i - 1]) / (2 * cell) * exaggerate
            val dzdy = (e[j - 1][i] - e[j + 1][i]) / (2 * cell) * exaggerate
            val slope = atan(hypot(dzdx, dzdy))
            val aspect = atan2(-dzdx, -dzdy)
            val hs = sin(alt) * cos(slope) + cos(alt) * sin(slope) * cos(az - aspect)
            out.put((128 + (hs - flat) * 255).roundToInt().coerceIn(0, 255).toByte())
        }
        return out.array()
    }
}
