package com.rsvpnano.api

import com.rsvpnano.models.NanoBook
import com.rsvpnano.models.NanoRssFeeds
import com.rsvpnano.models.NanoInfo
import com.rsvpnano.models.NanoSettings
import com.rsvpnano.models.NanoThemeCatalogItem
import com.rsvpnano.models.NanoFontCatalogItem
import com.rsvpnano.models.NanoUploadResponse
import com.rsvpnano.models.NanoWifiSettings

/**
 * Lightweight API client interface for device interactions. Implement with Ktor in commonMain
 * or provide a platform-backed implementation if preferred.
 */
interface NanoClient {
    suspend fun fetchInfo(baseUrl: String): NanoInfo
    suspend fun listBooks(baseUrl: String): List<NanoBook>
    suspend fun fetchSettings(baseUrl: String): NanoSettings
    suspend fun updateSettings(baseUrl: String, settings: NanoSettings): NanoSettings
    suspend fun fetchWifiSettings(baseUrl: String): NanoWifiSettings
    suspend fun updateWifi(baseUrl: String, ssid: String, password: String): NanoWifiSettings
    suspend fun forgetWifi(baseUrl: String): NanoWifiSettings
    suspend fun fetchRssFeeds(baseUrl: String): NanoRssFeeds
    suspend fun updateRssFeeds(baseUrl: String, feeds: List<String>): NanoRssFeeds
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
        size: String,
        name: String,
        data: ByteArray,
        onProgress: ((sent: Long, total: Long) -> Unit)? = null,
    ): NanoUploadResponse = throw NanoClientError("Font upload is not supported by this client.")
    suspend fun fetchThemeCatalog(url: String): List<NanoThemeCatalogItem> =
        throw NanoClientError("Theme catalog download is not supported by this client.")
    suspend fun downloadTheme(url: String): ByteArray =
        throw NanoClientError("Theme download is not supported by this client.")
    suspend fun fetchFontCatalog(url: String): List<NanoFontCatalogItem> =
        throw NanoClientError("Font catalog download is not supported by this client.")
    suspend fun downloadFont(url: String): ByteArray =
        throw NanoClientError("Font download is not supported by this client.")
    suspend fun deleteBook(baseUrl: String, id: String): NanoUploadResponse
    suspend fun setBookPosition(
        baseUrl: String,
        id: String,
        wordIndex: Int,
    ): NanoUploadResponse
}
