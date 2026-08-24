package com.rsvpnano.app

import com.rsvpnano.connection.*
import com.rsvpnano.models.RememberedNano
import kotlinx.coroutines.flow.MutableSharedFlow
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.SharedFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.update
import kotlinx.coroutines.suspendCancellableCoroutine
import kotlinx.coroutines.withTimeoutOrNull
import kotlinx.cinterop.ObjCSignatureOverride
import platform.Foundation.NSNetService
import platform.Foundation.NSNetServiceBrowser
import platform.Foundation.NSNetServiceBrowserDelegateProtocol
import platform.Foundation.NSNetServiceDelegateProtocol
import platform.Network.nw_path_monitor_cancel
import platform.Network.nw_path_monitor_create
import platform.Network.nw_path_monitor_set_queue
import platform.Network.nw_path_monitor_set_update_handler
import platform.Network.nw_path_monitor_start
import platform.Network.nw_path_monitor_t
import platform.darwin.NSObject
import platform.darwin.dispatch_async
import platform.darwin.dispatch_get_main_queue
import platform.NetworkExtension.NEHotspotConfiguration
import platform.NetworkExtension.NEHotspotConfigurationManager
import platform.NetworkExtension.NEHotspotNetwork
import kotlin.coroutines.resume

class IosNanoWifiConnector : NanoWifiConnector {
    private val _snapshot = MutableStateFlow(NanoWifiSnapshot())
    private val _events = MutableSharedFlow<NanoWifiEvent>(extraBufferCapacity = 4)
    private var requestedSsid: String? = null
    private var started = false
    private var pathMonitor: nw_path_monitor_t = null
    private var snapshotGeneration = 0
    private var networkChangePending = false
    // NSNetServiceBrowser does not retain its delegate.
    private var serviceDelegate: NanoServiceDelegate? = null

    override val snapshot: StateFlow<NanoWifiSnapshot> = _snapshot
    override val events: SharedFlow<NanoWifiEvent> = _events

    override fun start() {
        if (started) return
        started = true
        val monitor = nw_path_monitor_create() ?: run {
            refreshSnapshot()
            return
        }
        pathMonitor = monitor
        nw_path_monitor_set_update_handler(monitor) {
            scheduleSnapshotRefresh(networkChanged = true)
        }
        nw_path_monitor_set_queue(monitor, dispatch_get_main_queue())
        nw_path_monitor_start(monitor)
    }

    override fun stop() {
        started = false
        pathMonitor?.let(::nw_path_monitor_cancel)
        pathMonitor = null
        snapshotGeneration++
        networkChangePending = false
        releaseRequestedNanoNetwork()
    }

    override fun refreshSnapshot() {
        scheduleSnapshotRefresh(networkChanged = false)
    }

    private fun scheduleSnapshotRefresh(networkChanged: Boolean) {
        dispatch_async(dispatch_get_main_queue()) {
            if (started) {
                networkChangePending = networkChangePending || networkChanged
                val generation = ++snapshotGeneration
                NEHotspotNetwork.fetchCurrentWithCompletionHandler { network ->
                    dispatch_async(dispatch_get_main_queue()) {
                        if (started && generation == snapshotGeneration) {
                            val nano = NanoWifiIdentity.rememberedNanoOrNull(network?.SSID)
                            _snapshot.value = if (nano != null) {
                                NanoWifiSnapshot(
                                    currentNano = nano,
                                    isAttached = true,
                                    isRequesting = false,
                                )
                            } else {
                                NanoWifiSnapshot()
                            }
                            if (networkChangePending) {
                                networkChangePending = false
                                _events.tryEmit(NanoWifiEvent.NetworkChanged)
                            }
                        }
                    }
                }
            }
        }
    }

    override suspend fun discoverNanos(): List<NanoEndpoint> {
        val endpoints = linkedMapOf<String, NanoEndpoint>()
        withTimeoutOrNull(DISCOVERY_TIMEOUT_MS) {
            suspendCancellableCoroutine<Unit> { continuation ->
                val browser = NSNetServiceBrowser()
                lateinit var delegate: NanoServiceDelegate

                fun cleanup() {
                    browser.stop()
                    browser.delegate = null
                    delegate.stop()
                    serviceDelegate = null
                }

                delegate = NanoServiceDelegate(
                    onResolved = { endpoint -> endpoints[endpoint.nano.ssid] = endpoint },
                    onRemoved = { endpoints.remove(it) },
                    onFailure = {
                        cleanup()
                        if (continuation.isActive) continuation.resume(Unit)
                    },
                )
                serviceDelegate = delegate
                browser.delegate = delegate
                continuation.invokeOnCancellation { cleanup() }
                browser.searchForServicesOfType(SERVICE_TYPE, inDomain = "local.")
            }
        }
        return endpoints.values.sortedBy { it.nano.ssid }
    }

