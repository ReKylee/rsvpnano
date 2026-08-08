package com.rsvpnano.ui

import com.rsvpnano.app.CompanionNotice
import com.rsvpnano.app.FirmwareUpdates
import com.rsvpnano.app.NanoCompanionController
import com.rsvpnano.app.NanoConnectionState
import com.rsvpnano.app.NanoConnectionTransport
import com.rsvpnano.app.NanoEndpoint
import com.rsvpnano.app.NanoWifiConnector
import com.rsvpnano.app.NanoWifiEvent
import com.rsvpnano.app.NanoWifiIdentity
import com.rsvpnano.app.NanoWifiRequestResult
import com.rsvpnano.app.NanoWifiSnapshot
import com.rsvpnano.app.SharedAppUtils
import com.rsvpnano.app.releaseSource
import com.rsvpnano.app.rawContentUrl
import com.rsvpnano.converters.ImportPreparation
import com.rsvpnano.converters.RsvpConverter
import com.rsvpnano.models.NanoBook
import com.rsvpnano.models.NanoLanguageFont
import com.rsvpnano.models.NanoSettings
import com.rsvpnano.models.PendingUpload
import com.rsvpnano.models.RememberedNano
import com.rsvpnano.models.needsArticleFetch
import com.rsvpnano.persistence.AppSettingsStore
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Job
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.update
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import kotlinx.coroutines.withTimeout
import kotlin.time.Clock
import kotlin.uuid.ExperimentalUuidApi
import kotlin.uuid.Uuid

