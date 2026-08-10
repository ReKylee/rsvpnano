package com.rsvpnano

import com.rsvpnano.api.NanoKtorClient
import com.rsvpnano.api.NanoClientError
import com.rsvpnano.models.NanoFocusTimer
import com.rsvpnano.models.NanoFocusTimers
import com.rsvpnano.models.NanoLanguageFont
import com.rsvpnano.models.NanoRssFeeds
import com.rsvpnano.models.NanoSettings
import com.rsvpnano.models.NanoWifiUpdate
import io.ktor.client.HttpClient
import io.ktor.client.engine.mock.MockEngine
import io.ktor.client.engine.mock.respond
import io.ktor.client.plugins.contentnegotiation.ContentNegotiation
import io.ktor.client.request.HttpRequestData
import io.ktor.client.request.forms.MultiPartFormDataContent
import io.ktor.http.ContentType
import io.ktor.http.HttpHeaders
import io.ktor.http.HttpMethod
import io.ktor.http.HttpStatusCode
import io.ktor.http.headersOf
import io.ktor.http.content.TextContent
import io.ktor.serialization.kotlinx.json.json
import kotlinx.coroutines.runBlocking
import kotlinx.serialization.json.Json
import kotlinx.serialization.json.int
import kotlinx.serialization.json.jsonArray
import kotlinx.serialization.json.jsonObject
import kotlinx.serialization.json.jsonPrimitive
import kotlin.test.Test
import kotlin.test.assertContentEquals
import kotlin.test.assertEquals
import kotlin.test.assertFailsWith
import kotlin.test.assertIs

