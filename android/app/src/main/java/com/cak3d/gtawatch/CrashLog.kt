package com.cak3d.gtawatch

import android.content.Context
import android.os.Build
import java.io.File
import java.io.PrintWriter
import java.io.StringWriter

/**
 * Saves any uncaught exception to a file so the next launch can show it (Watch tab) — we can't
 * attach a debugger to the phone, so this is how crashes get diagnosed.
 */
object CrashLog {
    private lateinit var file: File

    fun install(ctx: Context) {
        file = File(ctx.filesDir, "last_crash.txt")
        val previous = Thread.getDefaultUncaughtExceptionHandler()
        Thread.setDefaultUncaughtExceptionHandler { thread, e ->
            try {
                val sw = StringWriter()
                e.printStackTrace(PrintWriter(sw))
                val version = try {
                    ctx.packageManager.getPackageInfo(ctx.packageName, 0).versionName
                } catch (_: Exception) {
                    "?"
                }
                file.writeText(
                    "GTA-Watch $version on ${Build.MANUFACTURER} ${Build.MODEL}, Android ${Build.VERSION.RELEASE} (API ${Build.VERSION.SDK_INT})\n" +
                        "${java.util.Date()} thread=${thread.name}\n\n$sw",
                )
            } catch (_: Exception) {
            }
            previous?.uncaughtException(thread, e)
        }
    }

    fun read(): String? = if (::file.isInitialized && file.exists()) file.readText() else null

    fun clear() {
        if (::file.isInitialized) file.delete()
    }
}
