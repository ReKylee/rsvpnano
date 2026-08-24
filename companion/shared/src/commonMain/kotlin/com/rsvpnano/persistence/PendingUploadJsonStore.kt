package com.rsvpnano.persistence

import com.rsvpnano.models.PendingUpload
import kotlinx.serialization.Serializable
import kotlinx.serialization.json.Json

class PendingUploadJsonStore(
    private val storage: TextStorage,
    private val json: Json = Json {
        ignoreUnknownKeys = true
        encodeDefaults = true
        explicitNulls = false
        prettyPrint = false
    },
) {
    suspend fun loadAll(): List<PendingUpload> {
        val text = storage.readText() ?: return emptyList()
        return runCatching { json.decodeFromString(PendingUploadList.serializer(), text).items }
            .getOrDefault(emptyList())
            .sortedByDescending(PendingUpload::createdAt)
    }

    suspend fun save(item: PendingUpload) {
        val items = loadAll().toMutableList()
        val index = items.indexOfFirst { it.id == item.id }
        if (index >= 0) items[index] = item else items.add(item)
        writeAll(items)
    }

    suspend fun delete(item: PendingUpload) = writeAll(loadAll().filterNot { it.id == item.id })

    private suspend fun writeAll(items: List<PendingUpload>) = storage.writeText(
        json.encodeToString(
            PendingUploadList.serializer(),
            PendingUploadList(items.sortedByDescending(PendingUpload::createdAt)),
        ),
    )

    @Serializable
    private data class PendingUploadList(
        val items: List<PendingUpload>,
    )
}
