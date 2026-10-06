package com.cak3d.gtawatch

import java.util.UUID

/** Mirror of firmware/src/proto.h. See docs/PROTOCOL.md. */
object Protocol {
    val SERVICE: UUID = UUID.fromString("6f9a0001-5a7c-4d2b-9e1f-47a5c0de0001")
    val RX: UUID = UUID.fromString("6f9a0002-5a7c-4d2b-9e1f-47a5c0de0001")  // phone -> watch (write)
    val TX: UUID = UUID.fromString("6f9a0003-5a7c-4d2b-9e1f-47a5c0de0001")  // watch -> phone (notify)
    val CCCD: UUID = UUID.fromString("00002902-0000-1000-8000-00805f9b34fb")

    const val FIRST = 0x01
    const val LAST = 0x02

    // phone -> watch
    const val TIME = 0x01
    const val WEATHER = 0x02
    const val GPS = 0x03
    const val MAP = 0x04
    const val NAV = 0x05
    const val ROUTE = 0x06
    const val NOTIFY = 0x07
    const val PLACES = 0x08
    const val SETTINGS = 0x09
    const val PHONE = 0x0A
    const val PING = 0x0B
    const val NOTIFY_CLEAR = 0x0C
    const val TERRAIN = 0x0D
    const val OTA_BEGIN = 0x10
    const val OTA_DATA = 0x11
    const val OTA_END = 0x12

    // watch -> phone
    const val TELEMETRY = 0x81
    const val EVENT = 0x82
    const val LOG = 0x83
    const val OTA_STATUS = 0x84

    /** Splits a message into BLE packets of at most [mtu]-3 bytes: [type][flags][payload...]. */
    fun packetize(type: Int, payload: ByteArray, mtu: Int): List<ByteArray> {
        val chunk = (mtu - 3 - 2).coerceAtLeast(18)
        val out = ArrayList<ByteArray>()
        var off = 0
        do {
            val n = minOf(chunk, payload.size - off)
            val flags = (if (off == 0) FIRST else 0) or (if (off + n >= payload.size) LAST else 0)
            val p = ByteArray(n + 2)
            p[0] = type.toByte()
            p[1] = flags.toByte()
            System.arraycopy(payload, off, p, 2, n)
            out.add(p)
            off += n
        } while (off < payload.size)
        return out
    }

    /** Reassembles notifications from the watch. */
    class Reassembler(private val onMessage: (Int, ByteArray) -> Unit) {
        private var type = -1
        private val buf = java.io.ByteArrayOutputStream()

        fun feed(packet: ByteArray) {
            if (packet.size < 2) return
            val t = packet[0].toInt() and 0xFF
            val flags = packet[1].toInt()
            if (flags and FIRST != 0) {
                type = t
                buf.reset()
            }
            if (t != type) return
            buf.write(packet, 2, packet.size - 2)
            if (flags and LAST != 0) {
                onMessage(type, buf.toByteArray())
                type = -1
            }
        }
    }
}
