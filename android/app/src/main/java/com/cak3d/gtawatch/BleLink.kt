package com.cak3d.gtawatch

import android.annotation.SuppressLint
import android.bluetooth.BluetoothDevice
import android.bluetooth.BluetoothGatt
import android.bluetooth.BluetoothGattCallback
import android.bluetooth.BluetoothGattCharacteristic
import android.bluetooth.BluetoothGattDescriptor
import android.bluetooth.BluetoothManager
import android.bluetooth.BluetoothProfile
import android.bluetooth.le.ScanCallback
import android.bluetooth.le.ScanFilter
import android.bluetooth.le.ScanResult
import android.bluetooth.le.ScanSettings
import android.content.Context
import android.os.Build
import android.os.Handler
import android.os.Looper
import android.os.ParcelUuid
import java.util.ArrayDeque

data class FoundWatch(val address: String, val name: String, val rssi: Int)

/**
 * GATT client for the watch. All GATT calls run on the main looper, one operation at a time
 * (Android silently drops overlapping GATT operations — a classic cause of flaky links).
 *
 * Reconnection strategy: after the first link, the connection is re-opened with autoConnect=true,
 * which hands reconnection to the Bluetooth stack: it reconnects by itself whenever the watch is
 * back in range, even with the screen off. A watchdog also retries a direct connect.
 */
@SuppressLint("MissingPermission")
class BleLink(private val ctx: Context, private val listener: Listener) {
    interface Listener {
        fun onReady()
        fun onMessage(type: Int, payload: ByteArray)
        fun onDisconnected()
    }

    private val main = Handler(Looper.getMainLooper())
    private val adapter get() = (ctx.getSystemService(Context.BLUETOOTH_SERVICE) as BluetoothManager).adapter
    private var gatt: BluetoothGatt? = null
    private var rx: BluetoothGattCharacteristic? = null
    private var mtu = 23
    private var ready = false
    private var wantConnected = false
    private var address: String? = null
    private var failures = 0

    private class Outgoing(val type: Int, val payload: ByteArray)
    private val queue = ArrayDeque<Outgoing>()
    private var packets: ArrayDeque<ByteArray>? = null
    private var writeInFlight = false

    private val reassembler = Protocol.Reassembler { t, p -> listener.onMessage(t, p) }

    val isReady get() = ready

    fun connect(addr: String) {
        main.post {
            wantConnected = true
            if (address != addr) closeGatt()
            address = addr
            if (gatt == null) open(autoConnect = false)
        }
    }

    fun disconnect() {
        main.post {
            wantConnected = false
            main.removeCallbacksAndMessages(null)
            closeGatt()
            Store.link.value = LinkState.OFF
        }
    }

    /** Queue a message. A newer GPS/NAV/map/etc. replaces any older unsent one so nothing goes stale. */
    fun send(type: Int, payload: ByteArray) {
        main.post {
            if (type != Protocol.NOTIFY) queue.removeAll { it.type == type }
            while (queue.size > 40) queue.poll()
            queue.add(Outgoing(type, payload))
            pump()
        }
    }

    private fun open(autoConnect: Boolean) {
        val a = address ?: return
        val dev: BluetoothDevice = try {
            adapter.getRemoteDevice(a)
        } catch (e: Exception) {
            return
        }
        Store.link.value = LinkState.CONNECTING
        Store.linkDetail.value = if (autoConnect) "Waiting for watch…" else "Connecting…"
        gatt = dev.connectGatt(ctx, autoConnect, callback, BluetoothDevice.TRANSPORT_LE)
        main.removeCallbacks(watchdog)
        main.postDelayed(watchdog, if (autoConnect) 45_000 else 12_000)
    }

    private val watchdog = Runnable {
        if (wantConnected && !ready) {
            Store.addLog("link watchdog: retrying")
            closeGatt()
            open(autoConnect = false)
        }
    }

    private fun closeGatt() {
        ready = false
        rx = null
        packets = null
        writeInFlight = false
        gatt?.let {
            try {
                it.disconnect()
                it.close()
            } catch (_: Exception) {
            }
        }
        gatt = null
    }

    private fun scheduleReconnect() {
        if (!wantConnected) return
        failures++
        // status 133 and friends: give the stack a moment, then hand off to autoConnect
        val delay = if (failures < 3) 1500L else 4000L
        main.postDelayed({ if (wantConnected && gatt == null) open(autoConnect = failures >= 2) }, delay)
    }

