package com.rsvpnano.app

import com.rsvpnano.api.NanoKtorClient
import com.rsvpnano.api.ArticleFetchClient
import com.rsvpnano.persistence.JsonAppSettingsStore
import com.rsvpnano.persistence.OkioTextStorage
import com.rsvpnano.persistence.PendingUploadJsonStore
import com.rsvpnano.persistence.PendingUploadRepository
import com.rsvpnano.ui.CompanionPresenter
import io.ktor.client.HttpClient
import io.ktor.client.engine.okhttp.OkHttp
import io.ktor.client.plugins.contentnegotiation.ContentNegotiation
import io.ktor.serialization.kotlinx.json.json
import java.io.File
import kotlinx.coroutines.CoroutineScope
import kotlinx.serialization.json.Json
import okio.Path.Companion.toPath

private const val PendingUploadRelativePath = "pending-uploads/drafts.json"
private const val SettingsRelativePath = "settings/companion_settings.json"

fun createAndroidCompanionPresenter(
    appFilesDir: File,
    nanoWifiConnector: NanoWifiConnector,
    scope: CoroutineScope,
): CompanionPresenter {
    val httpClient = createAndroidHttpClient()
    val nanoClient = NanoKtorClient(httpClient = httpClient)
    val root = appFilesDir.absolutePath.toPath()
    val settingsStore = JsonAppSettingsStore(OkioTextStorage(root.resolve(SettingsRelativePath)))
    val draftService = PendingDraftService(
        repository = PendingUploadRepository(
            PendingUploadJsonStore(OkioTextStorage(root.resolve(PendingUploadRelativePath))),
        ),
        articleFetchClient = ArticleFetchClient(httpClient = httpClient),
    )
    return CompanionPresenter(
        companionController = NanoCompanionController(draftService, nanoClient),
        firmwareUpdates = FirmwareUpdates(nanoClient, settingsStore),
        nanoNetworkController = nanoWifiConnector,
        settingsStore = settingsStore,
        scope = scope,
    )
}

fun createAndroidFirmwareUpdates(appFilesDir: File): FirmwareUpdates {
    val root = appFilesDir.absolutePath.toPath()
    return FirmwareUpdates(
        client = NanoKtorClient(createAndroidHttpClient()),
        settingsStore = JsonAppSettingsStore(OkioTextStorage(root.resolve(SettingsRelativePath))),
    )
}

private fun createAndroidHttpClient(): HttpClient {
    return HttpClient(OkHttp) {
        install(ContentNegotiation) {
            json(
                Json {
                    ignoreUnknownKeys = true
                    encodeDefaults = true
                    explicitNulls = false
                }
            )
        }
    }
}
