package com.rsvpnano

import com.rsvpnano.api.NanoClient
import com.rsvpnano.app.NanoCompanionController
import com.rsvpnano.app.PendingDraftService
import com.rsvpnano.converters.RsvpBookFile
import com.rsvpnano.models.NanoBook
import com.rsvpnano.models.NanoBookSource
import com.rsvpnano.models.NanoReadingProgress
import com.rsvpnano.models.NanoInfo
import com.rsvpnano.models.NanoRssFeeds
import com.rsvpnano.models.NanoSettings
import com.rsvpnano.models.NanoUploadResponse
import com.rsvpnano.models.NanoWifiSettings
import com.rsvpnano.models.PendingUpload
import com.rsvpnano.persistence.PendingUploadStore
import com.rsvpnano.persistence.PendingUploadRepository
import kotlinx.coroutines.runBlocking
import kotlin.test.Test
import kotlin.test.assertEquals

class NanoCompanionControllerTest {
    @Test
    fun connectLoadsDeviceFeedsAndDrafts() = runBlocking {
        val pendingStore = InMemoryPendingStore(listOf(samplePendingUpload()))
        val client = RecordingNanoClient(
            deviceFeeds = listOf(" https://device.example/feed ", "https://device.example/feed"),
        )
        val controller = controller(pendingStore, client)

        val snapshot = controller.connect(baseUrl = "http://device.local")

        assertEquals("Nano", snapshot.device.info?.name)
        assertEquals(listOf("https://device.example/feed"), snapshot.rssFeeds)
        assertEquals(listOf("https://device.example/feed"), snapshot.rssFeeds)
        assertEquals(1, snapshot.drafts.size)
    }

    @Test
    fun syncPendingUploadsUploadsDraftsAndRefreshesBooks() = runBlocking {
        val pending = samplePendingUpload()
        val pendingStore = InMemoryPendingStore(listOf(pending))
        val client = RecordingNanoClient()
        val controller = controller(pendingStore, client)

        val snapshot = controller.syncPendingUploads(
            baseUrl = "http://device.local",
            items = listOf(pending),
        )

        assertEquals(1, snapshot.syncedCount)
        assertEquals(emptyList(), snapshot.drafts)
        assertEquals("Example.rsvp", client.uploadedFilename)
        assertEquals(listOf(sampleBook(id = "Example.rsvp", title = "Example")), snapshot.books)
    }

    @Test
    fun draftMutationsReturnLatestDrafts() = runBlocking {
        val existing = samplePendingUpload()
        val pendingStore = InMemoryPendingStore(listOf(existing))
        val controller = controller(pendingStore, RecordingNanoClient())
        val added = existing.copy(id = "2", title = "Second")

        val saved = controller.saveDraft(added)
        val deleted = controller.deleteDraft(existing)

        assertEquals(listOf("Second", "Example"), saved.map { it.title })
        assertEquals(listOf("Second"), deleted.map { it.title })
    }

    @Test
    fun saveRssFeedsWritesNormalizedFeedsToDevice() = runBlocking {
        val client = RecordingNanoClient()
        val controller = controller(InMemoryPendingStore(), client)

        val snapshot = controller.saveRssFeeds(
            baseUrl = "http://device.local",
            feeds = listOf(" https://local.example/feed ", "https://local.example/feed"),
        )

        assertEquals(listOf("https://local.example/feed"), client.savedFeeds)
        assertEquals(listOf("https://local.example/feed"), snapshot)
    }

    @Test
    fun uploadBookUploadsFileAndRefreshesBooks() = runBlocking {
        val client = RecordingNanoClient()
        val controller = controller(InMemoryPendingStore(), client)

        val snapshot = controller.uploadBook(
            baseUrl = "http://device.local",
            file = RsvpBookFile(
                filename = "Manual.rsvp",
                data = byteArrayOf(1, 2, 3),
                title = "Manual",
                wordCount = 1,
                chapterCount = 1,
            ),
            category = "book",
        )

        assertEquals("Manual.rsvp", client.uploadedFilename)
        assertEquals("book", client.uploadedCategory)
        assertEquals(listOf(sampleBook(id = "Manual.rsvp", title = "Manual")), snapshot)
    }

    @Test
    fun deleteBooksDeletesEachBookAndRefreshesBooks() = runBlocking {
        val client = RecordingNanoClient(
            initialBooks = listOf(
                sampleBook(id = "b00000001", title = "One"),
                sampleBook(id = "b00000002", title = "Two"),
            )
        )
        val controller = controller(InMemoryPendingStore(), client)

        val snapshot = controller.deleteBooks(
            baseUrl = "http://device.local",
            bookIds = listOf("b00000001", "b00000002"),
        )

        assertEquals(listOf("b00000001", "b00000002"), client.deletedFilenames)
        assertEquals(emptyList(), snapshot)
    }

    @Test
    fun setBookPositionSavesAndRefreshesBooks() = runBlocking {
        val book = sampleBook(id = "b12345678", title = "Manual", wordCount = 1000).copy(
            source = NanoBookSource(size = 1234, fingerprint = 3456),
            reading = NanoReadingProgress(
                wordIndex = 100,
                percent = 10,
                remainingWords = 899,
                estimatedMinutes = 3,
            ),
        )
        val client = RecordingNanoClient(initialBooks = listOf(book))
        val controller = controller(InMemoryPendingStore(), client)

        val snapshot = controller.setBookPosition(
            baseUrl = "http://device.local",
            book = book,
            wordIndex = 250,
        )

        assertEquals("b12345678", client.savedPositionId)
        assertEquals(250, client.savedPositionWordIndex)
        assertEquals(listOf(book), snapshot)
    }

