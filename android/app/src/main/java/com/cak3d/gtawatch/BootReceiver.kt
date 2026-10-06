package com.cak3d.gtawatch

import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent

/** Reconnects to the watch after the phone restarts. */
class BootReceiver : BroadcastReceiver() {
    override fun onReceive(ctx: Context, intent: Intent) {
        if (intent.action == Intent.ACTION_BOOT_COMPLETED && Store.deviceAddress != null) {
            try {
                WatchService.start(ctx)
            } catch (_: Exception) {
            }
        }
    }
}
