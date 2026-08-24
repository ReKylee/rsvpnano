package com.rsvpnano.library

import com.rsvpnano.library.PendingDraftService
import com.rsvpnano.models.PendingUpload
import com.rsvpnano.persistence.PendingUploadJsonStore
import com.rsvpnano.persistence.TextStorage
import kotlin.test.Test
import kotlin.test.assertEquals

class PendingDraftServiceArticleTest {
    @Test
    fun producesBookFileFromPendingUpload() = kotlinx.coroutines.test.runTest {
        val service = PendingDraftService(
            store = PendingUploadJsonStore(object : TextStorage {
                override suspend fun readText(): String? = null
                override suspend fun writeText(value: String) = Unit
            }),
        )

        val item = PendingUpload(
            id = "1",
            title = "Shared Article",
            sourceUrl = "https://example.com/story",
            body = "<html><body><p>Hello world.</p></body></html>",
            createdAt = "2026-05-17T10:00:00Z",
        )

        val bookFile = service.bookFileFor(item)

        assertEquals("Shared Article.rsvp", bookFile.filename)
        assertEquals(true, bookFile.data.decodeToString().contains("Hello world."))
    }
}
