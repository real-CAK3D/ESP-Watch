package com.cak3d.gtawatch

import android.app.Application
import android.app.NotificationChannel
import android.app.NotificationManager
import org.maplibre.android.MapLibre

class GtaWatchApp : Application() {
    companion object {
        const val CH_LINK = "link"
        const val CH_ALERT = "alert"
    }

    override fun onCreate() {
        super.onCreate()
        Store.init(this)
        MapLibre.getInstance(this)
        val nm = getSystemService(NotificationManager::class.java)
        nm.createNotificationChannel(NotificationChannel(CH_LINK, "Watch connection", NotificationManager.IMPORTANCE_LOW))
        nm.createNotificationChannel(NotificationChannel(CH_ALERT, "Watch alerts", NotificationManager.IMPORTANCE_HIGH))
    }
}