    private val callback = object : BluetoothGattCallback() {
        override fun onConnectionStateChange(g: BluetoothGatt, status: Int, newState: Int) {
            main.post {
                if (g != gatt) return@post
                if (newState == BluetoothProfile.STATE_CONNECTED && status == BluetoothGatt.GATT_SUCCESS) {
                    Store.linkDetail.value = "Setting up…"
                    g.requestConnectionPriority(BluetoothGatt.CONNECTION_PRIORITY_HIGH)
                    if (!g.requestMtu(517)) g.discoverServices()
                } else {
                    val was = ready
                    Store.addLog("link down (status $status)")
                    closeGatt()
                    Store.link.value = if (wantConnected) LinkState.CONNECTING else LinkState.OFF
                    Store.linkDetail.value = if (wantConnected) "Reconnecting…" else ""
                    if (was) listener.onDisconnected()
                    scheduleReconnect()
                }
            }
        }

        override fun onMtuChanged(g: BluetoothGatt, newMtu: Int, status: Int) {
            main.post {
                if (g != gatt) return@post
                mtu = if (status == BluetoothGatt.GATT_SUCCESS) newMtu else 23
                g.discoverServices()
            }
        }

        override fun onServicesDiscovered(g: BluetoothGatt, status: Int) {
            main.post {
                if (g != gatt) return@post
                val svc = g.getService(Protocol.SERVICE)
                val tx = svc?.getCharacteristic(Protocol.TX)
                rx = svc?.getCharacteristic(Protocol.RX)
                if (svc == null || tx == null || rx == null) {
                    Store.addLog("watch service not found")
                    closeGatt()
                    scheduleReconnect()
                    return@post
                }
                g.setCharacteristicNotification(tx, true)
                val cccd = tx.getDescriptor(Protocol.CCCD)
                val v = BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE
                if (Build.VERSION.SDK_INT >= 33) g.writeDescriptor(cccd, v)
                else @Suppress("DEPRECATION") {
                    cccd.value = v
                    g.writeDescriptor(cccd)
                }
            }
        }

        override fun onDescriptorWrite(g: BluetoothGatt, d: BluetoothGattDescriptor, status: Int) {
            main.post {
                if (g != gatt) return@post
                ready = true
                failures = 0
                main.removeCallbacks(watchdog)
                Store.link.value = LinkState.CONNECTED
                Store.linkDetail.value = "MTU $mtu"
                Store.addLog("watch connected (MTU $mtu)")
                // after a reconnect, drop half-sent data and start clean
                packets = null
                writeInFlight = false
                listener.onReady()
                pump()
                pollRssi()
            }
        }

        override fun onCharacteristicWrite(g: BluetoothGatt, c: BluetoothGattCharacteristic, status: Int) {
            main.post {
                if (g != gatt) return@post
                writeInFlight = false
                if (status != BluetoothGatt.GATT_SUCCESS) {
                    Store.addLog("write failed ($status), resending message")
                    packets = null
                }
                pump()
            }
        }

        @Deprecated("pre-33 callback")
        override fun onCharacteristicChanged(g: BluetoothGatt, c: BluetoothGattCharacteristic) {
            @Suppress("DEPRECATION") val v = c.value ?: return
            main.post { reassembler.feed(v) }
        }

        override fun onCharacteristicChanged(g: BluetoothGatt, c: BluetoothGattCharacteristic, value: ByteArray) {
            main.post { reassembler.feed(value) }
        }

        override fun onReadRemoteRssi(g: BluetoothGatt, rssi: Int, status: Int) {
            if (status == BluetoothGatt.GATT_SUCCESS) Store.rssi.value = rssi
        }
    }

    private fun pollRssi() {
        main.postDelayed({
            if (ready) {
                gatt?.readRemoteRssi()
                pollRssi()
            }
        }, 5000)
    }

    private fun pump() {
        if (!ready || writeInFlight) return
        val g = gatt ?: return
        val c = rx ?: return
        if (packets.isNullOrEmpty()) {
            val m = queue.poll() ?: return
            packets = ArrayDeque(Protocol.packetize(m.type, m.payload, mtu))
        }
        val p = packets!!.poll() ?: return
        writeInFlight = true
        val ok = if (Build.VERSION.SDK_INT >= 33) {
            g.writeCharacteristic(c, p, BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT) == BluetoothGatt.GATT_SUCCESS
        } else @Suppress("DEPRECATION") {
            c.writeType = BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT
            c.value = p
            g.writeCharacteristic(c)
        }
        if (!ok) {
            // stack busy: retry shortly with the same packet
            writeInFlight = false
            packets!!.addFirst(p)
            main.postDelayed({ pump() }, 30)
        }
    }

    // ---------------------------------------------------------------- scanning
    private var scanCb: ScanCallback? = null

    fun scan(onFound: (FoundWatch) -> Unit) {
        stopScan()
        val scanner = adapter?.bluetoothLeScanner ?: return
        val cb = object : ScanCallback() {
            override fun onScanResult(callbackType: Int, r: ScanResult) {
                onFound(FoundWatch(r.device.address, r.scanRecord?.deviceName ?: r.device.name ?: "GTA-Watch", r.rssi))
            }
        }
        scanCb = cb
        val filter = ScanFilter.Builder().setServiceUuid(ParcelUuid(Protocol.SERVICE)).build()
        val settings = ScanSettings.Builder().setScanMode(ScanSettings.SCAN_MODE_LOW_LATENCY).build()
        scanner.startScan(listOf(filter), settings, cb)
        main.postDelayed({ stopScan() }, 20_000)
    }

    fun stopScan() {
        scanCb?.let { adapter?.bluetoothLeScanner?.stopScan(it) }
        scanCb = null
    }
}
