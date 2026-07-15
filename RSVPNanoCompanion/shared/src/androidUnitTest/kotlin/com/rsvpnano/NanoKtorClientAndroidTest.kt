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
    fun fetchesDeviceSnapshotEndpoints() = runBlocking {
        val seen = mutableListOf<String>()
        val client = NanoKtorClient(mockHttpClient { request ->
            seen += "${request.method.value} ${request.url.encodedPath}"
            when (request.url.encodedPath) {
                "/api/v1/device" -> """{"data":{"name":"Nano","apiVersion":1}}"""
                "/api/v1/library" -> """{"data":{"books":[{"id":"b12345678","name":"books/Book.rsvp","category":"book","bytes":1234,"metadata":{"title":"Book","wordCount":1000,"chapterCount":1,"chapters":[{"title":"Chapter 1","wordIndex":0}]},"source":{"size":1234,"fingerprint":3456},"reading":{"wordIndex":249,"percent":24,"remainingWords":750,"estimatedMinutes":3,"currentChapter":{"number":1,"title":"Chapter 1"}}}]}}"""
                "/api/v1/feeds" -> """{"data":{"feeds":["https://example.com/feed"]}}"""
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
        assertEquals(listOf("GET /api/v1/device", "GET /api/v1/library", "GET /api/v1/feeds"), seen)
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
