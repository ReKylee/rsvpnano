package com.rsvpnano.ui.library

import com.rsvpnano.models.NanoBook
import com.rsvpnano.models.NanoBookMetadata
import com.rsvpnano.models.NanoChapter
import com.rsvpnano.models.NanoReadingProgress
import com.rsvpnano.ui.library.librarySubtitle
import kotlin.test.Test
import kotlin.test.assertEquals

class LibraryLabelsTest {
    @Test
    fun rowSubtitleKeepsDetailsConciseAndTypeAware() {
        val metadata = NanoBookMetadata(
            title = "Alice's Adventures in Wonderland",
            author = "Lewis Carroll",
            wordCount = 26432,
            chapters = List(12) { NanoChapter("Chapter ${it + 1}", it * 2000) },
        )
        val reading = NanoReadingProgress(11366)

        assertEquals(
            "Lewis Carroll  │  26432 words  │  12 chapters  │  43% read",
            NanoBook("book", "alice.epub", 1024, metadata, reading).librarySubtitle,
        )
        assertEquals(
            "Lewis Carroll  │  26432 words  │  43% read",
            NanoBook("article", "articles/alice.rsvp", 1024, metadata, reading).librarySubtitle,
        )
    }
}