    override fun requestNanoNetwork(
        rememberedNano: RememberedNano?,
    ): NanoWifiRequestResult {
        val targetSsid = rememberedNano?.ssid ?: NanoWifiIdentity.SSID_PREFIX
        if (_snapshot.value.isAttached) return NanoWifiRequestResult.AlreadyAttached
        if (_snapshot.value.isRequesting) return NanoWifiRequestResult.AlreadyRequesting

        val target = rememberedNano ?: RememberedNano(ssid = targetSsid)
        requestedSsid = rememberedNano?.ssid
        _snapshot.update {
            it.copy(
                currentNano = target,
                isRequesting = true,
            )
        }

        val configuration = if (rememberedNano != null) {
            NEHotspotConfiguration(sSID = targetSsid)
        } else {
            NEHotspotConfiguration(sSIDPrefix = targetSsid)
        }
        configuration.joinOnce = true
        NEHotspotConfigurationManager.sharedManager.applyConfiguration(configuration) { error ->
            val alreadyAssociated = error?.code == HOTSPOT_ALREADY_ASSOCIATED_ERROR_CODE.toLong()
            if (error != null && !alreadyAssociated) {
                _snapshot.value = NanoWifiSnapshot()
                _events.tryEmit(NanoWifiEvent.RequestUnavailable)
                return@applyConfiguration
            }
            refreshSnapshot()
        }
        return NanoWifiRequestResult.Started
    }

    private fun cancelNanoRequest() {
        _snapshot.update { it.copy(isRequesting = false) }
    }

    private fun releaseRequestedNanoNetwork() {
        val ssid = requestedSsid ?: _snapshot.value.currentNano?.ssid
        if (ssid != null && NanoWifiIdentity.isNanoSsid(ssid)) {
            NEHotspotConfigurationManager.sharedManager.removeConfigurationForSSID(ssid)
        }
        requestedSsid = null
        cancelNanoRequest()
    }

    private companion object {
        const val DISCOVERY_TIMEOUT_MS = 2_500L
        const val HOTSPOT_ALREADY_ASSOCIATED_ERROR_CODE = 13
        const val SERVICE_TYPE = "_rsvpnano._tcp."
    }
}

private class NanoServiceDelegate(
    private val onResolved: (NanoEndpoint) -> Unit,
    private val onRemoved: (String) -> Unit,
    private val onFailure: () -> Unit,
) : NSObject(), NSNetServiceBrowserDelegateProtocol, NSNetServiceDelegateProtocol {
    private val services = mutableMapOf<String, NSNetService>()

    @ObjCSignatureOverride
    override fun netServiceBrowser(
        browser: NSNetServiceBrowser,
        didFindService: NSNetService,
        moreComing: Boolean,
    ) {
        if (didFindService.name in services) return
        services[didFindService.name] = didFindService
        didFindService.delegate = this
        didFindService.resolveWithTimeout(2.0)
    }

    @ObjCSignatureOverride
    override fun netServiceBrowser(
        browser: NSNetServiceBrowser,
        didRemoveService: NSNetService,
        moreComing: Boolean,
    ) {
        services.remove(didRemoveService.name)?.delegate = null
        onRemoved(didRemoveService.name)
    }

    override fun netServiceDidResolveAddress(sender: NSNetService) {
        val host = sender.hostName ?: return forget(sender)
        val port = sender.port.takeIf { it != 80L }?.let { ":$it" }.orEmpty()
        onResolved(
            NanoEndpoint(
                baseUrl = "http://$host$port",
                nano = RememberedNano(sender.name),
            ),
        )
        forget(sender)
    }

    override fun netService(sender: NSNetService, didNotResolve: Map<Any?, *>) {
        forget(sender)
    }

    override fun netServiceBrowser(browser: NSNetServiceBrowser, didNotSearch: Map<Any?, *>) {
        onFailure()
    }

    fun stop() {
        services.values.forEach { it.delegate = null }
        services.clear()
    }

    private fun forget(service: NSNetService) {
        services.remove(service.name)?.delegate = null
    }
}
