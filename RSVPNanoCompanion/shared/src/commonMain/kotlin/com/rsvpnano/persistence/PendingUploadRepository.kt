package com.rsvpnano.persistence

import com.rsvpnano.models.PendingUpload

/**
 * Shared CRUD logic for pending uploads.
 *
 * The storage backend remains platform-specific; the business rules stay here.
 */
class PendingUploadRepository(
    private val store: PendingUploadStore,
) {
    suspend fun loadAll(): List<PendingUpload> = store.loadAll()

    suspend fun save(item: PendingUpload) {
        val next = loadAll().toMutableList()
        val index = next.indexOfFirst { it.id == item.id }
        if (index >= 0) {
            next[index] = item
        } else {
            next.add(0, item)
        }
        store.saveAll(next)
    }

    suspend fun delete(item: PendingUpload) {
        store.remove(item.id)
    }

}
