package com.rsvpnano

import com.rsvpnano.api.NanoKtorClient
import com.rsvpnano.api.NanoClientError
import io.ktor.client.HttpClient
import io.ktor.client.engine.mock.MockEngine
import io.ktor.client.engine.mock.respond
import io.ktor.client.plugins.contentnegotiation.ContentNegotiation
import io.ktor.http.ContentType
import io.ktor.http.HttpHeaders
import io.ktor.http.HttpMethod
import io.ktor.http.HttpStatusCode
import io.ktor.http.headersOf
import io.ktor.serialization.kotlinx.json.json
import kotlinx.coroutines.runBlocking
import kotlinx.serialization.json.Json
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFailsWith

class NanoKtorClientAndroidTest {
    @Test
    fun resolvesPublishedBuildVersionFromReleaseTag() = runBlocking {
        val seen = mutableListOf<String>()
        val client = NanoKtorClient(mockHttpClient { request ->
            seen += request.url.encodedPath
            when (request.url.encodedPath) {
                "/repos/reader/rsvpnano/releases/tags/preview-v0.0.9" ->
                    """{"tag_name":"preview-v0.0.9","assets":[{"name":"reader-ota.bin"}]}"""
                "/repos/reader/rsvpnano/commits/preview-v0.0.9" ->
                    "0123456789abcdef0123456789abcdef01234567"
                else -> error("Unexpected request: ${request.url}")
            }
        })

        val release = client.fetchFirmwareRelease("reader", "rsvpnano", "preview-v0.0.9")

        assertEquals("preview-v0.0.9+0123456789ab", release.version)
        assertEquals(listOf("reader-ota.bin"), release.assets)
        assertEquals(
            listOf(
                "/repos/reader/rsvpnano/releases/tags/preview-v0.0.9",
                "/repos/reader/rsvpnano/commits/preview-v0.0.9",
            ),
            seen,
        )
    }

    @Test
    fun fetchesDeviceSnapshotEndpoints() = runBlocking {
        val seen = mutableListOf<String>()
        val client = NanoKtorClient(mockHttpClient { request ->
            seen += "${request.method.value} ${request.url.encodedPath}"
            when (request.url.encodedPath) {
                "/api/v1/device" -> """{"data":{"name":"Nano","apiVersion":1}}"""
                "/api/v1/library" -> """{"data":{"books":[{"id":"b12345678","name":"books/Book.rsvp","category":"book","bytes":1234,"metadata":{"title":"Book","wordCount":1000,"chapterCount":1,"chapters":[{"title":"Chapter 1","wordIndex":0}]},"source":{"size":1234,"fingerprint":3456},"reading":{"wordIndex":249,"percent":24,"remainingWords":750,"estimatedMinutes":3,"currentChapter":{"number":1,"title":"Chapter 1"}}}]}}"""
                "/api/v1/feeds" -> """{"data":{"schemaVersion":1,"feeds":["https://example.com/feed"]}}"""
                "/api/v1/focus" -> """{"data":{"schemaVersion":1,"timers":[{"name":"Pomodoro","focusMinutes":25,"breakMinutes":5,"rounds":4}]}}"""
                else -> error("Unexpected request: ${request.url}")
            }
        })

        assertEquals("Nano", client.fetchInfo("http://device.local").name)
        val book = client.listBooks("http://device.local").single()
        assertEquals("b12345678", book.id)
        assertEquals("books/Book.rsvp", book.name)
        assertEquals("Book", book.metadata.title)
        assertEquals(1000, book.metadata.wordCount)
        assertEquals(249, book.reading?.wordIndex)
        assertEquals("Chapter 1", book.metadata.chapters.single().title)
        assertEquals(listOf("https://example.com/feed"), client.fetchRssFeeds("http://device.local").feeds)
        assertEquals("Pomodoro", client.fetchFocusTimers("http://device.local").timers.single().name)
        assertEquals(listOf("GET /api/v1/device", "GET /api/v1/library", "GET /api/v1/feeds", "GET /api/v1/focus"), seen)
    }

