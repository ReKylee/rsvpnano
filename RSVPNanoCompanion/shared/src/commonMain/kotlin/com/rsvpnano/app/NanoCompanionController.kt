package com.rsvpnano.app

import com.rsvpnano.api.NanoClient
import com.rsvpnano.converters.RsvpBookFile
import com.rsvpnano.converters.SharedArticle
import com.rsvpnano.models.NanoBook
import com.rsvpnano.models.NanoSettings
import com.rsvpnano.models.NanoThemeCatalogItem
import com.rsvpnano.models.NanoFontCatalogItem
import com.rsvpnano.models.NanoWifiSettings
import com.rsvpnano.models.PendingUpload
import com.rsvpnano.models.needsArticleFetch
import com.rsvpnano.sync.RssFeedNormalizer
import kotlinx.coroutines.delay

/**
 * Shared workflow controller for app-level device operations.
 *
 * Platform ViewModels should own UI state, but this class owns the repeated sequencing between
 * local companion data, device sync calls, and post-mutation refreshes.
 */
class NanoCompanionController(
    private val draftService: PendingDraftService,
    private val client: NanoClient,
) {
    suspend fun refreshLocal(): List<PendingUpload> = draftService.loadDrafts()

    suspend fun connect(baseUrl: String): CompanionConnectSnapshot {
        val device = NanoDeviceSnapshot(
            info = client.fetchInfo(baseUrl),
            books = runCatching { client.listBooks(baseUrl) }.getOrDefault(emptyList()),
            settings = runCatching { client.fetchSettings(baseUrl) }.getOrNull(),
            wifiSettings = runCatching { client.fetchWifiSettings(baseUrl) }.getOrNull(),
            rssFeeds = runCatching { client.fetchRssFeeds(baseUrl) }.getOrNull(),
        )
        val deviceFeeds = RssFeedNormalizer.normalize(device.rssFeeds?.feeds.orEmpty())
        return CompanionConnectSnapshot(
            device = device,
            rssFeeds = deviceFeeds,
            drafts = draftService.loadDrafts(),
        )
    }

    suspend fun connectWithRetry(
        baseUrl: String,
        attempts: Int = DEFAULT_CONNECTION_ATTEMPTS,
        retryDelayMillis: Long = DEFAULT_CONNECTION_RETRY_DELAY_MILLIS,
    ): CompanionConnectSnapshot {
        return retryDeviceOperation(attempts, retryDelayMillis) {
            connect(baseUrl)
        }
    }

    suspend fun syncPendingUploads(baseUrl: String, items: List<PendingUpload>): CompanionPendingSyncSnapshot {
        verifyReachable(baseUrl)
        val readyItems = items.filterNot(PendingUpload::needsArticleFetch)
        val remaining = draftService.syncPendingUploads(client = client, baseUrl = baseUrl, items = readyItems)
        return CompanionPendingSyncSnapshot(
            drafts = remaining,
            books = client.listBooks(baseUrl),
            syncedCount = readyItems.size,
        )
    }

    suspend fun saveDraft(item: PendingUpload): List<PendingUpload> {
        draftService.saveDraft(item)
        return draftService.loadDrafts()
    }

    suspend fun saveDraftFetchingArticleIfNeeded(item: PendingUpload): CompanionDraftSaveSnapshot {
        val fetched = if (item.needsArticleFetch()) {
            draftService.fetchArticleIfAvailable(
                title = item.title,
                source = item.sourceUrl.orEmpty(),
            )
        } else {
            null
        }
        val savedItem = fetched?.let { article ->
            item.copy(
                title = article.title,
                body = article.text,
            )
        } ?: item
        draftService.saveDraft(savedItem)
        return CompanionDraftSaveSnapshot(
            drafts = draftService.loadDrafts(),
            item = savedItem,
            fetchedArticle = fetched != null,
        )
    }

    suspend fun fetchSharedArticle(title: String, source: String): SharedArticle? {
        return draftService.fetchArticleIfAvailable(title = title, source = source)
    }

    suspend fun deleteDraft(item: PendingUpload): List<PendingUpload> {
        draftService.deleteDraft(item)
        return draftService.loadDrafts()
    }

    suspend fun saveRssFeeds(
        baseUrl: String,
        feeds: List<String>,
    ): List<String> {
        verifyReachable(baseUrl)
        val normalized = RssFeedNormalizer.normalize(feeds)
        val deviceFeeds = client.updateRssFeeds(baseUrl, normalized).feeds
        val syncedFeeds = RssFeedNormalizer.normalize(deviceFeeds)
        return syncedFeeds
    }

    suspend fun refreshRssFeeds(baseUrl: String): List<String> {
        verifyReachable(baseUrl)
        val deviceFeeds = client.fetchRssFeeds(baseUrl).feeds
        val syncedFeeds = RssFeedNormalizer.normalize(deviceFeeds)
        return syncedFeeds
    }

    suspend fun uploadBook(
        baseUrl: String,
        file: RsvpBookFile,
        category: String,
        onProgress: ((sent: Long, total: Long) -> Unit)? = null,
    ): List<NanoBook> {
        verifyReachable(baseUrl)
        client.uploadBook(
            baseUrl = baseUrl,
            name = file.filename,
            data = file.data,
            category = category,
            onProgress = onProgress,
        )
        return client.listBooks(baseUrl)
    }

    suspend fun fetchThemeCatalog(catalogUrl: String): List<NanoThemeCatalogItem> = client.fetchThemeCatalog(catalogUrl)

    suspend fun fetchFontCatalog(catalogUrl: String): List<NanoFontCatalogItem> = client.fetchFontCatalog(catalogUrl)

    suspend fun downloadTheme(catalogUrl: String, theme: NanoThemeCatalogItem): CompanionThemeFile {
        require(theme.file.isNotBlank() && '/' !in theme.file && '\\' !in theme.file) {
            "Theme catalog file path is invalid."
        }
        return CompanionThemeFile(
            id = theme.id,
            filename = theme.file,
            data = client.downloadTheme(catalogFileUrl(catalogUrl, theme.file)),
        )
    }

    suspend fun downloadFont(catalogUrl: String, font: NanoFontCatalogItem, size: String): CompanionFontFile {
        val file = font.files[size].orEmpty()
        require(isSafeFontCatalogPath(file)) {
            "Font catalog file path is invalid."
        }
        return CompanionFontFile(
            id = font.id,
            family = font.name,
            size = size,
            filename = file.substringAfterLast('/'),
            data = client.downloadFont(catalogFileUrl(catalogUrl, file)),
        )
    }

    suspend fun uploadTheme(
        baseUrl: String,
        filename: String,
        data: ByteArray,
        onProgress: ((sent: Long, total: Long) -> Unit)? = null,
    ): CompanionSettingsSnapshot {
        verifyReachable(baseUrl)
        val uploaded = client.uploadTheme(
            baseUrl = baseUrl,
            name = filename,
            data = data,
            onProgress = onProgress,
        )
        val refreshed = client.fetchSettings(baseUrl)
        val selected = uploaded.id
            ?.takeIf { id -> refreshed.themes.any { it.id == id } }
            ?.let { id -> client.updateSettings(baseUrl, refreshed.withThemeId(id)) }
            ?: refreshed
        return CompanionSettingsSnapshot(settings = selected, wifiSettings = null)
    }

    suspend fun uploadFont(
        baseUrl: String,
        family: String,
        size: String,
        filename: String,
        data: ByteArray,
        onProgress: ((sent: Long, total: Long) -> Unit)? = null,
    ): CompanionSettingsSnapshot {
        verifyReachable(baseUrl)
        client.uploadFont(
            baseUrl = baseUrl,
            family = family,
            size = size,
            name = filename,
            data = data,
            onProgress = onProgress,
        )
        return CompanionSettingsSnapshot(settings = client.fetchSettings(baseUrl), wifiSettings = null)
    }

    suspend fun deleteBooks(baseUrl: String, bookIds: List<String>): List<NanoBook> {
        verifyReachable(baseUrl)
        bookIds.forEach { bookId ->
            client.deleteBook(baseUrl, bookId)
        }
        return client.listBooks(baseUrl)
    }

    suspend fun setBookPosition(baseUrl: String, book: NanoBook, wordIndex: Int): List<NanoBook> {
        val wordCount = book.metadata.wordCount
        require(book.source != null && wordCount > 0) {
            "Book position is unavailable."
        }
        verifyReachable(baseUrl)
        client.setBookPosition(
            baseUrl = baseUrl,
            id = book.id,
            wordIndex = wordIndex.coerceIn(0, wordCount - 1),
        )
        return client.listBooks(baseUrl)
    }

    suspend fun refreshSettings(baseUrl: String): CompanionSettingsSnapshot {
        verifyReachable(baseUrl)
        return CompanionSettingsSnapshot(
            settings = client.fetchSettings(baseUrl),
            wifiSettings = runCatching { client.fetchWifiSettings(baseUrl) }.getOrNull(),
        )
    }

    suspend fun saveSettings(baseUrl: String, settings: NanoSettings): CompanionSettingsSnapshot {
        verifyReachable(baseUrl)
        return CompanionSettingsSnapshot(
            settings = client.updateSettings(baseUrl, settings),
            wifiSettings = null,
        )
    }

    suspend fun saveWifiSettings(baseUrl: String, ssid: String, password: String): NanoWifiSettings {
        verifyReachable(baseUrl)
        return client.updateWifi(baseUrl, ssid, password)
    }

    suspend fun clearWifiSettings(baseUrl: String): NanoWifiSettings {
        verifyReachable(baseUrl)
        return client.forgetWifi(baseUrl)
    }

    suspend fun verifyReachable(baseUrl: String) {
        client.fetchInfo(baseUrl)
    }

    suspend fun verifyReachableWithRetry(
        baseUrl: String,
        attempts: Int = DEFAULT_CONNECTION_ATTEMPTS,
        retryDelayMillis: Long = DEFAULT_CONNECTION_RETRY_DELAY_MILLIS,
    ) {
        retryDeviceOperation(attempts, retryDelayMillis) {
            client.fetchInfo(baseUrl)
        }
    }

    private suspend fun <T> retryDeviceOperation(
        attempts: Int,
        retryDelayMillis: Long,
        operation: suspend () -> T,
    ): T {
        var lastError: Throwable? = null
        repeat(attempts.coerceAtLeast(1)) { index ->
            try {
                return operation()
            } catch (error: Throwable) {
                lastError = error
                if (index < attempts - 1 && retryDelayMillis > 0) {
                    delay(retryDelayMillis)
                }
            }
        }
        throw lastError ?: IllegalStateException("Device operation failed.")
    }

    private fun catalogFileUrl(catalogUrl: String, file: String): String =
        catalogUrl.substringBeforeLast('/', missingDelimiterValue = catalogUrl) + "/" + file

    private fun isSafeFontCatalogPath(file: String): Boolean =
        file.isNotBlank() &&
            !file.startsWith('/') &&
            '\\' !in file &&
            ".." !in file.split('/') &&
            file.endsWith(".rfont4", ignoreCase = true)

    companion object {
        const val DEFAULT_CONNECTION_ATTEMPTS = 4
        const val DEFAULT_CONNECTION_RETRY_DELAY_MILLIS = 750L
    }
}

data class CompanionConnectSnapshot(
    val device: NanoDeviceSnapshot,
    val rssFeeds: List<String>,
    val drafts: List<PendingUpload>,
)

data class CompanionPendingSyncSnapshot(
    val drafts: List<PendingUpload>,
    val books: List<NanoBook>,
    val syncedCount: Int,
)

data class CompanionDraftSaveSnapshot(
    val drafts: List<PendingUpload>,
    val item: PendingUpload,
    val fetchedArticle: Boolean,
)

data class CompanionThemeFile(
    val id: String,
    val filename: String,
    val data: ByteArray,
)

data class CompanionFontFile(
    val id: String,
    val family: String,
    val size: String,
    val filename: String,
    val data: ByteArray,
)

data class CompanionSettingsSnapshot(
    val settings: NanoSettings,
    val wifiSettings: NanoWifiSettings?,
)
