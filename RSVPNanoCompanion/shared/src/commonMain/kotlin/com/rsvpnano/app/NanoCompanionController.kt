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
    private val deviceSyncService: NanoDeviceSyncService,
    private val client: NanoClient,
) {
    suspend fun refreshLocal(): CompanionLocalSnapshot {
        return CompanionLocalSnapshot(
            drafts = draftService.loadDrafts(),
        )
    }

    suspend fun refreshDrafts(): CompanionDraftsSnapshot {
        return CompanionDraftsSnapshot(drafts = draftService.loadDrafts())
    }

    suspend fun connect(baseUrl: String): CompanionConnectSnapshot {
        val device = deviceSyncService.connect(baseUrl)
        val deviceFeeds = RssFeedNormalizer.normalize(device.rssFeeds?.feeds.orEmpty())
        return CompanionConnectSnapshot(
            device = device,
            rssFeeds = deviceFeeds,
            syncedRssFeeds = deviceFeeds,
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

    suspend fun refreshDevice(baseUrl: String): CompanionDeviceRefreshSnapshot {
        verifyReachable(baseUrl)
        val books = deviceSyncService.refreshBooks(baseUrl)
        val settings = runCatching { deviceSyncService.refreshSettings(baseUrl) }.getOrNull()
        val wifiSettings = runCatching { deviceSyncService.refreshWifiSettings(baseUrl) }.getOrNull()
        val deviceFeeds = RssFeedNormalizer.normalize(
            runCatching { deviceSyncService.refreshRssFeeds(baseUrl).feeds }.getOrDefault(emptyList())
        )
        return CompanionDeviceRefreshSnapshot(
            books = books,
            settings = settings,
            wifiSettings = wifiSettings,
            rssFeeds = deviceFeeds,
            syncedRssFeeds = deviceFeeds,
            drafts = draftService.loadDrafts(),
        )
    }

    suspend fun syncPendingUploads(baseUrl: String, items: List<PendingUpload>): CompanionPendingSyncSnapshot {
        verifyReachable(baseUrl)
        val readyItems = items.filterNot(::needsArticleFetch)
        val remaining = draftService.syncPendingUploads(client = client, baseUrl = baseUrl, items = readyItems)
        return CompanionPendingSyncSnapshot(
            drafts = remaining,
            books = deviceSyncService.refreshBooks(baseUrl),
            syncedCount = readyItems.size,
        )
    }

    suspend fun saveDraft(item: PendingUpload): CompanionDraftsSnapshot {
        draftService.saveDraft(item)
        return CompanionDraftsSnapshot(drafts = draftService.loadDrafts())
    }

    suspend fun saveDraftFetchingArticleIfNeeded(item: PendingUpload): CompanionDraftSaveSnapshot {
        val fetched = if (needsArticleFetch(item)) {
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

    suspend fun updateDraft(item: PendingUpload, title: String, body: String): CompanionDraftsSnapshot {
        draftService.updateDraft(item, title, body)
        return CompanionDraftsSnapshot(drafts = draftService.loadDrafts())
    }

    suspend fun deleteDraft(item: PendingUpload): CompanionDraftsSnapshot {
        draftService.deleteDraft(item)
        return CompanionDraftsSnapshot(drafts = draftService.loadDrafts())
    }

    suspend fun deleteDrafts(ids: List<String>): CompanionDraftsSnapshot {
        draftService.deleteDrafts(ids)
        return CompanionDraftsSnapshot(drafts = draftService.loadDrafts())
    }

    suspend fun fetchArticle(item: PendingUpload): CompanionArticleFetchSnapshot {
        val article = draftService.fetchArticle(title = item.title, source = item.sourceUrl.orEmpty())
        draftService.updateDraft(item, article.title, article.text)
        return CompanionArticleFetchSnapshot(
            article = article,
            drafts = draftService.loadDrafts(),
        )
    }

    suspend fun fetchArticles(items: List<PendingUpload>): CompanionDraftsSnapshot {
        items.forEach { item ->
            val article = draftService.fetchArticle(title = item.title, source = item.sourceUrl.orEmpty())
            draftService.updateDraft(item, article.title, article.text)
        }
        return CompanionDraftsSnapshot(drafts = draftService.loadDrafts())
    }

    fun needsArticleFetch(item: PendingUpload): Boolean = draftService.needsArticleFetch(item)

    suspend fun saveRssFeeds(
        baseUrl: String,
        feeds: List<String>,
    ): CompanionRssSnapshot {
        verifyReachable(baseUrl)
        val normalized = RssFeedNormalizer.normalize(feeds)
        val deviceFeeds = deviceSyncService.saveRssFeeds(baseUrl, normalized).feeds
        val syncedFeeds = RssFeedNormalizer.normalize(deviceFeeds)
        return CompanionRssSnapshot(
            rssFeeds = syncedFeeds,
            syncedRssFeeds = syncedFeeds,
            didSyncDevice = true,
        )
    }

    suspend fun refreshRssFeeds(baseUrl: String): CompanionRssSnapshot {
        verifyReachable(baseUrl)
        val deviceFeeds = deviceSyncService.refreshRssFeeds(baseUrl).feeds
        val syncedFeeds = RssFeedNormalizer.normalize(deviceFeeds)
        return CompanionRssSnapshot(
            rssFeeds = syncedFeeds,
            syncedRssFeeds = syncedFeeds,
            didSyncDevice = false,
        )
    }

    suspend fun uploadBook(
        baseUrl: String,
        file: RsvpBookFile,
        category: String,
        onProgress: ((sent: Long, total: Long) -> Unit)? = null,
    ): CompanionBooksSnapshot {
        verifyReachable(baseUrl)
        deviceSyncService.uploadBook(
            baseUrl = baseUrl,
            filename = file.filename,
            data = file.data,
            category = category,
            onProgress = onProgress,
        )
        return CompanionBooksSnapshot(books = deviceSyncService.refreshBooks(baseUrl))
    }

    suspend fun fetchThemeCatalog(catalogUrl: String): CompanionThemeCatalogSnapshot =
        CompanionThemeCatalogSnapshot(themes = client.fetchThemeCatalog(catalogUrl))

    suspend fun fetchFontCatalog(catalogUrl: String): CompanionFontCatalogSnapshot =
        CompanionFontCatalogSnapshot(fonts = client.fetchFontCatalog(catalogUrl))

    suspend fun downloadTheme(catalogUrl: String, theme: NanoThemeCatalogItem): CompanionThemeFile {
        require(theme.file.isNotBlank() && '/' !in theme.file && '\\' !in theme.file) {
            "Theme catalog file path is invalid."
        }
        return CompanionThemeFile(
            id = theme.id,
            filename = theme.file,
            data = client.downloadTheme(themeFileUrl(catalogUrl, theme.file)),
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
        val uploaded = deviceSyncService.uploadTheme(
            baseUrl = baseUrl,
            filename = filename,
            data = data,
            onProgress = onProgress,
        )
        val refreshed = deviceSyncService.refreshSettings(baseUrl)
        val selected = uploaded.id
            ?.takeIf { id -> refreshed.themes.any { it.id == id } }
            ?.let { id -> deviceSyncService.saveSettings(baseUrl, refreshed.withThemeId(id)) }
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
        deviceSyncService.uploadFont(
            baseUrl = baseUrl,
            family = family,
            size = size,
            filename = filename,
            data = data,
            onProgress = onProgress,
        )
        return CompanionSettingsSnapshot(settings = deviceSyncService.refreshSettings(baseUrl), wifiSettings = null)
    }

    suspend fun installOnlineFont(
        baseUrl: String,
        catalogUrl: String,
        font: NanoFontCatalogItem,
        sizes: List<String> = font.files.keys.sorted(),
        onProgress: ((sent: Long, total: Long) -> Unit)? = null,
    ): CompanionSettingsSnapshot {
        verifyReachable(baseUrl)
        sizes.forEach { size ->
            val file = downloadFont(catalogUrl, font, size)
            deviceSyncService.uploadFont(
                baseUrl = baseUrl,
                family = font.name,
                size = size,
                filename = file.filename,
                data = file.data,
                onProgress = onProgress,
            )
        }
        val refreshed = deviceSyncService.refreshSettings(baseUrl)
        val selected = if (refreshed.fonts.any { it.id == font.id }) {
            deviceSyncService.saveSettings(baseUrl, refreshed.withTypeface(font.id))
        } else {
            refreshed
        }
        return CompanionSettingsSnapshot(settings = selected, wifiSettings = null)
    }

    suspend fun installOnlineTheme(
        baseUrl: String,
        catalogUrl: String,
        theme: NanoThemeCatalogItem,
        onProgress: ((sent: Long, total: Long) -> Unit)? = null,
    ): CompanionSettingsSnapshot {
        verifyReachable(baseUrl)
        require(theme.file.isNotBlank() && '/' !in theme.file && '\\' !in theme.file) {
            "Theme catalog file path is invalid."
        }
        val data = client.downloadTheme(themeFileUrl(catalogUrl, theme.file))
        deviceSyncService.uploadTheme(
            baseUrl = baseUrl,
            filename = theme.file,
            data = data,
            onProgress = onProgress,
        )
        val refreshed = deviceSyncService.refreshSettings(baseUrl)
        val selected = if (refreshed.themes.any { it.id == theme.id }) {
            deviceSyncService.saveSettings(baseUrl, refreshed.withThemeId(theme.id))
        } else {
            refreshed
        }
        return CompanionSettingsSnapshot(settings = selected, wifiSettings = null)
    }

    suspend fun deleteBooks(baseUrl: String, bookIds: List<String>): CompanionBooksSnapshot {
        verifyReachable(baseUrl)
        bookIds.forEach { bookId ->
            deviceSyncService.deleteBook(baseUrl, bookId)
        }
        return CompanionBooksSnapshot(books = deviceSyncService.refreshBooks(baseUrl))
    }

    suspend fun setBookPosition(baseUrl: String, book: NanoBook, wordIndex: Int): CompanionBooksSnapshot {
        val wordCount = book.metadata.wordCount
        require(book.source != null && wordCount > 0) {
            "Book position is unavailable."
        }
        verifyReachable(baseUrl)
        deviceSyncService.setBookPosition(
            baseUrl = baseUrl,
            id = book.id,
            wordIndex = wordIndex.coerceIn(0, wordCount - 1),
        )
        return CompanionBooksSnapshot(books = deviceSyncService.refreshBooks(baseUrl))
    }

    suspend fun refreshSettings(baseUrl: String): CompanionSettingsSnapshot {
        verifyReachable(baseUrl)
        return CompanionSettingsSnapshot(
            settings = deviceSyncService.refreshSettings(baseUrl),
            wifiSettings = runCatching { deviceSyncService.refreshWifiSettings(baseUrl) }.getOrNull(),
        )
    }

    suspend fun saveSettings(baseUrl: String, settings: NanoSettings): CompanionSettingsSnapshot {
        verifyReachable(baseUrl)
        return CompanionSettingsSnapshot(
            settings = deviceSyncService.saveSettings(baseUrl, settings),
            wifiSettings = null,
        )
    }

    suspend fun refreshWifiSettings(baseUrl: String): CompanionWifiSnapshot {
        verifyReachable(baseUrl)
        return CompanionWifiSnapshot(wifiSettings = deviceSyncService.refreshWifiSettings(baseUrl))
    }

    suspend fun saveWifiSettings(baseUrl: String, ssid: String, password: String): CompanionWifiSnapshot {
        verifyReachable(baseUrl)
        return CompanionWifiSnapshot(
            wifiSettings = deviceSyncService.saveWifiSettings(baseUrl, ssid, password),
        )
    }

    suspend fun clearWifiSettings(baseUrl: String): CompanionWifiSnapshot {
        verifyReachable(baseUrl)
        return CompanionWifiSnapshot(wifiSettings = deviceSyncService.clearWifiSettings(baseUrl))
    }

    suspend fun verifyReachable(baseUrl: String) {
        deviceSyncService.verifyReachable(baseUrl)
    }

    suspend fun verifyReachableWithRetry(
        baseUrl: String,
        attempts: Int = DEFAULT_CONNECTION_ATTEMPTS,
        retryDelayMillis: Long = DEFAULT_CONNECTION_RETRY_DELAY_MILLIS,
    ) {
        retryDeviceOperation(attempts, retryDelayMillis) {
            deviceSyncService.verifyReachable(baseUrl)
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

    private fun themeFileUrl(catalogUrl: String, file: String): String =
        catalogUrl.substringBeforeLast('/', missingDelimiterValue = catalogUrl) + "/" + file

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

data class CompanionLocalSnapshot(
    val drafts: List<PendingUpload>,
)

data class CompanionConnectSnapshot(
    val device: NanoDeviceSnapshot,
    val rssFeeds: List<String>,
    val syncedRssFeeds: List<String>,
    val drafts: List<PendingUpload>,
)

data class CompanionDeviceRefreshSnapshot(
    val books: List<NanoBook>,
    val settings: NanoSettings?,
    val wifiSettings: NanoWifiSettings?,
    val rssFeeds: List<String>,
    val syncedRssFeeds: List<String>,
    val drafts: List<PendingUpload>,
)

data class CompanionPendingSyncSnapshot(
    val drafts: List<PendingUpload>,
    val books: List<NanoBook>,
    val syncedCount: Int,
)

data class CompanionDraftsSnapshot(
    val drafts: List<PendingUpload>,
)

data class CompanionDraftSaveSnapshot(
    val drafts: List<PendingUpload>,
    val item: PendingUpload,
    val fetchedArticle: Boolean,
)

data class CompanionArticleFetchSnapshot(
    val article: SharedArticle,
    val drafts: List<PendingUpload>,
)

data class CompanionRssSnapshot(
    val rssFeeds: List<String>,
    val syncedRssFeeds: List<String>,
    val didSyncDevice: Boolean,
)

data class CompanionBooksSnapshot(
    val books: List<NanoBook>,
)

data class CompanionThemeCatalogSnapshot(
    val themes: List<NanoThemeCatalogItem>,
)

data class CompanionFontCatalogSnapshot(
    val fonts: List<NanoFontCatalogItem>,
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

data class CompanionWifiSnapshot(
    val wifiSettings: NanoWifiSettings,
)
