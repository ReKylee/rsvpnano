package com.rsvpnano.ui

import com.rsvpnano.models.NanoBook
import com.rsvpnano.models.NanoBookMetadata
import com.rsvpnano.models.NanoReadingProgress
import kotlin.test.Test
import kotlin.test.assertEquals

class LibraryLabelsTest {
    @Test
    fun rowSubtitleKeepsDetailsConciseAndTypeAware() {
        val metadata = NanoBookMetadata(
            title = "Alice's Adventures in Wonderland",
            author = "Lewis Carroll",
            wordCount = 26432,
            chapterCount = 12,
        )
        val reading = NanoReadingProgress(100, 43, 15000, 50)

        assertEquals(
            "Lewis Carroll · 26432 words · 12 chapters · 43% read",
            NanoBook("book", "alice.epub", 1024, "book", metadata, reading = reading).librarySubtitle,
        )
        assertEquals(
            "Lewis Carroll · 26432 words · 43% read",
            NanoBook("article", "alice.rsvp", 1024, "article", metadata, reading = reading).librarySubtitle,
        )
    }
}
