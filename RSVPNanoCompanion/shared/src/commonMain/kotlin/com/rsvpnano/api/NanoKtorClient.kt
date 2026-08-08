package com.rsvpnano.api

import com.rsvpnano.models.NanoBook
import com.rsvpnano.models.NanoRssFeeds
import com.rsvpnano.models.NanoFocusTimers
import com.rsvpnano.models.NanoInfo
import com.rsvpnano.models.NanoSettings
import com.rsvpnano.models.NanoThemeCatalogItem
import com.rsvpnano.models.NanoThemeSummary
import com.rsvpnano.models.NanoThemesResponse
import com.rsvpnano.models.NanoFontCatalogItem
import com.rsvpnano.models.NanoFontSummary
import com.rsvpnano.models.NanoFontsResponse
import com.rsvpnano.models.NanoUploadResponse
import com.rsvpnano.models.NanoWifiSettings
import com.rsvpnano.models.NanoWifiUpdate
import com.rsvpnano.models.FirmwareRelease
import com.rsvpnano.models.NanoLocalesResponse
import com.rsvpnano.models.NanoLocaleCatalogItem
import com.rsvpnano.models.NanoLanguageFont
import io.ktor.client.HttpClient
import io.ktor.client.call.body
import io.ktor.client.plugins.onUpload
import io.ktor.client.request.delete
import io.ktor.client.request.forms.MultiPartFormDataContent
import io.ktor.client.request.forms.formData
import io.ktor.client.request.get
import io.ktor.client.request.post
import io.ktor.client.request.put
import io.ktor.client.request.patch
import io.ktor.client.request.setBody
import io.ktor.http.ContentType
import io.ktor.http.HttpHeaders
import io.ktor.http.HttpStatusCode
import io.ktor.http.URLBuilder
import io.ktor.http.appendPathSegments
import io.ktor.http.contentType
import io.ktor.http.isSuccess
import kotlinx.serialization.builtins.ListSerializer
import kotlinx.serialization.json.Json
import kotlinx.serialization.Serializable
import kotlinx.serialization.SerialName

