package com.cak3d.gtawatch

import android.app.Notification
import android.service.notification.NotificationListenerService
import android.service.notification.StatusBarNotification

/** Forwards phone notifications (texts, calls, apps) to the watch. Enabled in Android settings. */
class NotificationRelay : NotificationListenerService() {
    private val recent = LinkedHashMap<String, Long>()

    override fun onNotificationPosted(sbn: StatusBarNotification) {
        if (sbn.packageName == packageName || sbn.isOngoing) return
        val n = sbn.notification
        if (n.flags and Notification.FLAG_GROUP_SUMMARY != 0) return
        val ex = n.extras
        val title = ex.getCharSequence(Notification.EXTRA_TITLE)?.toString() ?: return
        val body = (ex.getCharSequence(Notification.EXTRA_BIG_TEXT) ?: ex.getCharSequence(Notification.EXTRA_TEXT))?.toString() ?: ""
        // apps re-post the same notification constantly; only forward real changes
        val key = "${sbn.packageName}|$title|$body"
        val now = System.currentTimeMillis()
        recent.entries.removeAll { now - it.value > 60_000 }
        if (recent.containsKey(key)) return
        recent[key] = now
        val app = try {
            packageManager.getApplicationLabel(packageManager.getApplicationInfo(sbn.packageName, 0)).toString()
        } catch (_: Exception) {
            sbn.packageName
        }
        WatchService.instance?.forwardNotification(app, title, body, sbn.id)
    }
}