    @Test
    fun saveSettingsPersistsDeviceSettings() = runBlocking {
        val client = RecordingNanoClient()
        val controller = controller(InMemoryPendingStore(), client)
        val settings = sampleSettings().withWpm(320).withBrightnessPercent(20)

        val snapshot = controller.saveSettings(
            baseUrl = "http://device.local",
            settings = settings,
        )

        assertEquals(settings, client.savedSettings)
        assertEquals(settings, snapshot.settings)
        assertEquals(null, snapshot.wifiSettings)
    }

    @Test
    fun wifiMutationsReturnUpdatedWifiSnapshot() = runBlocking {
        val client = RecordingNanoClient()
        val controller = controller(InMemoryPendingStore(), client)

        val saved = controller.saveWifiSettings(
            baseUrl = "http://device.local",
            ssid = "Home",
            password = "secret",
        )
        val cleared = controller.clearWifiSettings(baseUrl = "http://device.local")

        assertEquals("Home" to "secret", client.savedWifi)
        assertEquals(NanoWifiSettings(passwordSet = true), saved)
        assertEquals(NanoWifiSettings(passwordSet = false), cleared)
    }

    private fun controller(
        pendingStore: PendingUploadStore,
        client: NanoClient,
    ): NanoCompanionController {
        return NanoCompanionController(
            draftService = PendingDraftService(
                repository = PendingUploadRepository(pendingStore),
            ),
            client = client,
        )
    }

    private fun samplePendingUpload(): PendingUpload = PendingUpload(
        id = "1",
        title = "Example",
        sourceUrl = "https://example.com/story",
        body = "Hello reader.",
        createdAt = "2026-05-17T10:00:00Z",
    )

    private class RecordingNanoClient(
        private val deviceFeeds: List<String> = emptyList(),
        initialBooks: List<NanoBook> = emptyList(),
    ) : NanoClient {
        private var books: List<NanoBook> = initialBooks
        var uploadedFilename: String? = null
        var uploadedCategory: String? = null
        var savedFeeds: List<String>? = null
        var savedSettings: NanoSettings? = null
        var savedWifi: Pair<String, String>? = null
        var savedPositionId: String? = null
        var savedPositionWordIndex: Int? = null
        val deletedFilenames = mutableListOf<String>()

        override suspend fun fetchInfo(baseUrl: String): NanoInfo = NanoInfo(name = "Nano", apiVersion = 1)

        override suspend fun listBooks(baseUrl: String): List<NanoBook> = books

        override suspend fun fetchSettings(baseUrl: String): NanoSettings = sampleSettings()

        override suspend fun updateSettings(baseUrl: String, settings: NanoSettings): NanoSettings {
            savedSettings = settings
            return settings
        }

        override suspend fun fetchWifiSettings(baseUrl: String): NanoWifiSettings =
            NanoWifiSettings(passwordSet = false)

        override suspend fun updateWifi(baseUrl: String, ssid: String, password: String): NanoWifiSettings {
            savedWifi = ssid to password
            return NanoWifiSettings(passwordSet = true)
        }

        override suspend fun forgetWifi(baseUrl: String): NanoWifiSettings =
            NanoWifiSettings(passwordSet = false)

        override suspend fun fetchRssFeeds(baseUrl: String): NanoRssFeeds =
            NanoRssFeeds(feeds = deviceFeeds)

        override suspend fun updateRssFeeds(baseUrl: String, config: NanoRssFeeds): NanoRssFeeds {
            savedFeeds = config.feeds
            return config
        }

        override suspend fun uploadBook(
            baseUrl: String,
            name: String,
            data: ByteArray,
            category: String?,
            onProgress: ((sent: Long, total: Long) -> Unit)?,
        ): NanoUploadResponse {
            onProgress?.invoke(data.size.toLong(), data.size.toLong())
            uploadedFilename = name
            uploadedCategory = category
            books = listOf(sampleBook(id = name))
            return NanoUploadResponse(path = "/books/$name")
        }

        override suspend fun deleteBook(baseUrl: String, id: String): NanoUploadResponse {
            deletedFilenames += id
            books = books.filterNot { it.id == id }
            return NanoUploadResponse(id = id, deleted = true)
        }

        override suspend fun setBookPosition(
            baseUrl: String,
            id: String,
            wordIndex: Int,
        ): NanoUploadResponse {
            savedPositionId = id
            savedPositionWordIndex = wordIndex
            return NanoUploadResponse(id = id, wordIndex = wordIndex)
        }
    }

    private class InMemoryPendingStore(var items: List<PendingUpload> = emptyList()) : PendingUploadStore {
        override suspend fun loadAll(): List<PendingUpload> = items

        override suspend fun saveAll(items: List<PendingUpload>) {
            this.items = items
        }

        override suspend fun remove(id: String) {
            items = items.filterNot { it.id == id }
        }
    }

}