class NanoKtorClient(
    private val httpClient: HttpClient,
    private val json: Json = Json {
        ignoreUnknownKeys = true
        encodeDefaults = true
        explicitNulls = false
    },
) : NanoClient {
    override suspend fun fetchFirmwareRelease(owner: String, repository: String, tag: String): FirmwareRelease {
        val url = URLBuilder("https://api.github.com").apply {
            appendPathSegments("repos", owner, repository, "releases")
            if (tag.isBlank()) {
                appendPathSegments("latest")
            } else {
                appendPathSegments("tags", tag)
            }
        }.build()
        val response = httpClient.get(url) {
            headers.append(HttpHeaders.Accept, "application/vnd.github+json")
            headers.append(HttpHeaders.UserAgent, "RSVP-Nano-Companion")
        }
        if (!response.status.isSuccess()) {
            throw NanoClientError("Firmware release lookup returned HTTP ${response.status}")
        }
        val release = json.decodeFromString(GithubRelease.serializer(), response.body<String>())
        val commit = release.targetCommitish.trim()
        if (commit.length != 40 || !commit.all { it.digitToIntOrNull(16) != null }) {
            throw NanoClientError("Firmware release target is not a commit SHA.")
        }
        return FirmwareRelease(
            version = "${release.tagName}+${commit.take(12)}",
            assets = release.assets.map(GithubAsset::name),
        )
    }

    override suspend fun fetchInfo(baseUrl: String): NanoInfo =
        requestData(baseUrl, "api/v1/device", NanoInfo.serializer())

    override suspend fun listBooks(baseUrl: String): List<NanoBook> {
        return requestData(baseUrl, "api/v1/library", DeviceBooksResponse.serializer()).books
    }

    override suspend fun fetchSettings(baseUrl: String): NanoSettings =
        requestData(baseUrl, "api/v1/settings", NanoSettings.serializer())

    override suspend fun fetchThemes(baseUrl: String): List<NanoThemeSummary> =
        requestData(baseUrl, "api/v1/appearance/themes", NanoThemesResponse.serializer()).themes

    override suspend fun fetchFonts(baseUrl: String): List<NanoFontSummary> =
        requestData(baseUrl, "api/v1/appearance/fonts", NanoFontsResponse.serializer()).fonts

    override suspend fun deleteFont(baseUrl: String, id: String): NanoUploadResponse {
        val response = httpClient.delete(
            buildUrl(baseUrl, "api/v1/appearance/fonts", query = listOf("id" to id))
        )
        return decodeDeviceResponse(response.status, response.body<String>(), NanoUploadResponse.serializer())
    }

    override suspend fun fetchLocales(baseUrl: String): NanoLocalesResponse =
        requestData(baseUrl, "api/v1/locales", NanoLocalesResponse.serializer())

    override suspend fun fetchLocaleCatalog(url: String): List<NanoLocaleCatalogItem> {
        val response = httpClient.get(url)
        if (!response.status.isSuccess()) {
            throw NanoClientError("Locale-pack catalog returned HTTP ${response.status}")
        }
        return json.decodeFromString(ListSerializer(NanoLocaleCatalogItem.serializer()), response.body<String>())
    }

    override suspend fun downloadLocalePack(url: String): ByteArray {
        val response = httpClient.get(url)
        if (!response.status.isSuccess()) {
            throw NanoClientError("Locale-pack download returned HTTP ${response.status}")
        }
        return response.body()
    }

    override suspend fun updateSettings(baseUrl: String, settings: NanoSettings): NanoSettings {
        val response = httpClient.put(buildUrl(baseUrl, "api/v1/settings")) {
            contentType(ContentType.Application.Json)
            setBody(settings)
        }
        val body = response.body<String>()
        return decodeDeviceResponse(response.status, body, NanoSettings.serializer())
    }

    override suspend fun fetchWifiSettings(baseUrl: String): NanoWifiSettings =
        requestData(baseUrl, "api/v1/network", NanoWifiSettings.serializer())

    override suspend fun updateWifi(baseUrl: String, ssid: String, password: String): NanoWifiSettings {
        val response = httpClient.put(buildUrl(baseUrl, "api/v1/network")) {
            contentType(ContentType.Application.Json)
            setBody(NanoWifiUpdate(ssid = ssid, password = password))
        }
        val body = response.body<String>()
        return decodeDeviceResponse(response.status, body, NanoWifiSettings.serializer())
    }

    override suspend fun forgetWifi(baseUrl: String): NanoWifiSettings {
        val response = httpClient.delete(buildUrl(baseUrl, "api/v1/network"))
        val body = response.body<String>()
        return decodeDeviceResponse(response.status, body, NanoWifiSettings.serializer())
    }

    override suspend fun fetchRssFeeds(baseUrl: String): NanoRssFeeds =
        requestData(baseUrl, "api/v1/feeds", NanoRssFeeds.serializer())

    override suspend fun updateRssFeeds(baseUrl: String, config: NanoRssFeeds): NanoRssFeeds {
        val response = httpClient.put(buildUrl(baseUrl, "api/v1/feeds")) {
            contentType(ContentType.Application.Json)
            setBody(config)
        }
        val body = response.body<String>()
        return decodeDeviceResponse(response.status, body, NanoRssFeeds.serializer())
    }

    override suspend fun fetchFocusTimers(baseUrl: String): NanoFocusTimers =
        requestData(baseUrl, "api/v1/focus", NanoFocusTimers.serializer())

    override suspend fun updateFocusTimers(baseUrl: String, timers: NanoFocusTimers): NanoFocusTimers {
        val response = httpClient.put(buildUrl(baseUrl, "api/v1/focus")) {
            contentType(ContentType.Application.Json)
            setBody(timers)
        }
        return decodeDeviceResponse(response.status, response.body<String>(), NanoFocusTimers.serializer())
    }

    override suspend fun uploadBook(
        baseUrl: String,
        name: String,
        data: ByteArray,
        category: String?,
        onProgress: ((sent: Long, total: Long) -> Unit)?,
    ): NanoUploadResponse {
        val response = httpClient.post(
            buildUrl(
                baseUrl = baseUrl,
                path = "api/v1/library",
                query = listOfNotNull("name" to name, category?.let { "category" to it }),
            )
        ) {
            setBody(
                MultiPartFormDataContent(
                    formData {
                        append("file", data, headers = io.ktor.http.Headers.build {
                            append(HttpHeaders.ContentDisposition, "form-data; name=\"file\"; filename=\"$name\"")
                            append(HttpHeaders.ContentType, ContentType.Application.OctetStream.toString())
                        })
                    }
                )
            )
            onProgress?.let { progress ->
                onUpload { sent, total ->
                    progress(sent, total ?: data.size.toLong())
                }
            }
        }

        val body = response.body<String>()
        return decodeDeviceResponse(response.status, body, NanoUploadResponse.serializer())
    }

    override suspend fun uploadTheme(
        baseUrl: String,
        name: String,
        data: ByteArray,
        onProgress: ((sent: Long, total: Long) -> Unit)?,
    ): NanoUploadResponse {
        val response = httpClient.post(buildUrl(baseUrl, "api/v1/appearance/themes", query = listOf("name" to name))) {
            setBody(
                MultiPartFormDataContent(
                    formData {
                        append("file", data, headers = io.ktor.http.Headers.build {
                            append(HttpHeaders.ContentDisposition, "form-data; name=\"file\"; filename=\"$name\"")
                            append(HttpHeaders.ContentType, ContentType.Application.OctetStream.toString())
                        })
                    }
                )
            )
            onProgress?.let { progress ->
                onUpload { sent, total ->
                    progress(sent, total ?: data.size.toLong())
                }
            }
        }

        val body = response.body<String>()
        return decodeDeviceResponse(response.status, body, NanoUploadResponse.serializer())
    }

    override suspend fun uploadFont(
        baseUrl: String,
        family: String,
        name: String,
        data: ByteArray,
        onProgress: ((sent: Long, total: Long) -> Unit)?,
    ): NanoUploadResponse {
        val response = httpClient.post(
            buildUrl(
                baseUrl = baseUrl,
                path = "api/v1/appearance/fonts",
                query = listOf("family" to family),
            )
        ) {
            setBody(
                MultiPartFormDataContent(
                    formData {
                        append("file", data, headers = io.ktor.http.Headers.build {
                            append(HttpHeaders.ContentDisposition, "form-data; name=\"file\"; filename=\"$name\"")
                            append(HttpHeaders.ContentType, ContentType.Application.OctetStream.toString())
                        })
                    }
                )
            )
            onProgress?.let { progress ->
                onUpload { sent, total ->
                    progress(sent, total ?: data.size.toLong())
                }
            }
        }

        val body = response.body<String>()
        return decodeDeviceResponse(response.status, body, NanoUploadResponse.serializer())
    }

    override suspend fun fetchThemeCatalog(url: String): List<NanoThemeCatalogItem> {
        val response = httpClient.get(url)
        if (!response.status.isSuccess()) {
            throw NanoClientError("Theme catalog returned HTTP ${response.status}")
        }
        return json.decodeFromString(ListSerializer(NanoThemeCatalogItem.serializer()), response.body<String>())
    }

    override suspend fun downloadTheme(url: String): ByteArray {
        val response = httpClient.get(url)
        if (!response.status.isSuccess()) {
            throw NanoClientError("Theme download returned HTTP ${response.status}")
        }
        return response.body()
    }

    override suspend fun fetchFontCatalog(url: String): List<NanoFontCatalogItem> {
        val response = httpClient.get(url)
        if (!response.status.isSuccess()) {
            throw NanoClientError("Font catalog returned HTTP ${response.status}")
        }
        return json.decodeFromString(ListSerializer(NanoFontCatalogItem.serializer()), response.body<String>())
    }

    override suspend fun downloadFont(url: String): ByteArray {
        val response = httpClient.get(url)
        if (!response.status.isSuccess()) {
            throw NanoClientError("Font download returned HTTP ${response.status}")
        }
        return response.body()
    }

    override suspend fun beginLocalePackStage(baseUrl: String, id: String): NanoUploadResponse {
        val response = httpClient.post(buildUrl(baseUrl, "api/v1/locales/$id/stage"))
        return decodeDeviceResponse(response.status, response.body<String>(), NanoUploadResponse.serializer())
    }

    override suspend fun uploadLocalePackFile(
        baseUrl: String,
        id: String,
        path: String,
        data: ByteArray,
        onProgress: ((sent: Long, total: Long) -> Unit)?,
    ): NanoUploadResponse {
        val response = httpClient.post(
            buildUrl(baseUrl, "api/v1/locales/$id/files", query = listOf("path" to path))
        ) {
            setBody(
                MultiPartFormDataContent(
                    formData {
                        append("file", data, headers = io.ktor.http.Headers.build {
                            append(HttpHeaders.ContentDisposition, "form-data; name=\"file\"; filename=\"${path.substringAfterLast('/')}\"")
                            append(HttpHeaders.ContentType, ContentType.Application.OctetStream.toString())
                        })
                    }
                )
            )
            onProgress?.let { progress ->
                onUpload { sent, total -> progress(sent, total ?: data.size.toLong()) }
            }
        }
        return decodeDeviceResponse(response.status, response.body<String>(), NanoUploadResponse.serializer())
    }

    override suspend fun activateLocalePack(baseUrl: String, id: String): NanoUploadResponse {
        val response = httpClient.post(buildUrl(baseUrl, "api/v1/locales/$id/activate"))
        return decodeDeviceResponse(response.status, response.body<String>(), NanoUploadResponse.serializer())
    }

    override suspend fun deleteLocalePack(baseUrl: String, id: String): NanoUploadResponse {
        val response = httpClient.delete(buildUrl(baseUrl, "api/v1/locales/$id"))
        return decodeDeviceResponse(response.status, response.body<String>(), NanoUploadResponse.serializer())
    }

    override suspend fun deleteBook(baseUrl: String, id: String): NanoUploadResponse {
        val response = httpClient.delete(buildUrl(baseUrl, "api/v1/library", query = listOf("id" to id)))
        val body = response.body<String>()
        return decodeDeviceResponse(response.status, body, NanoUploadResponse.serializer())
    }

    override suspend fun setBookPosition(
        baseUrl: String,
        id: String,
        wordIndex: Int,
    ): NanoUploadResponse {
        val response = httpClient.patch(buildUrl(baseUrl, "api/v1/library/position")) {
            contentType(ContentType.Application.Json)
            setBody(
                BookPositionUpdate(
                    id = id,
                    wordIndex = wordIndex,
                )
            )
        }
        val body = response.body<String>()
        return decodeDeviceResponse(response.status, body, NanoUploadResponse.serializer())
    }

    override suspend fun setBookLanguageFonts(
        baseUrl: String,
        id: String,
        languageFonts: List<NanoLanguageFont>,
    ): NanoUploadResponse {
        val response = httpClient.patch(buildUrl(baseUrl, "api/v1/library/language-fonts")) {
            contentType(ContentType.Application.Json)
            setBody(BookLanguageFontsUpdate(id, languageFonts))
        }
        return decodeDeviceResponse(response.status, response.body<String>(), NanoUploadResponse.serializer())
    }

    private suspend fun <T> requestData(
        baseUrl: String,
        path: String,
        serializer: kotlinx.serialization.KSerializer<T>,
    ): T {
        val response = httpClient.get(buildUrl(baseUrl, path))
        return decodeDeviceResponse(response.status, response.body<String>(), serializer)
    }

    private fun buildUrl(baseUrl: String, path: String, query: List<Pair<String, String>> = emptyList()) = URLBuilder(baseUrl).apply {
        appendPathSegments(path.split('/').filter { it.isNotBlank() })
        query.forEach { (name, value) -> parameters.append(name, value) }
    }.build()

    private fun <T> decodeDeviceResponse(
        status: HttpStatusCode,
        body: String,
        serializer: kotlinx.serialization.KSerializer<T>,
    ): T {
        if (!status.isSuccess()) {
            val decoded = runCatching { json.decodeFromString(ApiErrorEnvelope.serializer(), body) }.getOrNull()?.error
            throw NanoClientError(
                message = decoded?.message ?: "Device rejected request with HTTP $status",
                code = decoded?.code,
                field = decoded?.field,
                status = status.value,
            )
        }
        return runCatching { json.decodeFromString(ApiEnvelope.serializer(serializer), body).data }
            .getOrElse { cause ->
                throw NanoClientError(
                    message = "Device returned an invalid API response.",
                    code = "invalid_response",
                    status = status.value,
                    cause = cause,
                )
            }
    }

    @Serializable
    private data class ApiEnvelope<T>(val data: T)

    @Serializable
    private data class ApiErrorEnvelope(val error: ApiError)

    @Serializable
    private data class ApiError(val code: String, val message: String, val field: String? = null)

    @Serializable
    private data class DeviceBooksResponse(
        val books: List<NanoBook>,
    )

    @Serializable
    private data class BookPositionUpdate(
        val id: String,
        val wordIndex: Int,
    )

    @Serializable
    private data class BookLanguageFontsUpdate(
        val id: String,
        val languageFonts: List<NanoLanguageFont>,
    )

    @Serializable
    private data class GithubRelease(
        @SerialName("tag_name") val tagName: String,
        @SerialName("target_commitish") val targetCommitish: String,
        val assets: List<GithubAsset> = emptyList(),
    )

    @Serializable
    private data class GithubAsset(val name: String)
}
