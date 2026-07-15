package com.rsvpnano.app

import com.rsvpnano.api.ArticleFetchClient
import com.rsvpnano.api.NanoClient
import com.rsvpnano.converters.ArticleFormatter
import com.rsvpnano.converters.RsvpBookFile
import com.rsvpnano.converters.RsvpConverter
import com.rsvpnano.converters.SharedArticle
import com.rsvpnano.models.PendingUpload
import com.rsvpnano.persistence.PendingUploadRepository
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext

/**
 * Shared domain service for saved article drafts and pending uploads.
 */
class PendingDraftService(
    private val repository: PendingUploadRepository,
    private val articleFetchClient: ArticleFetchClient? = null,
) {
    suspend fun fetchArticleIfAvailable(title: String, source: String): SharedArticle? {
        val client = articleFetchClient ?: return null
        return runCatching { client.fetch(title, source) }.getOrNull()
    }

    suspend fun loadDrafts(): List<PendingUpload> = repository.loadAll()

    suspend fun saveDraft(item: PendingUpload) {
        repository.save(item)
    }

    suspend fun deleteDraft(item: PendingUpload) {
        repository.delete(item)
    }

    suspend fun bookFileFor(item: PendingUpload): RsvpBookFile = withContext(Dispatchers.Default) {
        val article = ArticleFormatter.article(
            title = item.title,
            source = item.sourceUrl.orEmpty(),
            htmlOrText = item.body,
        )
        RsvpConverter.rsvpFile(
            title = article.title,
            author = "",
            source = article.source,
            events = ArticleFormatter.events(article),
        )
    }

    suspend fun syncPendingUploads(client: NanoClient, baseUrl: String, items: List<PendingUpload>): List<PendingUpload> {
        items.forEach { item ->
            val file = bookFileFor(item)
            client.uploadBook(baseUrl = baseUrl, name = file.filename, data = file.data, category = "article")
            repository.delete(item)
        }
        return repository.loadAll()
    }
}