class NanoKtorClientAndroidTest {
    @Test
    fun resolvesPublishedBuildVersionFromReleaseTarget() = runBlocking {
        val seen = mutableListOf<String>()
        val client = NanoKtorClient(mockHttpClient { request ->
            seen += request.url.encodedPath
            when (request.url.encodedPath) {
                "/repos/reader/rsvpnano/releases/tags/preview-v0.0.9" ->
                    """{"tag_name":"preview-v0.0.9","target_commitish":"0123456789abcdef0123456789abcdef01234567","assets":[{"name":"reader-ota.bin"}]}"""
                else -> error("Unexpected request: ${request.url}")
            }
        })

        val release = client.fetchFirmwareRelease("reader", "rsvpnano", "preview-v0.0.9")

        assertEquals("preview-v0.0.9+0123456789ab", release.version)
        assertEquals(listOf("reader-ota.bin"), release.assets)
        assertEquals(
            listOf("/repos/reader/rsvpnano/releases/tags/preview-v0.0.9"),
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
                "/api/v1/library" -> """{"data":{"books":[{"id":"b12345678","name":"books/Book.rsvp","category":"book","bytes":1234,"metadata":{"title":"Book","wordCount":1000,"chapterCount":1,"locale":"ar","direction":"rtl","scripts":["Arab","Latn"],"requiredCapabilities":["bidi","shaping.opentype"],"chapters":[{"title":"Chapter 1","wordIndex":0}]},"source":{"size":1234,"fingerprint":3456},"reading":{"wordIndex":249,"percent":24,"remainingWords":750,"estimatedMinutes":3,"currentChapter":{"number":1,"title":"Chapter 1"}}}]}}"""
                "/api/v1/feeds" -> """{"data":{"feeds":["https://example.com/feed"]}}"""
                "/api/v1/focus" -> """{"data":{"timers":[{"name":"Pomodoro","focusMinutes":25,"breakMinutes":5,"rounds":4}]}}"""
                "/api/v1/appearance/themes" -> """{"data":{"themes":[{"id":"default","name":"Default"},{"id":"night","name":"Night"}]}}"""
                "/api/v1/appearance/fonts" -> """{"data":{"fonts":[{"id":"literata","name":"Literata","scripts":["Latn","Cyrl"],"builtIn":true},{"id":"atkinson","name":"Atkinson Hyperlegible","scripts":["Latn"]}]}}"""
                else -> error("Unexpected request: ${request.url}")
            }
        })

        assertEquals("Nano", client.fetchInfo("http://device.local").name)
        val book = client.listBooks("http://device.local").single()
        assertEquals("b12345678", book.id)
        assertEquals("books/Book.rsvp", book.name)
        assertEquals("Book", book.metadata.title)
        assertEquals(1000, book.metadata.wordCount)
        assertEquals("ar", book.metadata.locale)
        assertEquals("rtl", book.metadata.direction)
        assertEquals(listOf("Arab", "Latn"), book.metadata.scripts)
        assertEquals(listOf("bidi", "shaping.opentype"), book.metadata.requiredCapabilities)
        assertEquals(249, book.reading?.wordIndex)
        assertEquals("Chapter 1", book.metadata.chapters.single().title)
        assertEquals(listOf("https://example.com/feed"), client.fetchRssFeeds("http://device.local").feeds)
        assertEquals("Pomodoro", client.fetchFocusTimers("http://device.local").timers.single().name)
        assertEquals(listOf("default", "night"), client.fetchThemes("http://device.local").map { it.id })
        val fonts = client.fetchFonts("http://device.local")
        assertEquals(listOf("literata", "atkinson"), fonts.map { it.id })
        assertEquals(listOf("Latn", "Cyrl"), fonts.first().scripts)
        assertEquals(true, fonts.first().builtIn)
        assertEquals(
            listOf(
                "GET /api/v1/device",
                "GET /api/v1/library",
                "GET /api/v1/feeds",
                "GET /api/v1/focus",
                "GET /api/v1/appearance/themes",
                "GET /api/v1/appearance/fonts",
            ),
            seen,
        )
    }

    @Test
    fun settingsRoundTripUsesCanonicalDocumentAndPut() = runBlocking {
        val seen = mutableListOf<String>()
        var deviceSettings = sampleSettings()
        val client = NanoKtorClient(mockHttpClient { request ->
            seen += "${request.method.value} ${request.url.encodedPath}"
            if (request.method == HttpMethod.Put) {
                assertEquals(ContentType.Application.Json, request.body.contentType)
                deviceSettings = testJson.decodeFromString(NanoSettings.serializer(), requestBodyText(request))
            }
            """{"data":${testJson.encodeToString(NanoSettings.serializer(), deviceSettings)}}"""
        })

        val fetched = client.fetchSettings("http://device.local")
        assertEquals(sampleSettings(), fetched)

        val requested = fetched
            .withWpm(450)
            .withBrightnessPercent(55)
            .withLocale("es")
            .withTypeface("atkinson")
            .withThemeId("default")
        val updated = client.updateSettings("http://device.local", requested)
        val fetchedAgain = client.fetchSettings("http://device.local")

        assertEquals(requested, updated)
        assertEquals(requested, fetchedAgain)
        assertEquals(
            listOf("GET /api/v1/settings", "PUT /api/v1/settings", "GET /api/v1/settings"),
            seen,
        )
    }

    @Test
    fun configurationApisRoundTripTheirPayloads() = runBlocking {
        val seen = mutableListOf<String>()
        var wifiPasswordSet = false
        var feeds = NanoRssFeeds()
        var focus = NanoFocusTimers()
        val client = NanoKtorClient(mockHttpClient { request ->
            seen += "${request.method.value} ${request.url.encodedPath}"
            when (request.url.encodedPath) {
                "/api/v1/network" -> {
                    when (request.method) {
                        HttpMethod.Put -> {
                            val update = testJson.decodeFromString(NanoWifiUpdate.serializer(), requestBodyText(request))
                            assertEquals(NanoWifiUpdate("Home", "secret"), update)
                            wifiPasswordSet = update.password.isNotEmpty()
                        }
                        HttpMethod.Delete -> wifiPasswordSet = false
                    }
                    """{"data":{"passwordSet":$wifiPasswordSet}}"""
                }
                "/api/v1/feeds" -> {
                    if (request.method == HttpMethod.Put) {
                        feeds = testJson.decodeFromString(NanoRssFeeds.serializer(), requestBodyText(request))
                    }
                    """{"data":${testJson.encodeToString(NanoRssFeeds.serializer(), feeds)}}"""
                }
                "/api/v1/focus" -> {
                    if (request.method == HttpMethod.Put) {
                        focus = testJson.decodeFromString(NanoFocusTimers.serializer(), requestBodyText(request))
                    }
                    """{"data":${testJson.encodeToString(NanoFocusTimers.serializer(), focus)}}"""
                }
                else -> error("Unexpected request: ${request.url}")
            }
        })

        assertEquals(false, client.fetchWifiSettings("http://device.local").passwordSet)
        assertEquals(true, client.updateWifi("http://device.local", "Home", "secret").passwordSet)
        assertEquals(false, client.forgetWifi("http://device.local").passwordSet)
        val requestedFeeds = NanoRssFeeds(listOf("https://example.com/feed.xml"))
        val requestedFocus = NanoFocusTimers(listOf(NanoFocusTimer("Deep work", 50, 10, 3)))
        assertEquals(requestedFeeds, client.updateRssFeeds("http://device.local", requestedFeeds))
        assertEquals(requestedFeeds, client.fetchRssFeeds("http://device.local"))
        assertEquals(requestedFocus, client.updateFocusTimers("http://device.local", requestedFocus))
        assertEquals(requestedFocus, client.fetchFocusTimers("http://device.local"))
        assertEquals(
            listOf(
                "GET /api/v1/network",
                "PUT /api/v1/network",
                "DELETE /api/v1/network",
                "PUT /api/v1/feeds",
                "GET /api/v1/feeds",
                "PUT /api/v1/focus",
                "GET /api/v1/focus",
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
                    assertIs<MultiPartFormDataContent>(request.body)
                    """{"data":{"path":"/books/articles/Story.rsvp"}}"""
                }
                HttpMethod.Delete -> {
                    assertEquals("b12345678", request.url.parameters["id"])
                    """{"data":{"id":"b12345678","deleted":true}}"""
                }
                HttpMethod.Patch -> {
                    val body = testJson.parseToJsonElement(requestBodyText(request)).jsonObject
                    assertEquals("b12345678", body.getValue("id").jsonPrimitive.content)
                    if (request.url.encodedPath.endsWith("/position")) {
                        assertEquals(250, body.getValue("wordIndex").jsonPrimitive.int)
                        """{"data":{"id":"b12345678","wordIndex":250,"percent":25}}"""
                    } else {
                        val fonts = body.getValue("languageFonts").jsonArray
                        assertEquals("ar", fonts[0].jsonObject.getValue("locale").jsonPrimitive.content)
                        assertEquals("noto-sans-arabic", fonts[0].jsonObject.getValue("fontId").jsonPrimitive.content)
                        assertEquals("math", fonts[1].jsonObject.getValue("locale").jsonPrimitive.content)
                        assertEquals("stix-two-math", fonts[1].jsonObject.getValue("fontId").jsonPrimitive.content)
                        """{"data":{"id":"b12345678"}}"""
                    }
                }
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
        val languageFonts = client.setBookLanguageFonts(
            baseUrl = "http://device.local",
            id = "b12345678",
            languageFonts = listOf(
                NanoLanguageFont(locale = "ar", fontId = "noto-sans-arabic"),
                NanoLanguageFont(locale = "math", fontId = "stix-two-math"),
            ),
        )

        assertEquals("/books/articles/Story.rsvp", upload.path)
        assertEquals(true, delete.deleted)
        assertEquals(250, position.wordIndex)
        assertEquals("b12345678", languageFonts.id)
        assertEquals(
            listOf(
                "POST /api/v1/library?name=Story.rsvp&category=article",
                "DELETE /api/v1/library?id=b12345678",
                "PATCH /api/v1/library/position?",
                "PATCH /api/v1/library/language-fonts?",
            ),
            seen,
        )
    }

    @Test
    fun localePackLifecycleUploadsZip() = runBlocking {
        val seen = mutableListOf<String>()
        val client = NanoKtorClient(mockHttpClient { request ->
            seen += "${request.method.value} ${request.url.encodedPath}?${request.url.encodedQuery}"
            when {
                request.method == HttpMethod.Get ->
                    """{"data":{"locales":[{"id":"ja","version":"1.0.0","locale":"ja","nativeName":"Japanese","englishName":"Japanese","direction":"ltr","translationStatus":"preview"}],"rejected":[]}}"""
                request.method == HttpMethod.Post -> {
                    assertIs<MultiPartFormDataContent>(request.body)
                    """{"data":{"id":"ja"}}"""
                }
                request.method == HttpMethod.Delete -> """{"data":{"id":"ja","deleted":true}}"""
                else -> error("Unexpected request: ${request.url}")
            }
        })

        assertEquals("ja", client.fetchLocales("http://device.local").locales.single().locale)
        assertEquals("ja", client.uploadLocalePack("http://device.local", "ja.zip", byteArrayOf(1, 2, 3)).id)
        assertEquals(true, client.deleteLocalePack("http://device.local", "ja").deleted)
        assertEquals(
            listOf(
                "GET /api/v1/locales?",
                "POST /api/v1/locales?",
                "DELETE /api/v1/locales/ja?",
            ),
            seen,
        )
    }

    @Test
    fun appearanceCatalogsDownloadsAndUploadsUseTheirContracts() = runBlocking {
        val seen = mutableListOf<String>()
        val client = NanoKtorClient(mockHttpClient { request ->
            seen += "${request.method.value} ${request.url.encodedPath}?${request.url.encodedQuery}"
            when (request.url.encodedPath) {
                "/themes/catalog.json" ->
                    """[{"id":"night","name":"Night","file":"night.toml"}]"""
                "/themes/night.toml" -> "theme-data"
                "/fonts/catalog.json" ->
                    """[{"id":"atkinson","name":"Atkinson Hyperlegible","file":"atkinson/font.rfont4"}]"""
                "/fonts/atkinson/font.rfont4" -> "font-data"
                "/locale-packs/index.json" ->
                    """[{"id":"ja","name":"日本語","englishName":"Japanese","version":"1.0.0","locale":"ja","direction":"ltr","scripts":["Hani","Hira","Kana"],"translationStatus":"preview","file":"ja.zip"}]"""
                "/locale-packs/ja.zip" -> "locale-pack-data"
                "/api/v1/appearance/themes" -> {
                    assertEquals(HttpMethod.Post, request.method)
                    assertEquals("night.toml", request.url.parameters["name"])
                    assertIs<MultiPartFormDataContent>(request.body)
                    """{"data":{"path":"/themes/night.toml","id":"night"}}"""
                }
                "/api/v1/appearance/fonts" -> {
                    if (request.method == HttpMethod.Delete) {
                        assertEquals("atkinson", request.url.parameters["id"])
                        """{"data":{"id":"atkinson","deleted":true}}"""
                    } else {
                        assertEquals(HttpMethod.Post, request.method)
                        assertEquals("atkinson", request.url.parameters["family"])
                        assertIs<MultiPartFormDataContent>(request.body)
                        """{"data":{"path":"/fonts/atkinson/font.rfont4"}}"""
                    }
                }
                else -> error("Unexpected request: ${request.url}")
            }
        })

        val theme = client.fetchThemeCatalog("https://catalog.example/themes/catalog.json").single()
        val font = client.fetchFontCatalog("https://catalog.example/fonts/catalog.json").single()
        val locale = client.fetchLocaleCatalog("https://catalog.example/locale-packs/index.json").single()
        assertEquals("night", theme.id)
        assertEquals("atkinson/font.rfont4", font.file)
        assertEquals("ja.zip", locale.file)
        assertContentEquals(
            "theme-data".encodeToByteArray(),
            client.downloadTheme("https://catalog.example/themes/night.toml"),
        )
        assertContentEquals(
            "font-data".encodeToByteArray(),
            client.downloadFont("https://catalog.example/fonts/atkinson/font.rfont4"),
        )
        assertContentEquals(
            "locale-pack-data".encodeToByteArray(),
            client.downloadLocalePack("https://catalog.example/locale-packs/ja.zip"),
        )
        assertEquals(
            "night",
            client.uploadTheme("http://device.local", "night.toml", "theme-data".encodeToByteArray()).id,
        )
        assertEquals(
            "/fonts/atkinson/font.rfont4",
            client.uploadFont(
                "http://device.local",
                "atkinson",
                "font.rfont4",
                "font-data".encodeToByteArray(),
            ).path,
        )
        assertEquals(true, client.deleteFont("http://device.local", "atkinson").deleted)
        assertEquals(
            listOf(
                "GET /themes/catalog.json?",
                "GET /fonts/catalog.json?",
                "GET /locale-packs/index.json?",
                "GET /themes/night.toml?",
                "GET /fonts/atkinson/font.rfont4?",
                "GET /locale-packs/ja.zip?",
                "POST /api/v1/appearance/themes?name=night.toml",
                "POST /api/v1/appearance/fonts?family=atkinson",
                "DELETE /api/v1/appearance/fonts?id=atkinson",
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

    private fun requestBodyText(request: HttpRequestData): String =
        assertIs<TextContent>(request.body).text

    private fun mockHttpClient(handler: (HttpRequestData) -> String): HttpClient {
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
                    testJson
                )
            }
        }
    }

    private companion object {
        val testJson = Json {
            ignoreUnknownKeys = true
            encodeDefaults = true
            explicitNulls = false
        }
    }
}
