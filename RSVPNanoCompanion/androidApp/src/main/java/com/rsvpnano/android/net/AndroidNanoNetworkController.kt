package com.rsvpnano.android.net

import android.content.Context
import android.content.pm.PackageManager
import android.net.ConnectivityManager
import android.net.Network
import android.net.NetworkCapabilities
import android.net.NetworkRequest
import android.net.nsd.NsdManager
import android.net.nsd.NsdServiceInfo
import android.net.wifi.WifiInfo
import android.net.wifi.WifiNetworkSpecifier
import android.os.PatternMatcher
import com.rsvpnano.app.NanoWifiConnector
import com.rsvpnano.app.NanoEndpoint
import com.rsvpnano.app.NanoWifiEvent
import com.rsvpnano.app.NanoWifiIdentity
import com.rsvpnano.app.NanoWifiRequestResult
import com.rsvpnano.app.NanoWifiSnapshot
import com.rsvpnano.models.RememberedNano
import kotlinx.coroutines.flow.MutableSharedFlow
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.SharedFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.update
import kotlinx.coroutines.suspendCancellableCoroutine
import kotlinx.coroutines.withTimeoutOrNull
import kotlin.coroutines.resume
import kotlin.coroutines.resumeWithException
import java.net.InetAddress
import java.net.Socket
import javax.net.SocketFactory

