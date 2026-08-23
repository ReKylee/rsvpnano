package com.rsvpnano.api

import com.rsvpnano.models.FirmwareRelease
import com.rsvpnano.models.NanoFontCatalogItem
import com.rsvpnano.models.NanoLocaleCatalogItem
import com.rsvpnano.models.NanoThemeCatalogItem

interface RepositoryClient {
    fun close() = Unit

    suspend fun fetchFirmwareRelease(owner: String, repository: String, tag: String): FirmwareRelease
    suspend fun fetchThemeCatalog(url: String): List<NanoThemeCatalogItem>
    suspend fun downloadTheme(url: String, onProgress: ((received: Long, total: Long?) -> Unit)? = null): ByteArray
    suspend fun fetchFontCatalog(url: String): List<NanoFontCatalogItem>
    suspend fun downloadFont(url: String, onProgress: ((received: Long, total: Long?) -> Unit)? = null): ByteArray
    suspend fun fetchLocaleCatalog(url: String): List<NanoLocaleCatalogItem>
    suspend fun downloadLocalePack(url: String, onProgress: ((received: Long, total: Long?) -> Unit)? = null): ByteArray
}
