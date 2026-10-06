package com.cak3d.gtawatch

import android.annotation.SuppressLint
import android.bluetooth.BluetoothDevice
import android.bluetooth.le.ScanFilter
import android.bluetooth.le.ScanResult
import android.companion.AssociationInfo
import android.companion.AssociationRequest
import android.companion.BluetoothLeDeviceFilter
import android.companion.CompanionDeviceManager
import android.companion.CompanionDeviceService
import android.content.Context
import android.content.Intent
import android.content.IntentSender
import android.os.Build
import android.os.ParcelUuid
import androidx.annotation.RequiresApi

/**
 * Pairs the watch the way Galaxy Wearable / Wear OS do: Android's Companion Device Manager shows the
 * system pairing sheet, and the association lets Android keep this app alive for the watch, start the
 * link service from the background, and wake us when the watch comes into range.
 */
object Companion {
    private fun cdm(ctx: Context) = ctx.getSystemService(Context.COMPANION_DEVICE_SERVICE) as CompanionDeviceManager

    fun associate(ctx: Context, onIntent: (IntentSender) -> Unit, onError: (String) -> Unit) {
        val filter = BluetoothLeDeviceFilter.Builder()
            .setScanFilter(ScanFilter.Builder().setServiceUuid(ParcelUuid(Protocol.SERVICE)).build())
            .build()
        val req = AssociationRequest.Builder().addDeviceFilter(filter).setSingleDevice(false).build()
        val cb = object : CompanionDeviceManager.Callback() {
            override fun onAssociationPending(sender: IntentSender) = onIntent(sender)

            @Deprecated("pre-33")
            override fun onDeviceFound(sender: IntentSender) = onIntent(sender)

            override fun onFailure(error: CharSequence?) = onError(error?.toString() ?: "Pairing cancelled")
        }
        if (Build.VERSION.SDK_INT >= 33) cdm(ctx).associate(req, ctx.mainExecutor, cb)
        else @Suppress("DEPRECATION") cdm(ctx).associate(req, cb, null)
    }

    /** Pull the chosen watch out of the pairing sheet's result. Returns (address, name). */
    @SuppressLint("MissingPermission")
    fun parseResult(data: Intent?): Pair<String, String>? {
        if (data == null) return null
        if (Build.VERSION.SDK_INT >= 33) {
            val info = data.getParcelableExtra(CompanionDeviceManager.EXTRA_ASSOCIATION, AssociationInfo::class.java)
            val mac = info?.deviceMacAddress?.toString()?.uppercase()
            if (mac != null) return mac to (info.displayName?.toString() ?: "GTA-Watch")
        }
        @Suppress("DEPRECATION")
        return when (val dev = data.getParcelableExtra<android.os.Parcelable>(CompanionDeviceManager.EXTRA_DEVICE)) {
            is ScanResult -> dev.device.address to (dev.scanRecord?.deviceName ?: "GTA-Watch")
            is BluetoothDevice -> dev.address to (dev.name ?: "GTA-Watch")
            else -> null
        }
    }

    /** Ask Android to tell CompanionService when the watch appears / disappears. */
    fun observe(ctx: Context, address: String) {
        if (Build.VERSION.SDK_INT >= 31) {
            try {
                @Suppress("DEPRECATION")
                cdm(ctx).startObservingDevicePresence(address)
            } catch (e: Exception) {
                Store.addLog("presence observe failed: ${e.message}")
            }
        }
    }

    fun isAssociated(ctx: Context, address: String?): Boolean {
        if (address == null) return false
        return try {
            if (Build.VERSION.SDK_INT >= 33) cdm(ctx).myAssociations.any { it.deviceMacAddress?.toString().equals(address, true) }
            else @Suppress("DEPRECATION") cdm(ctx).associations.any { it.equals(address, true) }
        } catch (_: Exception) {
            false
        }
    }
}

/** Woken by Android when the paired watch comes into Bluetooth range. */
@RequiresApi(31)
class CompanionService : CompanionDeviceService() {
    @Deprecated("API 31-32 callback")
    override fun onDeviceAppeared(address: String) = wake()

    override fun onDeviceAppeared(info: AssociationInfo) = wake()

    private fun wake() {
        Store.addLog("watch in range")
        try {
            WatchService.start(this)
        } catch (e: Exception) {
            Store.addLog("could not start link: ${e.message}")
        }
    }
}
