package com.rsvpnano

import com.rsvpnano.api.NanoClientError
import com.rsvpnano.api.NanoKtorClient
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
            assertTrue(missing.message.orEmpty().contains("\"code\":\"book_not_found\""))
            assertTrue(missing.message.orEmpty().contains("\"message\":\"Book not found\""))

            if (writeEnabled) {
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
                    val uploaded = measured("upload-book") {
                        client.uploadBook(
                            baseUrl = baseUrl,
                            name = filename,
                            data = "@title API contract probe\n@author Device test\n\nThe real ESP32 handled this upload.\n".encodeToByteArray(),
                            category = "book",
                        )
                    }
                    uploadedId = uploaded.id
                    assertEquals("API contract probe", uploaded.metadata.title)
                    assertEquals("Device test", uploaded.metadata.author)
                    val listed = measured("library-after-upload") { client.listLibrary(baseUrl) }
                        .single { it.id == uploaded.id }
                    assertEquals("API contract probe", listed.metadata.title)
                    assertEquals("Device test", listed.metadata.author)
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
