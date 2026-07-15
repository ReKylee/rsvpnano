package com.rsvpnano.android.ui

import androidx.lifecycle.ViewModel
import androidx.lifecycle.ViewModelProvider
import androidx.lifecycle.viewModelScope
import com.rsvpnano.app.NanoWifiConnector
import com.rsvpnano.app.NanoEndpoint
import com.rsvpnano.app.RsvpSharedApp
import com.rsvpnano.models.NanoBook
import com.rsvpnano.models.NanoSettings
import com.rsvpnano.models.PendingUpload
import com.rsvpnano.ui.CompanionPresenter
import com.rsvpnano.ui.SharedImport

class CompanionViewModel(
    sharedApp: RsvpSharedApp,
    nanoNetworkController: NanoWifiConnector,
) : ViewModel() {
    private val presenter = CompanionPresenter(
        sharedApp = sharedApp,
        nanoNetworkController = nanoNetworkController,
        settingsStore = sharedApp.appSettingsStore,
        scope = viewModelScope,
    )

    val uiState = presenter.uiState

    fun connectNanoScan() = presenter.connectNanoScan()
    fun selectDiscoveredNano(endpoint: NanoEndpoint) = presenter.selectDiscoveredNano(endpoint)
    fun cancelNanoSelection() = presenter.cancelNanoSelection()
    fun scanPermissionDenied() = presenter.scanPermissionDenied()
    fun requestWifiPermissions() = presenter.requestWifiPermissions()
    fun showHelpNotice() = presenter.showHelpNotice()
    fun wifiPermissionsBlocked() = presenter.wifiPermissionsBlocked()
    fun setWifiSsidDraft(value: String) = presenter.setWifiSsidDraft(value)
    fun setWifiPasswordDraft(value: String) = presenter.setWifiPasswordDraft(value)
    fun setDraftTitle(value: String) = presenter.setDraftTitle(value)
    fun setDraftSourceUrl(value: String) = presenter.setDraftSourceUrl(value)
    fun setDraftBody(value: String) = presenter.setDraftBody(value)
    fun setRssFeedDraft(value: String) = presenter.setRssFeedDraft(value)
    fun setSelectedCatalogThemeId(value: String) = presenter.setSelectedCatalogThemeId(value)
    fun setSelectedCatalogFontId(value: String) = presenter.setSelectedCatalogFontId(value)
    fun setSelectedCatalogFontSize(value: String) = presenter.setSelectedCatalogFontSize(value)
    fun refresh() = presenter.refresh()
    fun refreshThemeCatalog() = presenter.refreshThemeCatalog()
    fun refreshFontCatalog() = presenter.refreshFontCatalog()
    fun recheckConnectionAfterResume() = presenter.recheckConnectionAfterResume()
    fun recheckConnectionAfterNetworkChange() = presenter.recheckConnectionAfterNetworkChange()
    fun updateSettings(transform: (NanoSettings) -> NanoSettings) = presenter.updateSettings(transform)
    fun saveWifiSettings() = presenter.saveWifiSettings()
    fun clearWifiSettings() = presenter.clearWifiSettings()
    fun addRssFeed() = presenter.addRssFeed()
    fun deleteRssFeed(feed: String) = presenter.deleteRssFeed(feed)
    fun saveTextDraft() = presenter.saveTextDraft()
    fun saveSharedImports(imports: List<SharedImport>) = presenter.saveSharedImports(imports)
    fun fetchPendingArticlesWhenOnline() = presenter.fetchPendingArticlesWhenOnline()
    fun rememberCurrentNano() = presenter.rememberCurrentNano()
    fun forgetRememberedNano() = presenter.forgetRememberedNano()
    fun saveLinkDraft() = presenter.saveLinkDraft()
    fun editDraft(draft: PendingUpload) = presenter.editDraft(draft)
    fun cancelDraftEdit() = presenter.cancelDraftEdit()
    fun deleteDraft(draft: PendingUpload) = presenter.deleteDraft(draft)
    fun refreshRssFeeds() = presenter.refreshRssFeeds()
    fun syncSavedArticles() = presenter.syncSavedArticles()
    fun deleteDeviceBook(book: NanoBook) = presenter.deleteDeviceBook(book)
    fun setBookPosition(book: NanoBook, wordIndex: Int) = presenter.setBookPosition(book, wordIndex)
    fun uploadSelectedFile(displayName: String, data: ByteArray) = presenter.uploadSelectedFile(displayName, data)
    fun uploadThemeFile(displayName: String, data: ByteArray) = presenter.uploadThemeFile(displayName, data)
    fun uploadFontFile(displayName: String, data: ByteArray) = presenter.uploadFontFile(displayName, data)
    fun installSelectedOnlineTheme() = presenter.installSelectedOnlineTheme()
    fun installSelectedOnlineFont() = presenter.installSelectedOnlineFont()
    fun needsArticleFetch(draft: PendingUpload): Boolean = presenter.needsArticleFetch(draft)

    override fun onCleared() {
        presenter.close()
        super.onCleared()
    }

    class Factory(
        private val sharedApp: RsvpSharedApp,
        private val nanoNetworkController: NanoWifiConnector,
    ) : ViewModelProvider.Factory {
        @Suppress("UNCHECKED_CAST")
        override fun <T : ViewModel> create(modelClass: Class<T>): T {
            return CompanionViewModel(
                sharedApp = sharedApp,
                nanoNetworkController = nanoNetworkController,
            ) as T
        }
    }
}