    @Test
    fun settingsUseCanonicalDocumentAndPut() = runBlocking {
        val seen = mutableListOf<String>()
        val client = NanoKtorClient(mockHttpClient { request ->
            seen += "${request.method.value} ${request.url.encodedPath}"
            val wpm = if (request.method == HttpMethod.Put) 450 else 300
            """{"data":{"schemaVersion":1,"reading":{"wpm":$wpm,"pauseMode":"sentenceEnd","phantomWords":true,"chapterScrollReversed":false,"footerMetric":"chapterTime","batteryLabel":"timeRemaining","batteryIconVisible":true,"batteryVisibleWhileReading":true,"chapterVisibleWhileReading":false,"progressVisibleWhileReading":true,"leftHanded":false,"typography":{"fontId":"literata","fontSizeIndex":1,"focusHighlight":true,"tracking":0,"anchor":30,"guideWidth":30,"guideGap":5},"pacing":{"longWordDelayMs":200,"complexWordDelayMs":250,"punctuationDelayMs":300}},"interface":{"brightnessPercent":70,"language":"english","standbyTimerIndex":1,"screensaver":"reaction","selectedThemeId":"night"},"network":{"wifiSsid":"Home"},"updates":{"automatic":true,"repositoryOwner":"reader","releaseTag":"preview"}}}"""
        })

        val fetched = client.fetchSettings("http://device.local")
        assertEquals("sentenceEnd", fetched.reading.pauseMode)
        assertEquals(250, fetched.reading.pacing.complexWordDelayMs)
        assertEquals("literata", fetched.reading.typography.fontId)
        assertEquals("reaction", fetched.`interface`.screensaver)
        assertEquals("Home", fetched.network.wifiSsid)
        assertEquals("reader", fetched.updates.repositoryOwner)

        val updated = client.updateSettings("http://device.local", fetched.withWpm(450))
        assertEquals(450, updated.reading.wpm)
        assertEquals(
            listOf("GET /api/v1/settings", "PUT /api/v1/settings"),
            seen,
        )
    }

    @Test
    fun networkApiKeepsSsidInDeviceSettings() = runBlocking {
        val seen = mutableListOf<String>()
        val client = NanoKtorClient(mockHttpClient { request ->
            seen += "${request.method.value} ${request.url.encodedPath}"
            """{"data":{"passwordSet":${request.method != HttpMethod.Delete}}}"""
        })

        assertEquals(true, client.fetchWifiSettings("http://device.local").passwordSet)
        assertEquals(true, client.updateWifi("http://device.local", "Home", "secret").passwordSet)
        assertEquals(false, client.forgetWifi("http://device.local").passwordSet)
        assertEquals(
            listOf(
                "GET /api/v1/network",
                "PUT /api/v1/network",
                "DELETE /api/v1/network",
            ),
            seen,
        )
    }

    @Test
    fun bookMutationsUseDeviceContract() = runBlocking {
        val seen = mutableListOf<String>()
        val client = NanoKtorClient(mockHttpClient { request ->
            seen += "${request.method.value} ${request.url.encodedPath}?${request.url.encodedQuery}"
            when (request.method) {
                HttpMethod.Post -> {
                    assertEquals("Story.rsvp", request.url.parameters["name"])
                    assertEquals("article", request.url.parameters["category"])
                    """{"data":{"path":"/books/articles/Story.rsvp"}}"""
                }
                HttpMethod.Delete -> {
                    assertEquals("b12345678", request.url.parameters["id"])
                    """{"data":{"id":"b12345678","deleted":true}}"""
                }
                HttpMethod.Patch -> """{"data":{"id":"b12345678","wordIndex":250,"percent":25}}"""
                else -> error("Unexpected method: ${request.method}")
            }
        })

        val upload = client.uploadBook(
            baseUrl = "http://device.local",
            name = "Story.rsvp",
            data = byteArrayOf(1, 2, 3),
            category = "article",
        )
        val delete = client.deleteBook("http://device.local", "b12345678")
        val position = client.setBookPosition(
            baseUrl = "http://device.local",
            id = "b12345678",
            wordIndex = 250,
        )

        assertEquals("/books/articles/Story.rsvp", upload.path)
        assertEquals(true, delete.deleted)
        assertEquals(250, position.wordIndex)
        assertEquals(
            listOf(
                "POST /api/v1/library?name=Story.rsvp&category=article",
                "DELETE /api/v1/library?id=b12345678",
                "PATCH /api/v1/library/position?",
            ),
            seen,
        )
    }

    @Test
    fun exposesStructuredDeviceErrors() = runBlocking {
        val client = NanoKtorClient(
            HttpClient(MockEngine) {
                engine {
                    addHandler {
                        respond(
                            content = """{"error":{"code":"invalid_setting","message":"wpm is out of range","field":"wpm"}}""",
                            status = HttpStatusCode.UnprocessableEntity,
                            headers = headersOf(HttpHeaders.ContentType, ContentType.Application.Json.toString()),
                        )
                    }
                }
            }
        )

        val error = assertFailsWith<NanoClientError> {
            client.fetchSettings("http://device.local")
        }

        assertEquals("invalid_setting", error.code)
        assertEquals("wpm", error.field)
        assertEquals(422, error.status)
    }

    private fun mockHttpClient(handler: (io.ktor.client.request.HttpRequestData) -> String): HttpClient {
        return HttpClient(MockEngine) {
            engine {
                addHandler { request ->
                    respond(
                        content = handler(request),
                        status = HttpStatusCode.OK,
                        headers = headersOf(HttpHeaders.ContentType, ContentType.Application.Json.toString()),
                    )
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
}
