package com.rsvpnano.web

import com.rsvpnano.api.ArticleFetchClient
import com.rsvpnano.api.NanoKtorClient
import com.rsvpnano.app.FirmwareUpdates
import com.rsvpnano.app.NanoCompanionController
import com.rsvpnano.app.NanoEndpoint
import com.rsvpnano.app.NanoWifiConnector
import com.rsvpnano.app.NanoWifiEvent
import com.rsvpnano.app.NanoWifiRequestResult
import com.rsvpnano.app.NanoWifiSnapshot
import com.rsvpnano.app.PendingDraftService
import com.rsvpnano.models.RememberedNano
import com.rsvpnano.persistence.JsonAppSettingsStore
import com.rsvpnano.persistence.PendingUploadJsonStore
import com.rsvpnano.persistence.TextStorage
import com.rsvpnano.ui.CompanionPresenter
import io.ktor.client.HttpClient
import io.ktor.client.engine.js.Js
import io.ktor.client.plugins.contentnegotiation.ContentNegotiation
import io.ktor.serialization.kotlinx.json.json
import kotlinx.browser.window
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.emptyFlow
import kotlinx.serialization.json.Json

private const val DraftStorageKey = "rsvpnano.web.drafts"
private const val SettingsStorageKey = "rsvpnano.web.settings"
internal const val EndpointStorageKey = "rsvpnano.web.endpoint"
internal const val NanoNameStorageKey = "rsvpnano.web.nanoName"

fun createBrowserCompanionPresenter(scope: CoroutineScope): CompanionPresenter {
    val deviceClient = BrowserNanoApi(NanoKtorClient(browserHttpClient()), BrowserSerial.api)
    val internetHttpClient = browserHttpClient()
    val repository = NanoKtorClient(internetHttpClient)
    val settingsStore = JsonAppSettingsStore(LocalStorageTextStorage(SettingsStorageKey))
    val drafts = PendingDraftService(
        store = PendingUploadJsonStore(LocalStorageTextStorage(DraftStorageKey)),
        articleFetchClient = ArticleFetchClient(internetHttpClient, userAgent = null),
    )
    return CompanionPresenter(
        companionController = NanoCompanionController(drafts, deviceClient, repository),
        firmwareUpdates = FirmwareUpdates(repository, settingsStore),
        nanoNetworkController = BrowserNanoWifiConnector,
        settingsStore = settingsStore,
        scope = scope,
    )
}

private fun browserHttpClient() = HttpClient(Js) {
    install(ContentNegotiation) {
        json(Json {
            ignoreUnknownKeys = true
            encodeDefaults = true
            explicitNulls = false
        })
    }
}

private class LocalStorageTextStorage(private val key: String) : TextStorage {
    override suspend fun readText(): String? = window.localStorage.getItem(key)

    override suspend fun writeText(value: String) {
        window.localStorage.setItem(key, value)
    }
}

private object BrowserNanoWifiConnector : NanoWifiConnector {
    override val snapshot: StateFlow<NanoWifiSnapshot> = MutableStateFlow(NanoWifiSnapshot())
    override val events: Flow<NanoWifiEvent> = emptyFlow()

    override fun start() = Unit
    override fun stop() = Unit
    override fun refreshSnapshot() = Unit
    override suspend fun discoverNanos(): List<NanoEndpoint> =
        window.localStorage.getItem(EndpointStorageKey)
            ?.takeIf(String::isNotBlank)
            ?.let { address ->
                val name = window.localStorage.getItem(NanoNameStorageKey)
                    ?.takeIf(String::isNotBlank)
                    ?: address.substringAfter("://").substringBefore('/')
                listOf(NanoEndpoint(address, RememberedNano(name)))
            }
            .orEmpty()
    override fun requestNanoNetwork(rememberedNano: RememberedNano?): NanoWifiRequestResult =
        NanoWifiRequestResult.Failed("Connect with USB, or enter your Nano's address.")
}
