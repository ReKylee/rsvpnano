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
import okio.FileSystem
import okio.Path.Companion.toPath
import javax.net.SocketFactory

private const val PendingUploadRelativePath = "pending-uploads/drafts.json"
private const val SettingsRelativePath = "settings/companion_settings.json"

fun createAndroidCompanionPresenter(
    appFilesDir: File,
    nanoWifiConnector: NanoWifiConnector,
    nanoSocketFactory: SocketFactory? = null,
    scope: CoroutineScope,
): CompanionPresenter {
    val nanoClient = NanoKtorClient(httpClient = createAndroidHttpClient(nanoSocketFactory))
    val internetClient = createAndroidHttpClient()
    val repository = NanoKtorClient(httpClient = internetClient)
    val root = appFilesDir.absolutePath.toPath()
    val settingsStore = JsonAppSettingsStore(OkioTextStorage(root.resolve(SettingsRelativePath), FileSystem.SYSTEM))
    val draftService = PendingDraftService(
        repository = PendingUploadRepository(
            PendingUploadJsonStore(OkioTextStorage(root.resolve(PendingUploadRelativePath), FileSystem.SYSTEM)),
        ),
        articleFetchClient = ArticleFetchClient(httpClient = internetClient),
    )
    return CompanionPresenter(
        companionController = NanoCompanionController(draftService, nanoClient, repository),
        firmwareUpdates = FirmwareUpdates(repository, settingsStore),
        nanoNetworkController = nanoWifiConnector,
        settingsStore = settingsStore,
        scope = scope,
    )
}

fun createAndroidFirmwareUpdates(appFilesDir: File): FirmwareUpdates {
    val root = appFilesDir.absolutePath.toPath()
    return FirmwareUpdates(
        repository = NanoKtorClient(createAndroidHttpClient()),
        settingsStore = JsonAppSettingsStore(
            OkioTextStorage(root.resolve(SettingsRelativePath), FileSystem.SYSTEM),
        ),
    )
}

private fun createAndroidHttpClient(socketFactory: SocketFactory? = null): HttpClient {
    return HttpClient(OkHttp) {
        if (socketFactory != null) {
            engine {
                config {
                    socketFactory(socketFactory)
                }
            }
        }
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
