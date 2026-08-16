package com.rsvpnano.app

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
import platform.darwin.NSObject
import platform.NetworkExtension.NEHotspotConfiguration
import platform.NetworkExtension.NEHotspotConfigurationManager
import platform.NetworkExtension.NEHotspotNetwork
import kotlin.coroutines.resume

class IosNanoWifiConnector : NanoWifiConnector {
    private val _snapshot = MutableStateFlow(NanoWifiSnapshot())
    private val _events = MutableSharedFlow<NanoWifiEvent>(extraBufferCapacity = 4)
    private var requestedSsid: String? = null
    // NSNetServiceBrowser does not retain its delegate.
    private var serviceDelegate: NanoServiceDelegate? = null

    override val snapshot: StateFlow<NanoWifiSnapshot> = _snapshot
    override val events: SharedFlow<NanoWifiEvent> = _events

    override fun start() {
        refreshSnapshot()
    }

    override fun stop() {
        releaseRequestedNanoNetwork()
    }

    override fun refreshSnapshot() {
        NEHotspotNetwork.fetchCurrentWithCompletionHandler { network ->
            val ssid = network?.SSID
            val nano = NanoWifiIdentity.rememberedNanoOrNull(ssid)
            _snapshot.value = if (nano != null) {
                NanoWifiSnapshot(
                    currentNano = nano,
                    isAttached = true,
                    isRequesting = false,
                )
            } else {
                NanoWifiSnapshot()
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
        return endpoints.values.toList()
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
