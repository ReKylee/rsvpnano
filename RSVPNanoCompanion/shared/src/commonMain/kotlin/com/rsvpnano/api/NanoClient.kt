package com.rsvpnano.api

import com.rsvpnano.models.NanoBook
import com.rsvpnano.models.NanoRssFeeds
import com.rsvpnano.models.NanoFocusTimers
import com.rsvpnano.models.NanoInfo
import com.rsvpnano.models.NanoSettings
import com.rsvpnano.models.NanoThemeCatalogItem
import com.rsvpnano.models.NanoThemeSummary
import com.rsvpnano.models.NanoFontCatalogItem
import com.rsvpnano.models.NanoFontSummary
import com.rsvpnano.models.NanoUploadResponse
import com.rsvpnano.models.NanoWifiSettings
import com.rsvpnano.models.FirmwareRelease
import com.rsvpnano.models.NanoLocalesResponse
import com.rsvpnano.models.NanoLanguageFont

/**
 * Lightweight API client interface for device interactions. Implement with Ktor in commonMain
 * or provide a platform-backed implementation if preferred.
 */
interface NanoClient {
    suspend fun fetchFirmwareRelease(owner: String, repository: String, tag: String): FirmwareRelease =
        throw NanoClientError("Firmware release lookup is not supported by this client.")
    suspend fun fetchInfo(baseUrl: String): NanoInfo
    suspend fun listBooks(baseUrl: String): List<NanoBook>
    suspend fun fetchSettings(baseUrl: String): NanoSettings
    suspend fun updateSettings(baseUrl: String, settings: NanoSettings): NanoSettings
    suspend fun fetchWifiSettings(baseUrl: String): NanoWifiSettings
    suspend fun updateWifi(baseUrl: String, ssid: String, password: String): NanoWifiSettings
    suspend fun forgetWifi(baseUrl: String): NanoWifiSettings
    suspend fun fetchRssFeeds(baseUrl: String): NanoRssFeeds
    suspend fun updateRssFeeds(baseUrl: String, config: NanoRssFeeds): NanoRssFeeds
    suspend fun fetchFocusTimers(baseUrl: String): NanoFocusTimers =
        throw NanoClientError("Focus timers are not supported by this client.")
    suspend fun updateFocusTimers(baseUrl: String, timers: NanoFocusTimers): NanoFocusTimers =
        throw NanoClientError("Focus timers are not supported by this client.")
    suspend fun uploadBook(
        baseUrl: String,
        name: String,
        data: ByteArray,
        category: String? = null,
        onProgress: ((sent: Long, total: Long) -> Unit)? = null,
    ): NanoUploadResponse
    suspend fun uploadTheme(
        baseUrl: String,
        name: String,
        data: ByteArray,
        onProgress: ((sent: Long, total: Long) -> Unit)? = null,
    ): NanoUploadResponse = throw NanoClientError("Theme upload is not supported by this client.")
    suspend fun uploadFont(
        baseUrl: String,
        family: String,
        name: String,
        data: ByteArray,
        onProgress: ((sent: Long, total: Long) -> Unit)? = null,
    ): NanoUploadResponse = throw NanoClientError("Font upload is not supported by this client.")
    suspend fun fetchThemeCatalog(url: String): List<NanoThemeCatalogItem> =
        throw NanoClientError("Theme catalog download is not supported by this client.")
    suspend fun fetchThemes(baseUrl: String): List<NanoThemeSummary> =
        throw NanoClientError("Theme listing is not supported by this client.")
    suspend fun downloadTheme(url: String): ByteArray =
        throw NanoClientError("Theme download is not supported by this client.")
    suspend fun fetchFontCatalog(url: String): List<NanoFontCatalogItem> =
        throw NanoClientError("Font catalog download is not supported by this client.")
    suspend fun fetchFonts(baseUrl: String): List<NanoFontSummary> =
        throw NanoClientError("Font listing is not supported by this client.")
    suspend fun downloadFont(url: String): ByteArray =
        throw NanoClientError("Font download is not supported by this client.")
    suspend fun fetchLocales(baseUrl: String): NanoLocalesResponse =
        throw NanoClientError("Locale-pack listing is not supported by this client.")
    suspend fun beginLocalePackStage(baseUrl: String, id: String): NanoUploadResponse =
        throw NanoClientError("Locale-pack installation is not supported by this client.")
    suspend fun uploadLocalePackFile(
        baseUrl: String,
        id: String,
        path: String,
        data: ByteArray,
        onProgress: ((sent: Long, total: Long) -> Unit)? = null,
    ): NanoUploadResponse = throw NanoClientError("Locale-pack installation is not supported by this client.")
    suspend fun activateLocalePack(baseUrl: String, id: String): NanoUploadResponse =
        throw NanoClientError("Locale-pack activation is not supported by this client.")
    suspend fun deleteLocalePack(baseUrl: String, id: String): NanoUploadResponse =
        throw NanoClientError("Locale-pack removal is not supported by this client.")
    suspend fun deleteBook(baseUrl: String, id: String): NanoUploadResponse
    suspend fun setBookPosition(
        baseUrl: String,
        id: String,
        wordIndex: Int,
    ): NanoUploadResponse
    suspend fun setBookLanguageFonts(
        baseUrl: String,
        id: String,
        languageFonts: List<NanoLanguageFont>,
    ): NanoUploadResponse = throw NanoClientError("Book language fonts are not supported by this client.")
}