class CompanionPresenter(
    private val companionController: NanoCompanionController,
    private val firmwareUpdates: FirmwareUpdates,
    private val nanoNetworkController: NanoWifiConnector,
    private val settingsStore: AppSettingsStore,
    private val scope: CoroutineScope,
) {
    private val _uiState = MutableStateFlow(CompanionUiState(notice = CompanionNotice.Neutral("Loading shared data...")))
    val uiState: StateFlow<CompanionUiState> = _uiState
    private var pendingSettingsSave: NanoSettings? = null
    private var settingsSaveJob: Job? = null
    private var recheckJob: Job? = null
    private var connectionCheckJob: Job? = null
    private var articleFetchJob: Job? = null
    private var suppressedRememberPrompt: RememberedNano? = null
    private val current: CompanionUiState
        get() = _uiState.value

    init {
        nanoNetworkController.start()
        scope.launch {
            val appSettings = withContext(Dispatchers.Default) { settingsStore.load() }
            updateState { 
                it.copy(
                    rememberedNano = appSettings.rememberedNano,
                    firmwareNotificationsEnabled = appSettings.firmwareNotificationsEnabled,
                )
            }
        }
        observeNanoNetwork()
        observeNanoNetworkEvents()
        refresh()
    }

    fun connectNanoScan() {
        if (connectionCheckJob?.isActive == true) return
        connectionCheckJob = scope.launch {
            val rememberedNano = current.rememberedNano
            updateState {
                it.copy(
                    discoveredNanos = emptyList(),
                    connectionState = NanoConnectionState.CheckingReader(
                        rememberedNano,
                        NanoConnectionTransport.LocalNetwork,
                    ),
                    notice = CompanionNotice.Attention("Looking for your RSVP Nano..."),
                )
            }
            val endpoints = nanoNetworkController.discoverNanos()
            val endpoint = when {
                endpoints.isEmpty() -> {
                    updateState { it.copy(connectionState = NanoConnectionState.Disconnected) }
                    connectNano(rememberedNano)
                    return@launch
                }
                endpoints.size == 1 -> endpoints.first()
                else -> endpoints.firstOrNull { it.nano == rememberedNano }
            }
            if (endpoint == null) {
                updateState {
                    it.copy(
                        discoveredNanos = endpoints,
                        connectionState = NanoConnectionState.Disconnected,
                        notice = CompanionNotice.Attention("Choose which RSVP Nano to connect to."),
                    )
                }
                return@launch
            }

            connectLocalNano(endpoint)
        }
    }

    fun selectDiscoveredNano(endpoint: NanoEndpoint) {
        if (endpoint !in current.discoveredNanos) return
        connectionCheckJob = scope.launch { connectLocalNano(endpoint) }
    }

    fun cancelNanoSelection() {
        updateState {
            it.copy(
                discoveredNanos = emptyList(),
                connectionState = NanoConnectionState.Disconnected,
                notice = CompanionNotice.Neutral("Connection cancelled."),
            )
        }
    }

    private suspend fun connectLocalNano(endpoint: NanoEndpoint) {
        updateState {
            it.copy(
                discoveredNanos = emptyList(),
                connectionState = NanoConnectionState.CheckingReader(
                    endpoint.nano,
                    NanoConnectionTransport.LocalNetwork,
                ),
                notice = CompanionNotice.Attention("Connecting to ${endpoint.nano.ssid}..."),
            )
        }
        runCatching { refreshConnection(endpoint.baseUrl) }
            .onFailure {
                markDisconnected("Found ${endpoint.nano.ssid}, but the reader did not respond.")
            }
    }

    fun scanPermissionDenied() {
        setNotice(CompanionNotice.Attention("Wi-Fi permission was not granted. Use the Wi-Fi panel to join your Nano manually."))
    }

    fun requestWifiPermissions() {
        setNotice(CompanionNotice.Attention("Grant Wi-Fi permission so the app can find your RSVP Nano."))
    }

    fun wifiPermissionsBlocked() {
        setNotice(CompanionNotice.Error("Wi-Fi permission is blocked. Enable it in app settings to let the app find your RSVP Nano."))
    }

    fun setWifiSsidDraft(value: String) = updateState { it.copy(wifiSsidDraft = value) }

    fun setWifiPasswordDraft(value: String) = updateState { it.copy(wifiPasswordDraft = value) }

    fun setDraftTitle(value: String) = updateState { it.copy(draftTitle = value) }

    fun setDraftSourceUrl(value: String) = updateState { it.copy(draftSourceUrl = value) }

    fun setDraftBody(value: String) = updateState { it.copy(draftBody = value) }

    fun setRssFeedDraft(value: String) = updateState { it.copy(rssFeedDraft = value) }

    fun setSelectedCatalogThemeId(value: String) = updateState { it.copy(selectedCatalogThemeId = value) }

    fun refresh() {
        scope.launch {
            val startedAt = currentTimeMillis()
            updateState { it.copy(isRefreshing = true, notice = CompanionNotice.Neutral("Refreshing...")) }
            runCatching {
                val drafts = withContext(Dispatchers.Default) { companionController.refreshLocal() }
                updateState {
                    it.copy(
                        drafts = drafts,
                        notice = CompanionNotice.Neutral("Loaded ${drafts.size} drafts."),
                    )
                }
                if (!current.isConnected) {
                    setNotice(CompanionNotice.Neutral("Ready. Connect to your Nano when you want to sync."))
                } else {
                    verifyCurrentConnection()
                }
            }.onFailure { error ->
                updateState {
                    it.copy(notice = CompanionNotice.Error(error.message ?: "Refresh failed."))
                }
            }.also {
                val elapsed = currentTimeMillis() - startedAt
                if (elapsed < MIN_REFRESH_INDICATOR_MS) {
                    delay(MIN_REFRESH_INDICATOR_MS - elapsed)
                }
                updateState { it.copy(isRefreshing = false) }
            }
        }
    }

    fun refreshThemeCatalog() {
        scope.launch {
            runCatching {
                val catalogUrl = catalogUrl("themes/index.json")
                catalogUrl to companionController.fetchThemeCatalog(catalogUrl)
            }
                .onSuccess { (catalogUrl, themes) ->
                    updateState {
                        val selected = it.selectedCatalogThemeId.takeIf { id -> themes.any { theme -> theme.id == id } }
                            ?: themes.firstOrNull()?.id.orEmpty()
                        it.copy(
                            themeCatalog = themes,
                            themeCatalogUrl = catalogUrl,
                            selectedCatalogThemeId = selected,
                        )
                    }
                }
                .onFailure { error ->
                    setNotice(CompanionNotice.Error(error.message ?: "Online theme catalog could not be loaded."))
                }
        }
    }

    fun refreshFontCatalog() {
        scope.launch {
            runCatching {
                val catalogUrl = catalogUrl("fonts/index.json")
                catalogUrl to companionController.fetchFontCatalog(catalogUrl)
            }
                .onSuccess { (catalogUrl, fonts) ->
                    updateState {
                        it.copy(
                            fontCatalog = fonts,
                            fontCatalogUrl = catalogUrl,
                        )
                    }
                }
                .onFailure { error ->
                    setNotice(CompanionNotice.Error(error.message ?: "Online font catalog could not be loaded."))
                }
        }
    }

    fun recheckConnectionAfterResume() {
        recheckJob?.cancel()
        recheckJob = scope.launch {
            nanoNetworkController.refreshSnapshot()
            if (current.isConnected) {
                verifyCurrentConnection()
            }
        }
    }

    fun recheckConnectionAfterNetworkChange() {
        recheckJob?.cancel()
        recheckJob = scope.launch {
            if (current.isConnected) {
                verifyCurrentConnection()
            }
        }
    }

    private suspend fun verifyCurrentConnection() {
        val state = current
        if (!state.isConnected) return
        runCatching {
            withNanoApi {
                companionController.verifyReachableWithRetry(
                    baseUrl = state.baseUrl,
                    attempts = 2,
                    retryDelayMillis = 300,
                )
            }
        }.onFailure {
            if (current.isNanoWifiAttached) {
                setNotice(CompanionNotice.Attention("Nano Wi-Fi is connected, but the reader is not responding."))
            } else {
                markDisconnected("Reader disconnected. Reconnect to your Nano before continuing.")
            }
        }
    }

    private suspend fun ensureReaderReachable(action: String): Boolean {
        val state = current
        if (!state.isConnected) {
            setNotice(CompanionNotice.Error("Connect to your Nano before $action."))
            return false
        }
        return runCatching {
            withNanoApi {
                companionController.verifyReachableWithRetry(
                    baseUrl = state.baseUrl,
                    attempts = 1,
                    retryDelayMillis = 0,
                )
            }
        }.onFailure {
            markDisconnected("Reader disconnected. Reconnect to your Nano before $action.")
        }.isSuccess
    }

    fun updateSettings(transform: (NanoSettings) -> NanoSettings) {
        val state = current
        val currentSettings = state.settings
        if (!state.isConnected || currentSettings == null) {
            setNotice(CompanionNotice.Error("Connect to your Nano before saving settings."))
            return
        }

        val nextSettings = transform(currentSettings)
        updateState {
            it.copy(
                settings = nextSettings,
                isSavingSettings = true,
                notice = CompanionNotice.Neutral("Saving reader settings..."),
            )
        }
        enqueueSettingsSave(nextSettings)
    }

    fun setFirmwareNotificationsEnabled(enabled: Boolean) {
        scope.launch {
            firmwareUpdates.setNotificationsEnabled(enabled)
            updateState { it.copy(firmwareNotificationsEnabled = enabled) }
        }
    }

    private fun enqueueSettingsSave(settings: NanoSettings) {
        pendingSettingsSave = settings
        if (settingsSaveJob?.isActive == true) {
            return
        }

        settingsSaveJob = scope.launch {
            while (true) {
                val settingsToSave = pendingSettingsSave ?: break
                pendingSettingsSave = null
                val baseUrl = current.baseUrl
                if (!ensureReaderReachable("saving settings")) {
                    pendingSettingsSave = null
                    updateState { it.copy(isSavingSettings = false) }
                    break
                }

                val result = runCatching { withNanoApi { companionController.saveSettings(baseUrl, settingsToSave) } }
                if (result.isFailure) {
                    val error = result.exceptionOrNull()
                    pendingSettingsSave = null
                    updateState { it.copy(isSavingSettings = false) }
                    markDisconnected(error?.message ?: "Reader disconnected before saving settings.")
                    break
                }

                val snapshot = result.getOrThrow()
                firmwareUpdates.rememberDevice(current.firmwareVersion, current.otaAsset, snapshot.settings)
                updateState { state ->
                    if (pendingSettingsSave == null && state.settings == settingsToSave) {
                        state.copy(
                            settings = snapshot.settings,
                            isSavingSettings = false,
                            notice = CompanionNotice.Success("Reader settings applied."),
                        )
                    } else {
                        state
                    }
                }
            }
        }
    }

    fun saveWifiSettings() {
        scope.launch {
            val state = current
            if (!state.isConnected) {
                setNotice(CompanionNotice.Error("Connect to your Nano before saving Wi-Fi."))
                return@launch
            }
            val ssid = state.wifiSsidDraft.trim()
            if (ssid.isEmpty()) {
                setNotice(CompanionNotice.Error("Wi-Fi SSID is required."))
                return@launch
            }
            setNotice(CompanionNotice.Attention("Saving Wi-Fi settings..."))
            if (!ensureReaderReachable("saving Wi-Fi")) return@launch
            runCatching { withNanoApi { companionController.saveWifiSettings(state.baseUrl, ssid, state.wifiPasswordDraft) } }
                .onSuccess { wifi ->
                    updateState {
                        it.copy(
                            wifiSettings = wifi,
                            settings = it.settings?.copy(network = NanoSettings.Network(wifiSsid = ssid)),
                            wifiSsidDraft = ssid,
                            wifiPasswordDraft = "",
                            notice = CompanionNotice.Success("Wi-Fi settings saved."),
                        )
                    }
                }
                .onFailure { error -> markDisconnected(error.message ?: "Reader disconnected before saving Wi-Fi.") }
        }
    }

    fun clearWifiSettings() {
        scope.launch {
            val state = current
            if (!state.isConnected) {
                setNotice(CompanionNotice.Error("Connect to your Nano before clearing Wi-Fi."))
                return@launch
            }
            setNotice(CompanionNotice.Attention("Clearing Wi-Fi settings..."))
            if (!ensureReaderReachable("clearing Wi-Fi")) return@launch
            runCatching { withNanoApi { companionController.clearWifiSettings(state.baseUrl) } }
                .onSuccess { wifi ->
                    updateState {
                        it.copy(
                            wifiSettings = wifi,
                            settings = it.settings?.copy(network = NanoSettings.Network()),
                            wifiSsidDraft = "",
                            wifiPasswordDraft = "",
                            notice = CompanionNotice.Success("Wi-Fi settings cleared."),
                        )
                    }
                }
                .onFailure { error -> markDisconnected(error.message ?: "Reader disconnected before clearing Wi-Fi.") }
        }
    }

    fun addRssFeed() {
        scope.launch {
            val state = current
            if (!state.isConnected) {
                setNotice(CompanionNotice.Error("Connect to your Nano before editing RSS feeds."))
                return@launch
            }
            val feed = state.rssFeedDraft.trim()
            if (!feed.startsWith("http://") && !feed.startsWith("https://")) {
                setNotice(CompanionNotice.Error("RSS feed URLs must start with http:// or https://."))
                return@launch
            }
            setNotice(CompanionNotice.Attention("Saving RSS feed on Nano..."))
            if (!ensureReaderReachable("editing RSS feeds")) return@launch
            runCatching {
                withNanoApi {
                    companionController.saveRssFeeds(
                        baseUrl = state.baseUrl,
                        feeds = state.rssFeeds + feed,
                    )
                }
            }.onSuccess { feeds ->
                updateState {
                    it.copy(
                        rssFeeds = feeds,
                        rssFeedDraft = "",
                        notice = CompanionNotice.Success("RSS feed saved on Nano."),
                    )
                }
            }.onFailure { error ->
                markDisconnected(error.message ?: "Reader disconnected before saving RSS feeds.")
            }
        }
    }

    fun deleteRssFeed(feed: String) {
        scope.launch {
            val state = current
            if (!state.isConnected) {
                setNotice(CompanionNotice.Error("Connect to your Nano before editing RSS feeds."))
                return@launch
            }
            val nextFeeds = state.rssFeeds.filterNot { it == feed }
            setNotice(CompanionNotice.Attention("Removing RSS feed from Nano..."))
            if (!ensureReaderReachable("editing RSS feeds")) return@launch
            runCatching {
                withNanoApi {
                    companionController.saveRssFeeds(
                        baseUrl = state.baseUrl,
                        feeds = nextFeeds,
                    )
                }
            }.onSuccess { feeds ->
                updateState {
                    it.copy(
                        rssFeeds = feeds,
                        notice = CompanionNotice.Success("RSS feed removed from Nano."),
                    )
                }
            }.onFailure { error ->
                markDisconnected(error.message ?: "Reader disconnected before removing RSS feeds.")
            }
        }
    }

    fun saveTextDraft() {
        scope.launch {
            val state = current
            val title = state.draftTitle.trim()
            val body = state.draftBody.trim()
            if (title.isEmpty() || body.isEmpty()) {
                setNotice(CompanionNotice.Error("Text drafts need a title and body."))
                return@launch
            }
            val existing = state.editingDraftId?.let { id -> state.drafts.firstOrNull { it.id == id } }
            val drafts = withContext(Dispatchers.Default) {
                companionController.saveDraft(
                    ImportPreparation.pendingUploadForText(
                        id = existing?.id ?: newId(),
                        title = title,
                        source = state.draftSourceUrl,
                        text = body,
                        createdAt = existing?.createdAt ?: SharedAppUtils.nowIso8601(),
                        fallbackTitle = "Untitled",
                    )
                )
            }
            clearDraftEditor(
                drafts = drafts,
                notice = if (existing == null) CompanionNotice.Success("Text draft saved locally.") else CompanionNotice.Success("Text draft updated."),
            )
        }
    }

    fun saveSharedImports(imports: List<SharedImport>) {
        scope.launch {
            val prepared = withContext(Dispatchers.Default) {
                imports.mapNotNull {
                    ImportPreparation.prepareSharedImport(
                        id = newId(),
                        title = it.title,
                        text = it.text,
                        source = it.source,
                        createdAt = SharedAppUtils.nowIso8601(),
                    )
                }
            }
            if (prepared.isEmpty()) {
                setNotice(CompanionNotice.Error("Shared item is not readable text or a URL."))
                return@launch
            }

            var drafts = current.drafts
            var fetchedCount = 0
            prepared.forEach { item ->
                val snapshot = withContext(Dispatchers.Default) {
                    companionController.saveDraftFetchingArticleIfNeeded(item)
                }
                drafts = snapshot.drafts
                if (snapshot.fetchedArticle) {
                    fetchedCount += 1
                }
            }
            updateState {
                it.copy(
                    drafts = drafts,
                    notice = sharedImportNotice(savedCount = prepared.size, fetchedCount = fetchedCount),
                )
            }
        }
    }

    fun fetchPendingArticlesWhenOnline() {
        if (articleFetchJob?.isActive == true) return
        articleFetchJob = scope.launch {
            val pending = current.drafts.filter(PendingUpload::needsArticleFetch)
            if (pending.isEmpty()) return@launch

            var drafts = current.drafts
            var fetchedCount = 0
            pending.forEach { item ->
                val snapshot = withContext(Dispatchers.Default) {
                    companionController.saveDraftFetchingArticleIfNeeded(item)
                }
                drafts = snapshot.drafts
                if (snapshot.fetchedArticle) {
                    fetchedCount += 1
                }
            }

            if (fetchedCount > 0) {
                updateState {
                    it.copy(
                        drafts = drafts,
                        notice = CompanionNotice.Success("Fetched $fetchedCount saved articles. Connect to your Nano to sync."),
                    )
                }
            }
        }
    }

    fun rememberCurrentNano() {
        val identity = currentRememberableNano()
        if (identity == null) {
            setNotice(CompanionNotice.Error("Connect to a Nano before remembering it."))
            return
        }
        scope.launch {
            withContext(Dispatchers.Default) {
                val currentSettings = settingsStore.load()
                settingsStore.save(currentSettings.copy(rememberedNano = identity))
            }
            suppressedRememberPrompt = null
            updateState {
                it.copy(
                    rememberedNano = identity,
                    canRememberCurrentNano = false,
                    notice = CompanionNotice.Success("Remembered ${identity.ssid}."),
                )
            }
        }
    }

    fun forgetRememberedNano() {
        scope.launch {
            val identity = currentRememberableNano()
            suppressedRememberPrompt = identity
            withContext(Dispatchers.Default) {
                val currentSettings = settingsStore.load()
                settingsStore.save(currentSettings.copy(rememberedNano = null))
            }
            updateState {
                it.copy(
                    rememberedNano = null,
                    canRememberCurrentNano = false,
                    notice = CompanionNotice.Success("Forgot remembered Nano."),
                )
            }
        }
    }

    private fun sharedImportNotice(savedCount: Int, fetchedCount: Int): CompanionNotice {
        return when {
            fetchedCount > 0 && savedCount == 1 -> {
                CompanionNotice.Success("Shared article fetched and saved. Connect to your Nano when you are ready to sync it.")
            }
            fetchedCount > 0 -> {
                CompanionNotice.Success("Saved $savedCount shared items and fetched $fetchedCount articles. Connect to your Nano to sync.")
            }
            savedCount == 1 -> {
                CompanionNotice.Attention("Shared link saved locally. It will fetch article text when the phone has internet again; then connect to your Nano to sync.")
            }
            else -> {
                CompanionNotice.Attention("Saved $savedCount shared items locally. URL-only drafts will fetch when the phone has internet again.")
            }
        }
    }

    fun saveLinkDraft() {
        scope.launch {
            val state = current
            val sourceUrl = state.draftSourceUrl.trim()
            if (!sourceUrl.startsWith("http://") && !sourceUrl.startsWith("https://")) {
                setNotice(CompanionNotice.Error("Saved links need an http:// or https:// URL."))
                return@launch
            }
            val title = state.draftTitle.trim().ifEmpty { hostName(sourceUrl).ifEmpty { "Saved Article" } }
            val existing = state.editingDraftId?.let { id -> state.drafts.firstOrNull { it.id == id } }
            val pending = ImportPreparation.pendingUploadForUrl(
                id = existing?.id ?: newId(),
                title = title,
                source = sourceUrl,
                host = hostName(sourceUrl),
                createdAt = existing?.createdAt ?: SharedAppUtils.nowIso8601(),
            )
            val snapshot = withContext(Dispatchers.Default) {
                companionController.saveDraftFetchingArticleIfNeeded(pending)
            }
            clearDraftEditor(
                drafts = snapshot.drafts,
                notice = when {
                    snapshot.fetchedArticle -> {
                        CompanionNotice.Success("Fetched and saved ${snapshot.item.title}. Connect to your Nano to sync it.")
                    }
                    existing == null -> {
                        CompanionNotice.Attention("Link saved locally. If article text was not fetched, edit it while online before syncing.")
                    }
                    else -> {
                        CompanionNotice.Attention("Link updated. If article text was not fetched, edit it while online before syncing.")
                    }
                },
            )
        }
    }

    fun editDraft(draft: PendingUpload) {
        updateState {
            it.copy(
                draftTitle = draft.title,
                draftSourceUrl = draft.sourceUrl.orEmpty(),
                draftBody = draft.body,
                editingDraftId = draft.id,
                notice = CompanionNotice.Neutral("Editing ${draft.title}."),
            )
        }
    }

    fun cancelDraftEdit() {
        clearDraftEditor(notice = CompanionNotice.Neutral("Edit cancelled."))
    }

    fun deleteDraft(draft: PendingUpload) {
        scope.launch {
            val drafts = withContext(Dispatchers.Default) {
                companionController.deleteDraft(draft)
            }
            if (current.editingDraftId == draft.id) {
                clearDraftEditor(drafts = drafts, notice = CompanionNotice.Success("Draft deleted."))
            } else {
                updateState { it.copy(drafts = drafts, notice = CompanionNotice.Success("Draft deleted.")) }
            }
        }
    }

    fun refreshRssFeeds() {
        scope.launch {
            val state = current
            if (!state.isConnected) {
                setNotice(CompanionNotice.Error("Connect to your Nano before refreshing RSS feeds."))
                return@launch
            }
            setNotice(CompanionNotice.Attention("Refreshing RSS feeds from Nano..."))
            if (!ensureReaderReachable("refreshing RSS feeds")) return@launch
            runCatching {
                withNanoApi {
                    companionController.refreshRssFeeds(baseUrl = state.baseUrl)
                }
            }.onSuccess { feeds ->
                updateState { it.copy(rssFeeds = feeds, notice = CompanionNotice.Success("RSS feeds loaded from Nano.")) }
            }.onFailure { error ->
                markDisconnected(error.message ?: "Reader disconnected before refreshing RSS feeds.")
            }
        }
    }

    fun syncSavedArticles() {
        scope.launch {
            val state = current
            if (!state.isConnected) {
                setNotice(CompanionNotice.Error("Connect to your Nano before syncing saved articles."))
                return@launch
            }
            val readyDrafts = state.drafts.filterNot(PendingUpload::needsArticleFetch)
            if (readyDrafts.isEmpty()) {
                setNotice(CompanionNotice.Error("No fetched articles are ready. Share links while online, or paste article text before syncing."))
                return@launch
            }
            setNotice(CompanionNotice.Attention("Syncing saved articles..."))
            if (!ensureReaderReachable("syncing saved articles")) return@launch
            runCatching {
                withNanoApi {
                    companionController.syncPendingUploads(
                        baseUrl = state.baseUrl,
                        items = readyDrafts,
                    )
                }
            }.onSuccess { synced ->
                updateState {
                    it.copy(
                        drafts = synced.drafts,
                        books = synced.books,
                        notice = CompanionNotice.Success("Synced ${synced.syncedCount} saved articles."),
                    )
                }
            }.onFailure { error ->
                markDisconnected(error.message ?: "Reader disconnected before syncing saved articles.")
            }
        }
    }

    fun deleteDeviceBook(book: NanoBook) {
        scope.launch {
            val state = current
            if (!state.isConnected) {
                setNotice(CompanionNotice.Error("Connect to your Nano before deleting books."))
                return@launch
            }
            val title = book.displayTitle
            setNotice(CompanionNotice.Attention("Deleting $title..."))
            if (!ensureReaderReachable("deleting books")) return@launch
            runCatching {
                withNanoApi { companionController.deleteBooks(state.baseUrl, listOf(book.id)) }
            }.onSuccess { books ->
                updateState { it.copy(books = books, notice = CompanionNotice.Success("Deleted $title.")) }
            }.onFailure { error -> markDisconnected(error.message ?: "Reader disconnected before deleting books.") }
        }
    }

    fun setBookPosition(book: NanoBook, wordIndex: Int) {
        scope.launch {
            val state = current
            if (!state.isConnected) {
                setNotice(CompanionNotice.Error("Connect to your Nano before setting book position."))
                return@launch
            }
            val title = book.displayTitle
            setNotice(CompanionNotice.Attention("Saving position for $title..."))
            if (!ensureReaderReachable("setting book position")) return@launch
            runCatching {
                withNanoApi { companionController.setBookPosition(state.baseUrl, book, wordIndex) }
            }.onSuccess { books ->
                updateState { it.copy(books = books, notice = CompanionNotice.Success("Saved position for $title.")) }
            }.onFailure { error -> markDisconnected(error.message ?: "Reader disconnected before setting book position.") }
        }
    }

    fun refreshLocaleCatalog() {
        scope.launch {
            runCatching {
                val catalogUrl = catalogUrl("locale-packs/index.json")
                catalogUrl to companionController.fetchLocaleCatalog(catalogUrl)
            }
                .onSuccess { (catalogUrl, locales) ->
                    updateState {
                        it.copy(
                            localeCatalog = locales,
                            localeCatalogUrl = catalogUrl,
                        )
                    }
                }
                .onFailure { error ->
                    setNotice(CompanionNotice.Error(error.message ?: "Online locale catalog could not be loaded."))
                }
        }
    }

    fun setBookLanguageFonts(book: NanoBook, languageFonts: List<NanoLanguageFont>) {
        scope.launch {
            val state = current
            if (!state.isConnected) {
                setNotice(CompanionNotice.Error("Connect to your Nano before configuring book fonts."))
                return@launch
            }
            val title = book.displayTitle
            setNotice(CompanionNotice.Attention("Saving language fonts for $title..."))
            if (!ensureReaderReachable("configuring book fonts")) return@launch
            runCatching {
                withNanoApi { companionController.setBookLanguageFonts(state.baseUrl, book, languageFonts) }
            }.onSuccess { books ->
                updateState {
                    it.copy(books = books, notice = CompanionNotice.Success("Saved language fonts for $title."))
                }
            }.onFailure { error ->
                markDisconnected(error.message ?: "Reader disconnected before saving book fonts.")
            }
        }
    }

    fun uploadSelectedFile(displayName: String, data: ByteArray) {
        scope.launch {
            val state = current
            if (!state.isConnected) {
                setNotice(CompanionNotice.Error("Connect to your Nano before uploading files."))
                return@launch
            }
            if (!ensureReaderReachable("uploading files")) return@launch
            updateBookJob(BookJob(active = BookJobStep.Convert, name = displayName))
            val file = runCatching {
                withContext(Dispatchers.Default) {
                    RsvpConverter.bookFile(data = data, filename = displayName)
                }
            }.onFailure { error ->
                updateState {
                    it.copy(
                        bookJob = null,
                        notice = CompanionNotice.Error(error.message ?: "Could not convert $displayName."),
                    )
                }
            }.getOrNull() ?: return@launch

            val jobName = file.title.ifBlank { displayName }
            updateBookJob(
                BookJob(
                    active = BookJobStep.Upload,
                    name = jobName,
                    done = listOf(BookJobStep.Convert),
                    progress = 0f,
                )
            )
            runCatching {
                withNanoApi {
                    companionController.uploadBook(
                        baseUrl = state.baseUrl,
                        file = file,
                        category = "book",
                        onProgress = { sent, total ->
                            updateBookJob(
                                BookJob(
                                    active = BookJobStep.Upload,
                                    name = jobName,
                                    done = listOf(BookJobStep.Convert),
                                    progress = uploadProgress(sent = sent, total = total),
                                )
                            )
                        },
                    )
                }
            }.onSuccess { books ->
                val uploadedName = current.bookJob?.name ?: jobName
                updateState {
                    it.copy(
                        books = books,
                        bookJob = null,
                        notice = CompanionNotice.Success("Uploaded $uploadedName."),
                    )
                }
            }.onFailure { error ->
                updateState { it.copy(bookJob = null) }
                markDisconnected(error.message ?: "Reader disconnected before uploading files.")
            }
        }
    }

    fun uploadThemeFile(displayName: String, data: ByteArray) {
        scope.launch {
            val state = current
            if (!state.isConnected) {
                setNotice(CompanionNotice.Error("Connect to your Nano before uploading themes."))
                return@launch
            }
            if (!displayName.endsWith(".toml", ignoreCase = true)) {
                setNotice(CompanionNotice.Error("Theme files must use the .toml extension."))
                return@launch
            }
            if (!ensureReaderReachable("uploading themes")) return@launch
            setNotice(CompanionNotice.Attention("Uploading $displayName..."))
            runCatching {
                withNanoApi {
                    companionController.uploadTheme(
                        baseUrl = state.baseUrl,
                        filename = displayName,
                        data = data,
                    )
                }
            }.onSuccess { snapshot ->
                updateState {
                    it.copy(
                        settings = snapshot.settings,
                        availableThemes = snapshot.themes,
                        notice = CompanionNotice.Success("Uploaded $displayName."),
                    )
                }
            }.onFailure { error ->
                setNotice(CompanionNotice.Error(themeUploadError(error)))
            }
        }
    }

    fun installSelectedOnlineTheme() {
        scope.launch {
            val state = current
            if (!state.isConnected) {
                setNotice(CompanionNotice.Error("Connect to your Nano before installing themes."))
                return@launch
            }
            val theme = state.themeCatalog.firstOrNull { it.id == state.selectedCatalogThemeId }
                ?: state.themeCatalog.firstOrNull()
            if (theme == null) {
                setNotice(CompanionNotice.Error("Load the online theme list first."))
                return@launch
            }

            setNotice(CompanionNotice.Attention("Downloading ${theme.name}..."))
            val catalogUrl = state.themeCatalogUrl.ifBlank { catalogUrl("themes/index.json") }
            val themeFile = runCatching {
                companionController.downloadTheme(catalogUrl, theme)
            }.onFailure { error ->
                setNotice(CompanionNotice.Error(error.message ?: "Theme download failed."))
            }.getOrNull() ?: return@launch

            if (!ensureReaderReachable("installing themes")) return@launch
            setNotice(CompanionNotice.Attention("Installing ${theme.name}..."))
            runCatching {
                withNanoApi {
                    companionController.uploadTheme(
                        baseUrl = state.baseUrl,
                        filename = themeFile.filename,
                        data = themeFile.data,
                    )
                }
            }.onSuccess { snapshot ->
                updateState {
                    it.copy(
                        settings = snapshot.settings,
                        availableThemes = snapshot.themes,
                        selectedCatalogThemeId = theme.id,
                        notice = CompanionNotice.Success("Installed ${theme.name}."),
                    )
                }
            }.onFailure { error ->
                setNotice(CompanionNotice.Error(themeUploadError(error)))
            }
        }
    }


    fun uploadFontFile(displayName: String, data: ByteArray) {
        scope.launch {
            val state = current
            if (!state.isConnected) {
                setNotice(CompanionNotice.Error("Connect to your Nano before uploading fonts."))
                return@launch
            }
            if (!displayName.endsWith(".rfont4", ignoreCase = true)) {
                setNotice(CompanionNotice.Error("Font files must use the .rfont4 extension."))
                return@launch
            }
            val family = inferFontFamily(displayName)
            if (!ensureReaderReachable("uploading fonts")) return@launch
            setNotice(CompanionNotice.Attention("Uploading $displayName..."))
            runCatching {
                withNanoApi {
                    companionController.uploadFont(
                        baseUrl = state.baseUrl,
                        family = family,
                        filename = displayName,
                        data = data,
                    )
                }
            }.onSuccess { snapshot ->
                updateState {
                    it.copy(
                        settings = snapshot.settings,
                        availableFonts = snapshot.fonts,
                        notice = CompanionNotice.Success("Uploaded $displayName as $family."),
                    )
                }
            }.onFailure { error ->
                setNotice(CompanionNotice.Error(fontUploadError(error)))
            }
        }
    }

    fun installOnlineFont(id: String) {
        scope.launch {
            val state = current
            if (!state.isConnected) {
                setNotice(CompanionNotice.Error("Connect to your Nano before installing fonts."))
                return@launch
            }
            val font = state.fontCatalog.firstOrNull { it.id == id }
            if (font == null) {
                setNotice(CompanionNotice.Error("Load the online font list first."))
                return@launch
            }
            setNotice(CompanionNotice.Attention("Downloading ${font.name}..."))
            val catalogUrl = state.fontCatalogUrl.ifBlank { catalogUrl("fonts/index.json") }
            val fontFile = runCatching {
                companionController.downloadFont(catalogUrl, font)
            }.onFailure { error ->
                setNotice(CompanionNotice.Error(error.message ?: "Font download failed."))
            }.getOrNull() ?: return@launch

            if (!ensureReaderReachable("installing fonts")) return@launch
            setNotice(CompanionNotice.Attention("Installing ${font.name}..."))
            runCatching {
                withNanoApi {
                    companionController.uploadFont(
                        baseUrl = state.baseUrl,
                        family = fontFile.family,
                        filename = fontFile.filename,
                        data = fontFile.data,
                    )
                }
            }.onSuccess { snapshot ->
                updateState {
                    it.copy(
                        settings = snapshot.settings,
                        availableFonts = snapshot.fonts,
                        notice = CompanionNotice.Success("Installed ${font.name}."),
                    )
                }
            }.onFailure { error ->
                setNotice(CompanionNotice.Error(fontUploadError(error)))
            }
        }
    }

    fun installLocalePackFile(displayName: String, data: ByteArray) {
        scope.launch {
            installLocalePack(displayName, data)
        }
    }

    fun removeFont(id: String) {
        scope.launch {
            val state = current
            if (!state.isConnected || !ensureReaderReachable("removing a font")) return@launch
            runCatching {
                withNanoApi { companionController.removeFont(state.baseUrl, id) }
            }.onSuccess { snapshot ->
                updateState {
                    it.copy(
                        settings = snapshot.settings,
                        availableFonts = snapshot.fonts,
                        notice = CompanionNotice.Success("Removed font $id."),
                    )
                }
            }.onFailure { error ->
                setNotice(CompanionNotice.Error(error.message ?: "Font removal failed."))
            }
        }
    }

    fun installOnlineLocalePack(id: String) {
        scope.launch {
            val state = current
            if (!state.isConnected) {
                setNotice(CompanionNotice.Error("Connect to your Nano before installing locale packs."))
                return@launch
            }
            val pack = state.localeCatalog.firstOrNull { it.id == id }
            if (pack == null) {
                setNotice(CompanionNotice.Error("Load the online locale list first."))
                return@launch
            }
            setNotice(CompanionNotice.Attention("Downloading ${pack.name}..."))
            val catalogUrl = state.localeCatalogUrl.ifBlank { catalogUrl("locale-packs/index.json") }
            val file = runCatching { companionController.downloadLocalePack(catalogUrl, pack) }
                .onFailure { error ->
                    setNotice(CompanionNotice.Error(error.message ?: "Locale-pack download failed."))
                }
                .getOrNull() ?: return@launch
            installLocalePack(file.filename, file.data)
        }
    }

    private suspend fun installLocalePack(displayName: String, data: ByteArray) {
        val state = current
        if (!state.isConnected) {
            setNotice(CompanionNotice.Error("Connect to your Nano before installing locale packs."))
            return
        }
        if (!displayName.endsWith(".zip", ignoreCase = true)) {
            setNotice(CompanionNotice.Error("Locale packs must use the .zip extension."))
            return
        }
        if (!ensureReaderReachable("installing a locale pack")) return
        setNotice(CompanionNotice.Attention("Installing $displayName..."))
        runCatching {
            withNanoApi {
                companionController.installLocalePack(state.baseUrl, displayName, data)
            }
        }.onSuccess { locales ->
            updateState {
                it.copy(
                    availableLocales = locales.locales,
                    notice = CompanionNotice.Success("Installed $displayName."),
                )
            }
        }.onFailure { error ->
            setNotice(CompanionNotice.Error(error.message ?: "Locale pack installation failed."))
        }
    }

    fun removeLocalePack(id: String) {
        scope.launch {
            val state = current
            if (!state.isConnected || !ensureReaderReachable("removing a locale pack")) return@launch
            runCatching {
                withNanoApi { companionController.removeLocalePack(state.baseUrl, id) }
            }.onSuccess { locales ->
                updateState {
                    it.copy(
                        availableLocales = locales.locales,
                        notice = CompanionNotice.Success("Removed locale pack $id."),
                    )
                }
            }.onFailure { error ->
                setNotice(CompanionNotice.Error(error.message ?: "Locale pack removal failed."))
            }
        }
    }

    private fun clearDraftEditor(
        drafts: List<PendingUpload> = current.drafts,
        notice: CompanionNotice,
    ) {
        updateState {
            it.copy(
                drafts = drafts,
                draftTitle = "",
                draftSourceUrl = "",
                draftBody = "",
                editingDraftId = null,
                notice = notice,
            )
        }
    }

    private fun setNotice(notice: CompanionNotice) = updateState { it.copy(notice = notice) }

    private fun updateBookJob(bookJob: BookJob) = updateState { it.copy(bookJob = bookJob) }

    private fun observeNanoNetwork() {
        scope.launch {
            nanoNetworkController.snapshot.collect { snapshot ->
                onNanoNetworkSnapshot(snapshot)
            }
        }
    }

    private fun observeNanoNetworkEvents() {
        scope.launch {
            nanoNetworkController.events.collect { event ->
                when (event) {
                    NanoWifiEvent.RequestUnavailable -> {
                        setNotice(CompanionNotice.Error("Android did not find a matching RSVP-Nano Wi-Fi network."))
                    }
                }
            }
        }
    }

    private fun onNanoNetworkSnapshot(snapshot: NanoWifiSnapshot) {
        val stateBefore = current
        val remembered = stateBefore.rememberedNano
        val currentIdentity = nanoIdentity(snapshot)
        val canRemember = canPromptToRemember(currentIdentity, remembered)
        updateState {
            it.copy(
                connectionState = snapshot.toConnectionState(previous = it.connectionState),
                canRememberCurrentNano = canRemember,
            )
        }
        
        when {
            snapshot.isAttached && !stateBefore.isConnected && !stateBefore.isCheckingReader -> {
                connectAccessPointApi()
            }
            !snapshot.isAttached && stateBefore.isNanoWifiAttached -> {
                markDisconnected("Reader disconnected.")
            }
        }
    }

    private fun connectAccessPointApi() {
        connectionCheckJob = scope.launch {
            updateState {
                it.copy(
                    baseUrl = SharedAppUtils.ACCESS_POINT_BASE_URL,
                    connectionState = NanoConnectionState.CheckingReader(
                        it.currentNano,
                        NanoConnectionTransport.AccessPoint,
                    ),
                )
            }
            try {
                runCatching {
                    withNanoApi { refreshConnection(SharedAppUtils.ACCESS_POINT_BASE_URL) }
                }.onFailure {
                    markDisconnected("Connected to Nano Wi-Fi, but the reader did not respond.")
                }
            } finally {
                updateState {
                    if (it.connectionState is NanoConnectionState.CheckingReader) {
                        it.copy(connectionState = NanoConnectionState.WifiAttached(it.currentNano))
                    } else {
                        it
                    }
                }
            }
        }
    }

    private fun connectNano(rememberedNano: RememberedNano?) {
        updateState {
            it.copy(
                notice = CompanionNotice.Attention(
                    rememberedNano?.let { nano -> "Connecting to remembered Nano ${nano.ssid}..." }
                        ?: "Searching for RSVP Nano Wi-Fi...",
                ),
            )
        }
        when (val result = nanoNetworkController.requestNanoNetwork(rememberedNano)) {
            NanoWifiRequestResult.Started -> Unit
            NanoWifiRequestResult.AlreadyAttached -> {
                connectAccessPointApi()
            }
            NanoWifiRequestResult.AlreadyRequesting -> Unit
            NanoWifiRequestResult.MissingPermissions -> {
                setNotice(CompanionNotice.Error("Wi-Fi permission is needed to find your Nano from the app."))
            }
            is NanoWifiRequestResult.Failed -> {
                setNotice(CompanionNotice.Error(result.reason))
            }
        }
    }

    private suspend fun refreshConnection(baseUrl: String) {
        val snapshot = withTimeout(8_000) {
            companionController.connectWithRetry(baseUrl)
        }
        val device = snapshot.device
        val deviceName = device.info?.name ?: "RSVP Nano"
        val apiIdentity = NanoWifiIdentity.rememberedNanoOrNull(device.info?.networkSsid)
        val currentIdentity = nanoNetworkController.snapshot.value.currentNano ?: current.currentNano ?: apiIdentity
        updateState {
            val nextConnectionState = if (device.info != null) {
                NanoConnectionState.ReaderConnected(
                    currentIdentity ?: it.currentNano,
                    it.connectionState.transport ?: NanoConnectionTransport.LocalNetwork,
                )
            } else {
                it.connectionState
            }
            it.copy(
                books = device.books,
                settings = device.settings,
                availableThemes = device.themes,
                availableFonts = device.fonts,
                availableLocales = device.locales.locales,
                firmwareVersion = device.info?.firmwareVersion.orEmpty(),
                otaAsset = device.info?.otaAsset.orEmpty(),
                wifiSettings = device.wifiSettings,
                wifiSsidDraft = device.settings?.network?.wifiSsid.orEmpty(),
                wifiPasswordDraft = "",
                baseUrl = baseUrl,
                rssFeeds = snapshot.rssFeeds,
                drafts = snapshot.drafts,
                connectionState = nextConnectionState,
                canRememberCurrentNano = canPromptToRemember(currentIdentity, it.rememberedNano),
                notice = CompanionNotice.Success("Connected to $deviceName. ${device.summaryText}"),
            )
        }
        if (device.info != null && device.settings != null) {
            firmwareUpdates.rememberDevice(device.info, device.settings)
        }
        if (device.info != null && device.settings == null) {
            fetchMissingSettings(baseUrl)
        }
    }

    private suspend fun fetchMissingSettings(baseUrl: String) {
        runCatching {
            withNanoApi { companionController.refreshSettings(baseUrl) }
        }.onSuccess { snapshot ->
            firmwareUpdates.rememberDevice(current.firmwareVersion, current.otaAsset, snapshot.settings)
            updateState {
                it.copy(
                    settings = snapshot.settings,
                    wifiSettings = snapshot.wifiSettings ?: it.wifiSettings,
                    wifiSsidDraft = snapshot.settings.network.wifiSsid,
                    notice = CompanionNotice.Success("Reader settings loaded."),
                )
            }
        }.onFailure { error ->
            updateState {
                it.copy(notice = CompanionNotice.Attention("Connected, but reader settings could not be loaded: ${error.message ?: "unknown error"}."))
            }
        }
    }

    private fun markDisconnected(status: String) {
        updateState {
            it.copy(
                books = emptyList(),
                settings = null,
                availableThemes = emptyList(),
                availableFonts = emptyList(),
                firmwareVersion = "",
                otaAsset = "",
                wifiSettings = null,
                connectionState = NanoConnectionState.Disconnected,
                discoveredNanos = emptyList(),
                isSavingSettings = false,
                bookJob = null,
                notice = CompanionNotice.Error(status),
            )
        }
    }

    private fun updateState(transform: (CompanionUiState) -> CompanionUiState) {
        _uiState.update(transform)
    }

    private suspend fun <T> withNanoApi(block: suspend () -> T): T {
        return nanoNetworkController.withNanoNetwork(block)
    }

    private fun catalogUrl(path: String): String {
        val settings = current.settings ?: error("Connect to your Nano before loading catalogs.")
        val source = releaseSource(settings.updates.repositoryOwner, settings.updates.releaseTag)
            ?: error("Configure a GitHub release owner on your Nano first.")
        return source.rawContentUrl(path)
    }

    private fun themeUploadError(error: Throwable): String {
        val message = error.message.orEmpty()
        return if ("HTTP 404" in message) {
            "Reader firmware does not support theme upload yet. Flash this firmware build first."
        } else {
            message.ifBlank { "Theme upload failed." }
        }
    }

    private fun fontUploadError(error: Throwable): String {
        val message = error.message.orEmpty()
        return if ("HTTP 404" in message) {
            "Reader firmware does not support font upload yet. Flash this firmware build first."
        } else {
            message.ifBlank { "Font upload failed." }
        }
    }

    private fun inferFontFamily(filename: String): String =
        filename.substringBeforeLast('.')
            .ifBlank { "Custom Font" }

    private fun currentRememberableNano(): RememberedNano? {
        return current.currentNano ?: nanoIdentity(nanoNetworkController.snapshot.value)
    }

    private fun nanoIdentity(snapshot: NanoWifiSnapshot): RememberedNano? {
        return snapshot.currentNano ?: current.currentNano
    }

    private fun canPromptToRemember(
        currentNano: RememberedNano?,
        rememberedNano: RememberedNano?,
    ): Boolean {
        return currentNano != null &&
            currentNano != rememberedNano &&
            currentNano != suppressedRememberPrompt
    }

    fun close() {
        nanoNetworkController.stop()
    }

    private fun hostName(url: String): String {
        return url.substringAfter("://", url).substringBefore("/")
    }

    private fun uploadProgress(sent: Long, total: Long): Float? {
        if (total <= 0L) return null
        return (sent.toFloat() / total.toFloat()).coerceIn(0f, 1f)
    }

    private fun currentTimeMillis(): Long = Clock.System.now().toEpochMilliseconds()

    @OptIn(ExperimentalUuidApi::class)
    private fun newId(): String = Uuid.random().toString()

    private companion object {
        const val MIN_REFRESH_INDICATOR_MS = 650L
    }
}


