package com.rsvpnano.library

import com.rsvpnano.library.PendingDraftService
import com.rsvpnano.models.PendingUpload
import com.rsvpnano.persistence.PendingUploadJsonStore
import com.rsvpnano.persistence.TextStorage
import kotlinx.coroutines.test.runTest
import kotlin.test.Test
import kotlin.test.assertEquals

class PendingDraftServiceTest {
    @Test
    fun savesUpdatesAndDeletesDrafts() = runTest {
        val service = PendingDraftService(PendingUploadJsonStore(InMemoryStorage()))
        val item = PendingUpload(
            id = "1",
            title = "Draft",
            sourceUrl = "https://example.com",
            body = "https://example.com",
            createdAt = "2026-05-18T10:00:00Z",
        )

        service.saveDraft(item)
        assertEquals(listOf("Draft"), service.loadDrafts().map { it.title })
        service.deleteDraft(item)
        assertEquals(emptyList(), service.loadDrafts())
    }

    private class InMemoryStorage : TextStorage {
        private var value: String? = null
        override suspend fun readText(): String? = value
        override suspend fun writeText(value: String) { this.value = value }
    }
}
