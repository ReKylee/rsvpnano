package com.rsvpnano

import com.rsvpnano.app.NanoConnectionState
import com.rsvpnano.app.NanoConnectionTransport
import com.rsvpnano.app.NanoWifiSnapshot
import com.rsvpnano.models.RememberedNano
import kotlin.test.Test
import kotlin.test.assertEquals

class NanoConnectionModelsTest {
    private val nano = RememberedNano("RSVP-Nano-123456")

    @Test
    fun attachedAccessPointSnapshotPreservesApiConnection() {
        val connected = NanoConnectionState.ReaderConnected(nano, NanoConnectionTransport.AccessPoint)

        assertEquals(
            connected,
            NanoWifiSnapshot(currentNano = nano, isAttached = true).toConnectionState(connected),
        )
    }

    @Test
    fun nanoAccessPointSnapshotSwitchesFromLocalTransport() {
        val connected = NanoConnectionState.ReaderConnected(nano, NanoConnectionTransport.LocalNetwork)

        assertEquals(connected, NanoWifiSnapshot().toConnectionState(connected))
        assertEquals(
            NanoConnectionState.WifiAttached(nano),
            NanoWifiSnapshot(currentNano = nano, isAttached = true).toConnectionState(connected),
        )
    }

    @Test
    fun losingAccessPointClearsAccessPointConnection() {
        val connected = NanoConnectionState.ReaderConnected(nano, NanoConnectionTransport.AccessPoint)

        assertEquals(NanoConnectionState.Disconnected, NanoWifiSnapshot().toConnectionState(connected))
    }
}
