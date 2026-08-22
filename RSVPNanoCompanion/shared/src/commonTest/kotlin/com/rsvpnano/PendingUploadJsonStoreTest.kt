package com.rsvpnano

import com.rsvpnano.models.PendingUpload
import com.rsvpnano.persistence.PendingUploadJsonStore
import com.rsvpnano.persistence.TextStorage
import kotlinx.coroutines.runBlocking
import kotlin.test.assertEquals
import kotlin.test.Test

class PendingUploadJsonStoreTest {
    @Test
    fun roundTripPreservesDrafts() {
        val storage = InMemoryStorage()
        val store = PendingUploadJsonStore(storage)

        val item = PendingUpload(
            id = "1",
            title = "Title",
            sourceUrl = "https://example.com",
            body = "Body",
            createdAt = "2026-05-17T10:00:00Z",
        )

        runBlocking {
            store.save(item)
            assertEquals(listOf(item), store.loadAll())
        }
    }

    @Test
    fun saveReplacesByIdAndKeepsNewestFirst() = runBlocking {
        val store = PendingUploadJsonStore(InMemoryStorage())
        val older = PendingUpload("1", "Old", null, "Old body", "2026-05-17T10:00:00Z")
        val newer = PendingUpload("2", "New", null, "New body", "2026-05-17T11:00:00Z")

        store.save(older)
        store.save(newer)
        store.save(older.copy(title = "Edited", body = "Edited body"))

        assertEquals(listOf("2", "1"), store.loadAll().map(PendingUpload::id))
        assertEquals("Edited", store.loadAll().last().title)
    }

    @Test
    fun deleteRemovesOnlyTheRequestedDraft() = runBlocking {
        val store = PendingUploadJsonStore(InMemoryStorage())
        val first = PendingUpload("1", "First", null, "Body", "2026-05-17T10:00:00Z")
        val second = PendingUpload("2", "Second", null, "Body", "2026-05-17T11:00:00Z")
        store.save(first)
        store.save(second)

        store.delete(first)

        assertEquals(listOf(second), store.loadAll())
    }

    private class InMemoryStorage : TextStorage {
        private var value: String? = null

        override suspend fun readText(): String? = value

        override suspend fun writeText(value: String) {
            this.value = value
        }
    }
}
