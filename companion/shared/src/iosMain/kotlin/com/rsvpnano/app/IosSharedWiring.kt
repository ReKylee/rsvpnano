package com.rsvpnano.app

import com.rsvpnano.api.NanoKtorClient
import com.rsvpnano.api.ArticleFetchClient
import com.rsvpnano.library.PendingDraftService
import com.rsvpnano.persistence.JsonAppSettingsStore
import com.rsvpnano.persistence.OkioTextStorage
import com.rsvpnano.persistence.PendingUploadJsonStore
import com.rsvpnano.presentation.CompanionPresenter
import com.rsvpnano.updates.FirmwareUpdates
import io.ktor.client.HttpClient
import io.ktor.client.engine.darwin.Darwin
import io.ktor.client.plugins.contentnegotiation.ContentNegotiation
import io.ktor.serialization.kotlinx.json.json
import kotlinx.serialization.json.Json
import kotlinx.coroutines.CoroutineScope
import okio.FileSystem
import okio.Path
import okio.Path.Companion.toPath
import platform.Foundation.NSFileManager

private const val DefaultAppGroupIdentifier = "group.com.rsvpnano.companion"

fun createIosCompanionPresenter(scope: CoroutineScope): CompanionPresenter {
    val httpClient = createIosHttpClient()
    val nanoClient = NanoKtorClient(httpClient = httpClient)
    val root = appGroupRootPath(DefaultAppGroupIdentifier)
    val settingsStore = createIosSettingsStore(root)
    return CompanionPresenter(
        companionController = NanoCompanionController(createIosDraftService(root, httpClient), nanoClient, nanoClient),
        firmwareUpdates = FirmwareUpdates(nanoClient, settingsStore),
        nanoNetworkController = IosNanoWifiConnector(),
        settingsStore = settingsStore,
        scope = scope,
    )
}

fun iosFirmwareUpdates(): FirmwareUpdates {
    val settingsStore = createIosSettingsStore(appGroupRootPath(DefaultAppGroupIdentifier))
    return FirmwareUpdates(NanoKtorClient(createIosHttpClient()), settingsStore)
}

fun createIosCompanionController(
    appGroupIdentifier: String = DefaultAppGroupIdentifier,
): NanoCompanionController {
    val httpClient = createIosHttpClient()
    val root = appGroupRootPath(appGroupIdentifier)
    val client = NanoKtorClient(httpClient)
    return NanoCompanionController(
        draftService = createIosDraftService(root, httpClient),
        nanoApi = client,
        repository = client,
    )
}

private fun createIosDraftService(root: Path, httpClient: HttpClient): PendingDraftService =
    PendingDraftService(
        store = PendingUploadJsonStore(
            OkioTextStorage(root.resolve("PendingUploads/drafts.json"), FileSystem.SYSTEM),
        ),
        articleFetchClient = ArticleFetchClient(httpClient),
    )

private fun createIosSettingsStore(root: Path): JsonAppSettingsStore =
    JsonAppSettingsStore(
        OkioTextStorage(root.resolve("Settings/companion_settings.json"), FileSystem.SYSTEM),
    )

private fun createIosHttpClient(): HttpClient {
    return HttpClient(Darwin) {
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

private fun appGroupRootPath(appGroupIdentifier: String): Path {
    val rootURL = NSFileManager.defaultManager()
        .containerURLForSecurityApplicationGroupIdentifier(appGroupIdentifier)
        ?: error("App group container is unavailable: $appGroupIdentifier")
    return requireNotNull(rootURL.path).toPath()
}