class AndroidNanoNetworkController(
    context: Context,
) : NanoWifiConnector {
    private val appContext = context.applicationContext
    private val connectivityManager = appContext.getSystemService(Context.CONNECTIVITY_SERVICE) as ConnectivityManager
    private val nsdManager = appContext.getSystemService(Context.NSD_SERVICE) as NsdManager
    private val _snapshot = MutableStateFlow(NanoWifiSnapshot())
    private val _events = MutableSharedFlow<NanoWifiEvent>(extraBufferCapacity = 4)
    override val snapshot: StateFlow<NanoWifiSnapshot> = _snapshot
    override val events: SharedFlow<NanoWifiEvent> = _events
    private var monitorCallback: ConnectivityManager.NetworkCallback? = null
    private var requestCallback: ConnectivityManager.NetworkCallback? = null
    private var requestedNetwork: Network? = null
    @Volatile
    private var currentNetwork: Network? = null
    val socketFactory: SocketFactory = NetworkSocketFactory { currentNetwork }

    override fun start() {
        if (monitorCallback != null) return
        val callback = object : ConnectivityManager.NetworkCallback(
            ConnectivityManager.NetworkCallback.FLAG_INCLUDE_LOCATION_INFO,
        ) {
            override fun onAvailable(network: Network) = Unit

            override fun onLost(network: Network) {
                clearIfCurrent(network)
            }

            override fun onCapabilitiesChanged(network: Network, networkCapabilities: NetworkCapabilities) {
                updateFromCapabilities(network, networkCapabilities, source = NetworkEventSource.Monitor)
            }
        }
        monitorCallback = callback
        connectivityManager.registerNetworkCallback(
            NetworkRequest.Builder()
                .addTransportType(NetworkCapabilities.TRANSPORT_WIFI)
                .build(),
            callback,
        )
        refreshSnapshot()
    }

    override fun stop() {
        monitorCallback?.let { runCatching { connectivityManager.unregisterNetworkCallback(it) } }
        monitorCallback = null
        cancelNanoRequest()
        currentNetwork = null
        _snapshot.value = NanoWifiSnapshot()
    }

    override fun requestNanoNetwork(
        rememberedNano: RememberedNano?,
    ): NanoWifiRequestResult {
        val current = snapshot.value
        if (current.isAttached) return NanoWifiRequestResult.AlreadyAttached
        if (current.isRequesting) return NanoWifiRequestResult.AlreadyRequesting
        if (!hasRequiredPermissions()) {
            return NanoWifiRequestResult.MissingPermissions
        }

        cancelNanoRequest()
        return runCatching {
            val request = nanoNetworkRequest(rememberedNano)
            val callback = nanoRequestCallback()
            requestCallback = callback
            markRequestStarted(rememberedNano = rememberedNano)
            connectivityManager.requestNetwork(request, callback)
            NanoWifiRequestResult.Started
        }.onFailure {
            requestCallback = null
            requestedNetwork = null
            markRequestStopped()
        }.getOrElse { error ->
            NanoWifiRequestResult.Failed(error.message ?: error::class.simpleName ?: "Android rejected the Wi-Fi scan request.")
        }
    }

    private fun nanoNetworkRequest(rememberedNano: RememberedNano?): NetworkRequest {
        val specifier = WifiNetworkSpecifier.Builder().apply {
            if (rememberedNano != null) {
                setSsid(rememberedNano.ssid)
            } else {
                setSsidPattern(PatternMatcher(NanoWifiIdentity.SSID_PREFIX, PatternMatcher.PATTERN_PREFIX))
            }
        }.build()
        return NetworkRequest.Builder()
            .addTransportType(NetworkCapabilities.TRANSPORT_WIFI)
            .removeCapability(NetworkCapabilities.NET_CAPABILITY_INTERNET)
            .setNetworkSpecifier(specifier)
            .build()
    }

    private fun nanoRequestCallback(): ConnectivityManager.NetworkCallback {
        return object : ConnectivityManager.NetworkCallback(
            ConnectivityManager.NetworkCallback.FLAG_INCLUDE_LOCATION_INFO,
        ) {
                override fun onAvailable(network: Network) {
                    requestedNetwork = network
                }

                override fun onCapabilitiesChanged(network: Network, networkCapabilities: NetworkCapabilities) {
                    updateFromCapabilities(network, networkCapabilities, source = NetworkEventSource.Request)
                }

                override fun onUnavailable() {
                    cancelNanoRequest()
                    publish(NanoWifiEvent.RequestUnavailable)
                }

                override fun onLost(network: Network) {
                    if (requestedNetwork == network) {
                        requestedNetwork = null
                    }
                    clearIfCurrent(network)
                }
        }
    }

    private fun markRequestStarted(rememberedNano: RememberedNano?) {
        _snapshot.update {
            it.copy(
                currentNano = rememberedNano,
                isRequesting = true,
            )
        }
    }

    private fun markRequestStopped() {
        _snapshot.update {
            it.copy(
                isRequesting = false,
            )
        }
    }

    private fun publish(event: NanoWifiEvent) {
        _events.tryEmit(event)
    }

    private fun cancelNanoRequest() {
        requestCallback?.let { runCatching { connectivityManager.unregisterNetworkCallback(it) } }
        requestCallback = null
        requestedNetwork = null
        _snapshot.update { it.copy(isRequesting = false) }
    }

    override fun refreshSnapshot() {
        val network = currentNetwork
        if (network == null) {
            _snapshot.update {
                if (it.isAttached) NanoWifiSnapshot() else it
            }
            return
        }

        val capabilities = connectivityManager.getNetworkCapabilities(network)
        if (capabilities == null) {
            _snapshot.update {
                currentNetwork = null
                NanoWifiSnapshot()
            }
            return
        }

        updateFromCapabilities(network, capabilities, source = NetworkEventSource.Monitor)
    }

    fun hasRequiredPermissions(): Boolean {
        return appContext.checkSelfPermission(android.Manifest.permission.NEARBY_WIFI_DEVICES) ==
            PackageManager.PERMISSION_GRANTED
    }

    override suspend fun discoverNanos(): List<NanoEndpoint> {
        val endpoints = linkedMapOf<String, NanoEndpoint>()
        withTimeoutOrNull(DISCOVERY_TIMEOUT_MS) {
            suspendCancellableCoroutine<Unit> { continuation ->
                val callbacks = mutableMapOf<String, NsdManager.ServiceInfoCallback>()
                var discoveryStarted = false
                var completed = false
                lateinit var listener: NsdManager.DiscoveryListener

                fun unregisterService(name: String) {
                    callbacks.remove(name)?.let { callback ->
                        runCatching { nsdManager.unregisterServiceInfoCallback(callback) }
                    }
                }

                fun cleanup() {
                    if (discoveryStarted) {
                        discoveryStarted = false
                        runCatching { nsdManager.stopServiceDiscovery(listener) }
                    }
                    val registeredCallbacks = callbacks.values.toList()
                    callbacks.clear()
                    registeredCallbacks.forEach { callback ->
                        runCatching { nsdManager.unregisterServiceInfoCallback(callback) }
                    }
                }

                fun finish() {
                    if (completed) return
                    completed = true
                    cleanup()
                    if (continuation.isActive) continuation.resume(Unit)
                }

                fun fail(errorCode: Int) {
                    if (completed) return
                    completed = true
                    cleanup()
                    if (continuation.isActive) {
                        continuation.resumeWithException(
                            IllegalStateException("Local discovery failed with Android NSD error $errorCode."),
                        )
                    }
                }

                listener = object : NsdManager.DiscoveryListener {
                    override fun onDiscoveryStarted(serviceType: String) {
                        discoveryStarted = true
                        if (!continuation.isActive) cleanup()
                    }

                    override fun onServiceFound(service: NsdServiceInfo) {
                        val name = service.serviceName
                        if (name in callbacks || name in endpoints) return
                        val callback = object : NsdManager.ServiceInfoCallback {
                            override fun onServiceInfoCallbackRegistrationFailed(errorCode: Int) {
                                callbacks.remove(name)
                            }

                            override fun onServiceUpdated(serviceInfo: NsdServiceInfo) {
                                val host = serviceInfo.hostAddresses
                                    .firstOrNull { it.address.size == 4 }
                                    ?.hostAddress
                                    ?: serviceInfo.hostAddresses.firstOrNull()?.hostAddress
                                    ?: return
                                val address = if (':' in host) "[${host.replace("%", "%25")}]" else host
                                val port = serviceInfo.port.takeIf { it != 80 }?.let { ":$it" }.orEmpty()
                                endpoints[name] = NanoEndpoint(
                                    baseUrl = "http://$address$port",
                                    nano = RememberedNano(name),
                                )
                                unregisterService(name)
                            }

                            override fun onServiceLost() {
                                endpoints.remove(name)
                                unregisterService(name)
                            }

                            override fun onServiceInfoCallbackUnregistered() = Unit
                        }
                        callbacks[name] = callback
                        runCatching {
                            nsdManager.registerServiceInfoCallback(service, appContext.mainExecutor, callback)
                        }.onFailure {
                            callbacks.remove(name)
                        }
                    }

                    override fun onServiceLost(service: NsdServiceInfo) {
                        endpoints.remove(service.serviceName)
                        unregisterService(service.serviceName)
                    }

                    override fun onDiscoveryStopped(serviceType: String) = finish()
                    override fun onStartDiscoveryFailed(serviceType: String, errorCode: Int) = fail(errorCode)
                    override fun onStopDiscoveryFailed(serviceType: String, errorCode: Int) = Unit
                }

                continuation.invokeOnCancellation { cleanup() }
                nsdManager.discoverServices(SERVICE_TYPE, NsdManager.PROTOCOL_DNS_SD, listener)
            }
        }
        return endpoints.values.sortedBy { it.nano.ssid }
    }

    private fun updateFromCapabilities(
        network: Network,
        capabilities: NetworkCapabilities,
        source: NetworkEventSource,
    ) {
        if (!capabilities.hasTransport(NetworkCapabilities.TRANSPORT_WIFI)) return
        val ssid = capabilities.nanoSsidOrNull()
        val wasNano = currentNetwork == network && snapshot.value.isAttached
        val isNano = NanoWifiIdentity.isNanoSsid(ssid) || source == NetworkEventSource.Request || wasNano
        if (!isNano && currentNetwork != network) return

        currentNetwork = network
        val identity = NanoWifiIdentity.rememberedNanoOrNull(ssid) ?: snapshot.value.currentNano
        _snapshot.value = NanoWifiSnapshot(
            currentNano = identity,
            isAttached = isNano,
            isRequesting = false,
        )
    }

    private fun clearIfCurrent(network: Network) {
        if (currentNetwork == network) {
            currentNetwork = null
            _snapshot.value = NanoWifiSnapshot()
        }
    }

    private fun NetworkCapabilities.nanoSsidOrNull(): String? {
        return wifiInfoOrNull()?.ssid?.cleanSsid()
    }

    private fun NetworkCapabilities.wifiInfoOrNull(): WifiInfo? {
        return transportInfo as? WifiInfo
    }

    private fun String.cleanSsid(): String? {
        return NanoWifiIdentity.cleanSsid(this)
    }

    companion object {
        private const val DISCOVERY_TIMEOUT_MS = 2_500L
        private const val SERVICE_TYPE = "_rsvpnano._tcp."
    }

    private enum class NetworkEventSource {
        Monitor,
        Request,
    }
}

private class NetworkSocketFactory(
    private val network: () -> Network?,
) : SocketFactory() {
    private fun delegate(): SocketFactory = network()?.socketFactory ?: getDefault()

    override fun createSocket(): Socket = delegate().createSocket()

    override fun createSocket(host: String, port: Int): Socket = delegate().createSocket(host, port)

    override fun createSocket(
        host: String,
        port: Int,
        localHost: InetAddress,
        localPort: Int,
    ): Socket = delegate().createSocket(host, port, localHost, localPort)

    override fun createSocket(host: InetAddress, port: Int): Socket = delegate().createSocket(host, port)

    override fun createSocket(
        address: InetAddress,
        port: Int,
        localAddress: InetAddress,
        localPort: Int,
    ): Socket = delegate().createSocket(address, port, localAddress, localPort)
}
