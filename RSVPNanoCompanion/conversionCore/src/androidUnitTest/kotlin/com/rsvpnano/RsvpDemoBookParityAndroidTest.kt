package com.rsvpnano

import com.rsvpnano.converters.RsvpConverter
import java.io.File
import kotlin.test.Test
import kotlin.test.assertContentEquals
import kotlin.test.assertEquals
import kotlin.test.assertTrue

class RsvpDemoBookParityAndroidTest {
    @Test
    fun existingRsvpDemoBookPassesThroughByteForByte() {
        val demo = demoBookFile("european-letter-demo.rsvp")
        val data = demo.readBytes()
        val converted = RsvpConverter.bookFile(data, demo.name)

        assertEquals(demo.name, converted.filename)
        assertContentEquals(data, converted.data)
        assertEquals("european-letter-demo", converted.title)
    }

    @Test
    fun multilingualCorpusHasIdenticalContentAcrossFormats() {
        val names = listOf(
            "multilingual.txt",
            "multilingual.md",
            "multilingual.html",
            "multilingual.xhtml",
            "multilingual-epub2.epub",
            "multilingual-epub3.epub",
            "multilingual.rsvp",
        )
        val expected = normalizedContent(RsvpConverter.bookFile(corpusFile(names.first()).readBytes(), names.first()).data)
        names.drop(1).forEach { name ->
            val actual = normalizedContent(RsvpConverter.bookFile(corpusFile(name).readBytes(), name).data)
            assertEquals(expected, actual, name)
        }

        val epub = RsvpConverter.bookFile(
            corpusFile("multilingual-epub3.epub").readBytes(),
            "multilingual-epub3.epub",
        ).data.decodeToString()
        assertTrue(epub.contains("@language he\n@direction rtl\n"))
        assertTrue(epub.contains("@language ar\n"))
        assertTrue(epub.contains("@language ja\n@direction ltr\n"))
        assertEquals(false, epub.contains("@language en\n@direction ltr\n@language ar\n"))
        assertEquals(
            listOf(
                "Multilingual Reader Corpus", "English", "Español", "Français", "Deutsch", "Română",
                "Polski", "Русский", "עברית", "العربية", "日本語", "简体中文", "Mixed scripts",
            ),
            epub.lineSequence().filter { it.startsWith("@chapter ") }.map { it.removePrefix("@chapter ") }.toList(),
        )
    }

    @Test
    fun verticalCjkEpubAndXhtmlPreserveExplicitWritingMode() {
        val epub = RsvpConverter.bookFile(
            corpusFile("vertical-cjk.epub").readBytes(),
            "vertical-cjk.epub",
        ).data
        assertContentEquals(corpusFile("vertical-cjk-expected.rsvp").readBytes(), epub)

        val xhtml = RsvpConverter.bookFile(
            corpusFile("vertical-cjk.xhtml").readBytes(),
            "vertical-cjk.xhtml",
        ).data.decodeToString()
        assertEquals(1, xhtml.lineSequence().count { it == "@writing-mode vertical-rl" })
        assertTrue(xhtml.contains("@chapter 縦書き日本語\n"))
        assertTrue(xhtml.contains("@chapter 竖排简体中文\n"))
        assertTrue(xhtml.contains("吾輩は猫である、名前はまだ無い。"))
        assertTrue(xhtml.contains("天地玄黄，宇宙洪荒。日月盈昃，辰宿列张。"))
    }

    private fun demoBookFile(name: String): File {
        val candidates = generateSequence(File("").absoluteFile) { it.parentFile }
            .map { File(it, "RSVPNanoCompanion/testdata/localization/$name") }
            .toList()
        return candidates.firstOrNull { it.isFile }
            ?: error("Demo book not found. Checked: ${candidates.joinToString { it.path }}")
    }

    private fun corpusFile(name: String): File {
        val candidates = generateSequence(File("").absoluteFile) { it.parentFile }
            .map { File(it, "RSVPNanoCompanion/testdata/multilingual/$name") }
            .toList()
        return candidates.firstOrNull { it.isFile }
            ?: error("Multilingual corpus file not found. Checked: ${candidates.joinToString { it.path }}")
    }

    private fun normalizedContent(data: ByteArray): List<String> = data.decodeToString().lineSequence()
        .map(String::trim)
        .filter { line ->
            line.isNotEmpty() && listOf(
                "@rsvp ", "@title ", "@author ", "@source ", "@language ", "@direction ", "@converter ",
            ).none(line::startsWith)
        }
        .toList()
}
