package com.rsvpnano.app

import com.rsvpnano.models.RememberedNano
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.StateFlow

interface NanoWifiConnector {
    val snapshot: StateFlow<NanoWifiSnapshot>
    val events: Flow<NanoWifiEvent>

    fun start()
    fun stop()
    fun refreshSnapshot()
    suspend fun discoverNanos(): List<NanoEndpoint>
    fun requestNanoNetwork(
        rememberedNano: RememberedNano? = null,
    ): NanoWifiRequestResult
    suspend fun <T> withNanoNetwork(block: suspend () -> T): T
}

data class NanoEndpoint(
    val baseUrl: String,
    val nano: RememberedNano,
)

sealed interface NanoConnectionState {
    val currentNano: RememberedNano?
    val transport: NanoConnectionTransport?

    data object Disconnected : NanoConnectionState {
        override val currentNano: RememberedNano? = null
        override val transport: NanoConnectionTransport? = null
    }

    data class Requesting(
        val rememberedNano: RememberedNano?,
    ) : NanoConnectionState {
        override val currentNano: RememberedNano? = rememberedNano
        override val transport: NanoConnectionTransport = NanoConnectionTransport.AccessPoint
    }

    data class WifiAttached(
        override val currentNano: RememberedNano?,
    ) : NanoConnectionState {
        override val transport: NanoConnectionTransport = NanoConnectionTransport.AccessPoint
    }

    data class CheckingReader(
        override val currentNano: RememberedNano?,
        override val transport: NanoConnectionTransport,
    ) : NanoConnectionState

    data class ReaderConnected(
        override val currentNano: RememberedNano?,
        override val transport: NanoConnectionTransport,
    ) : NanoConnectionState
}

enum class NanoConnectionTransport {
    LocalNetwork,
    AccessPoint,
}

data class NanoWifiSnapshot(
    val currentNano: RememberedNano? = null,
    val isAttached: Boolean = false,
    val isRequesting: Boolean = false,
) {
    fun toConnectionState(previous: NanoConnectionState): NanoConnectionState {
        val identity = currentNano ?: previous.currentNano
        return when {
            isRequesting -> NanoConnectionState.Requesting(identity)
            isAttached -> NanoConnectionState.WifiAttached(identity)
            else -> NanoConnectionState.Disconnected
        }
    }
}

sealed interface NanoWifiRequestResult {
    data object Started : NanoWifiRequestResult
    data object AlreadyAttached : NanoWifiRequestResult
    data object AlreadyRequesting : NanoWifiRequestResult
    data object MissingPermissions : NanoWifiRequestResult
    data class Failed(val reason: String) : NanoWifiRequestResult
}

sealed interface NanoWifiEvent {
    data object RequestUnavailable : NanoWifiEvent
}

val NanoConnectionState.isConnected: Boolean
    get() = this is NanoConnectionState.ReaderConnected

val NanoConnectionState.isWifiAttached: Boolean
    get() = this is NanoConnectionState.WifiAttached ||
        (this is NanoConnectionState.CheckingReader && transport == NanoConnectionTransport.AccessPoint) ||
        (this is NanoConnectionState.ReaderConnected && transport == NanoConnectionTransport.AccessPoint)

val NanoConnectionState.isCheckingReader: Boolean
    get() = this is NanoConnectionState.CheckingReader

val NanoConnectionState.isRequesting: Boolean
    get() = this is NanoConnectionState.Requesting
