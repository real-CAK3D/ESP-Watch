package com.cak3d.gtawatch

import android.app.PendingIntent
import android.content.Context
import android.content.Intent
import android.content.pm.PackageInstaller
import org.json.JSONObject
import java.io.File
import java.net.HttpURLConnection
import java.net.URL

/**
 * Updates from the GitHub Releases page (real-CAK3D/ESP-Watch): the app installs a newer APK through
 * Android's package installer, and new watch firmware is sent to the watch over Bluetooth (OTA).
 */
object Updater {
    private const val API = "https://api.github.com/repos/real-CAK3D/ESP-Watch/releases/latest"

    data class Release(val version: String, val notes: String, val apkUrl: String?, val firmwareUrl: String?, val page: String)

    fun appVersion(ctx: Context): String = ctx.packageManager.getPackageInfo(ctx.packageName, 0).versionName ?: "0"

    /** "2.10.1" > "2.9" etc. */
    fun newer(a: String, b: String): Boolean {
        val x = a.trimStart('v').split('.', '-').map { it.toIntOrNull() ?: 0 }
        val y = b.trimStart('v').split('.', '-').map { it.toIntOrNull() ?: 0 }
        for (i in 0 until maxOf(x.size, y.size)) {
            val d = x.getOrElse(i) { 0 } - y.getOrElse(i) { 0 }
            if (d != 0) return d > 0
        }
        return false
    }

    fun latest(): Release {
        val o = JSONObject(Net.get(API))
        val assets = o.getJSONArray("assets")
        var apk: String? = null
        var fw: String? = null
        for (i in 0 until assets.length()) {
            val a = assets.getJSONObject(i)
            val name = a.getString("name")
            val url = a.getString("browser_download_url")
            if (name.endsWith(".apk")) apk = url
            if (name.contains("app-0x10000") && name.endsWith(".bin")) fw = url
        }
        return Release(o.getString("tag_name").trimStart('v'), o.optString("body"), apk, fw, o.getString("html_url"))
    }

    fun download(url: String, dest: File, onProgress: (Int) -> Unit): File {
        var u = URL(url)
        var c: HttpURLConnection
        while (true) {  // GitHub release assets redirect to a CDN
            c = u.openConnection() as HttpURLConnection
            c.instanceFollowRedirects = false
            c.setRequestProperty("User-Agent", "GTA-Watch")
            if (c.responseCode in 300..399) {
                u = URL(c.getHeaderField("Location"))
                c.disconnect()
                continue
            }
            break
        }
        val total = c.contentLengthLong
        c.inputStream.use { inp ->
            dest.outputStream().use { out ->
                val buf = ByteArray(64 * 1024)
                var done = 0L
                while (true) {
                    val n = inp.read(buf)
                    if (n < 0) break
                    out.write(buf, 0, n)
                    done += n
                    if (total > 0) onProgress((done * 100 / total).toInt())
                }
            }
        }
        return dest
    }

    /** Hands the APK to Android's installer; the system shows its own "Update this app?" prompt. */
    fun installApk(ctx: Context, apk: File) {
        val pi = ctx.packageManager.packageInstaller
        val params = PackageInstaller.SessionParams(PackageInstaller.SessionParams.MODE_FULL_INSTALL)
        params.setAppPackageName(ctx.packageName)
        val id = pi.createSession(params)
        pi.openSession(id).use { s ->
            s.openWrite("base.apk", 0, apk.length()).use { out -> apk.inputStream().use { it.copyTo(out) } ; s.fsync(out) }
            val status = PendingIntent.getBroadcast(ctx, id, Intent(ctx, InstallReceiver::class.java),
                PendingIntent.FLAG_MUTABLE or PendingIntent.FLAG_UPDATE_CURRENT)
            s.commit(status.intentSender)
        }
    }
}

/** Receives the package installer's status; launches the confirmation screen when Android asks for it. */
class InstallReceiver : android.content.BroadcastReceiver() {
    override fun onReceive(ctx: Context, intent: Intent) {
        when (intent.getIntExtra(PackageInstaller.EXTRA_STATUS, -999)) {
            PackageInstaller.STATUS_PENDING_USER_ACTION -> {
                @Suppress("DEPRECATION")
                val confirm = intent.getParcelableExtra<Intent>(Intent.EXTRA_INTENT) ?: return
                ctx.startActivity(confirm.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK))
            }
            PackageInstaller.STATUS_SUCCESS -> Store.updateStatus.value = "Updated"
            else -> Store.updateStatus.value = "Install failed: ${intent.getStringExtra(PackageInstaller.EXTRA_STATUS_MESSAGE)}"
        }
    }
}
