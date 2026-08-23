package com.rsvpnano

import com.rsvpnano.api.NanoClientError
import com.rsvpnano.api.NanoKtorClient
import com.rsvpnano.app.catalogContentUrl
import com.rsvpnano.app.releaseSource
import com.rsvpnano.models.NanoChapter
import io.ktor.client.HttpClient
import io.ktor.client.engine.okhttp.OkHttp
import io.ktor.client.plugins.HttpTimeout
import io.ktor.client.plugins.contentnegotiation.ContentNegotiation
import io.ktor.serialization.kotlinx.json.json
import kotlinx.coroutines.runBlocking
import kotlinx.serialization.json.Json
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFailsWith
import kotlin.test.assertTrue
import kotlin.time.TimeSource

class NanoKtorClientDeviceTest {
    @Test
    fun exercisesActualDeviceApi() = runBlocking {
        val baseUrl = System.getenv("RSVPNANO_DEVICE_URL")?.trim()?.trimEnd('/')
        if (baseUrl.isNullOrEmpty()) {
            println("[device-api] skipped; RSVPNANO_DEVICE_URL is not set")
            return@runBlocking
        }

        val iterations = System.getenv("RSVPNANO_DEVICE_ITERATIONS")?.toIntOrNull()?.coerceAtLeast(1) ?: 3
        val writeEnabled = System.getenv("RSVPNANO_DEVICE_WRITE") == "1"
        val metrics = mutableListOf<Pair<String, Long>>()
        val httpClient = HttpClient(OkHttp) {
            install(HttpTimeout) {
                connectTimeoutMillis = 10_000
                requestTimeoutMillis = 60_000
                socketTimeoutMillis = 60_000
            }
            install(ContentNegotiation) {
                json(
                    Json {
                        ignoreUnknownKeys = true
                        encodeDefaults = true
                        explicitNulls = false
                    },
                )
            }
        }
        val client = NanoKtorClient(httpClient)

        suspend fun <T> measured(name: String, request: suspend () -> T): T {
            val started = TimeSource.Monotonic.markNow()
            return try {
                request()
            } finally {
                val elapsedMs = started.elapsedNow().inWholeMilliseconds
                metrics += name to elapsedMs
                println("[device-api] $name ${elapsedMs}ms")
            }
        }

        try {
            repeat(iterations) { index ->
                val suffix = if (iterations == 1) "" else "[${index + 1}]"
                val info = measured("device$suffix") { client.fetchDevice(baseUrl) }
                assertTrue(info.firmwareVersion.isNotBlank())
                assertTrue(info.otaAsset.isNotBlank())
                measured("library$suffix") { client.listLibrary(baseUrl) }
                measured("themes$suffix") { client.listThemes(baseUrl) }
                measured("fonts$suffix") { client.listFonts(baseUrl) }
                measured("locales$suffix") { client.listLocales(baseUrl) }
                measured("settings$suffix") { client.fetchSettings(baseUrl) }
                measured("network$suffix") { client.fetchWifiSettings(baseUrl) }
                measured("feeds$suffix") { client.fetchRssFeeds(baseUrl) }
                measured("focus-timers$suffix") { client.fetchFocusTimers(baseUrl) }
            }

            val missing = assertFailsWith<NanoClientError> {
                measured("structured-error") { client.deleteBook(baseUrl, "api-test-missing") }
            }
            assertEquals(404, missing.status)
            assertEquals("book_not_found", missing.code)
            assertEquals("Book not found", missing.message)

            if (writeEnabled) {
                val catalogSettings = measured("catalog-settings") { client.fetchSettings(baseUrl) }
                val catalog = requireNotNull(
                    releaseSource(catalogSettings.updates.repositoryOwner, catalogSettings.updates.releaseTag),
                )
                val installedFonts = measured("catalog-installed-fonts") { client.listFonts(baseUrl) }
                val font = measured("catalog-fonts") {
                    client.fetchFontCatalog(catalog.catalogContentUrl("fonts/index.json"))
                }.first()
                val fontData = measured("catalog-font-download") {
                    client.downloadFont(catalog.catalogContentUrl("fonts/${font.file}"))
                }
                assertTrue(fontData.isNotEmpty())
                if (installedFonts.none { it.id == font.id }) {
                    try {
                        assertEquals(font.id, measured("catalog-font-install") {
                            client.uploadFont(baseUrl, font.file.substringAfterLast('/'), fontData)
                        }.id)
                    } finally {
                        measured("catalog-font-delete") { client.deleteFont(baseUrl, font.id) }
                    }
                }

                val installedLocales = measured("catalog-installed-locales") { client.listLocales(baseUrl) }
                val locale = measured("catalog-locales") {
                    client.fetchLocaleCatalog(catalog.catalogContentUrl("locale-packs/index.json"))
                }.first()
                val localeData = measured("catalog-locale-download") {
                    client.downloadLocalePack(catalog.catalogContentUrl("locale-packs/${locale.file}"))
                }
                assertTrue(localeData.isNotEmpty())
                if (installedLocales.none { it.id == locale.id }) {
                    try {
                        val installed = measured("catalog-locale-install") {
                            client.uploadLocalePack(baseUrl, locale.file, localeData)
                        }
                        assertEquals(locale.id, installed.id)
                        assertTrue(measured("catalog-locales-after-install") {
                            client.listLocales(baseUrl)
                        }.any { it.id == locale.id })
                    } finally {
                        measured("catalog-locale-delete") { client.deleteLocalePack(baseUrl, locale.id) }
                    }
                }

                val settings = measured("settings-before-write") { client.fetchSettings(baseUrl) }
                measured("patch-reading") { client.updateReadingSettings(baseUrl, settings.reading) }
                measured("patch-display") { client.updateDisplaySettings(baseUrl, settings.`interface`) }
                measured("patch-updates") { client.updateUpdateSettings(baseUrl, settings.updates) }
                measured("select-theme") { client.selectTheme(baseUrl, settings.`interface`.selectedThemeId) }
                measured("select-font") { client.selectFont(baseUrl, settings.reading.typography.fontId) }
                measured("select-locale") { client.selectLocale(baseUrl, settings.`interface`.locale) }

                val feeds = measured("feeds-before-write") { client.fetchRssFeeds(baseUrl) }
                measured("put-feeds") { client.updateRssFeeds(baseUrl, feeds) }
                val timers = measured("focus-before-write") { client.fetchFocusTimers(baseUrl) }
                measured("put-focus-timers") { client.updateFocusTimers(baseUrl, timers) }

                var uploadedId: String? = null
                try {
                    val filename = "api-contract-${System.currentTimeMillis()}.rsvp"
                    val words = (0 until 900).joinToString(" ") { "word$it" }
                    val uploaded = measured("upload-book") {
                        client.uploadBook(
                            baseUrl = baseUrl,
                            name = filename,
                            data = "@title API contract probe\n@author Device test\n@chapter Streamed upload\n\n$words\n"
                                .encodeToByteArray(),
                            category = "book",
                        )
                    }
                    uploadedId = uploaded.id
                    assertEquals("API contract probe", uploaded.metadata.title)
                    assertEquals("Device test", uploaded.metadata.author)
                    assertEquals(900, uploaded.metadata.wordCount)
                    assertEquals(listOf(NanoChapter(title = "Streamed upload", wordIndex = 0)), uploaded.metadata.chapters)
                    val listed = measured("library-after-upload") { client.listLibrary(baseUrl) }
                        .single { it.id == uploaded.id }
                    assertEquals("API contract probe", listed.metadata.title)
                    assertEquals("Device test", listed.metadata.author)
                    assertEquals(uploaded.metadata.wordCount, listed.metadata.wordCount)
                    assertEquals(uploaded.metadata.chapters, listed.metadata.chapters)
                } finally {
                    uploadedId?.let { id -> measured("delete-book") { client.deleteBook(baseUrl, id) } }
                }
                assertTrue(
                    measured("library-after-delete") { client.listLibrary(baseUrl) }.none { it.id == uploadedId },
                )
            }
        } finally {
            httpClient.close()
            metrics.groupBy({ it.first.substringBefore('[') }, { it.second }).forEach { (name, samples) ->
                val sorted = samples.sorted()
                println("[device-api] summary $name median=${sorted[sorted.size / 2]}ms max=${sorted.last()}ms")
            }
        }
    }
}
