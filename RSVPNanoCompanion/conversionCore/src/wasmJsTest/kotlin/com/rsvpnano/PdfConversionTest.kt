package com.rsvpnano

import com.rsvpnano.converters.RsvpConversionError
import com.rsvpnano.converters.RsvpConverter
import kotlinx.coroutines.test.runTest
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFailsWith

class PdfConversionTest {
    @Test
    fun textUsesMetadataAndPageOrder() = runTest {
        val pdf = testPdf("First page text.", "Second page text.")

        val converted = RsvpConverter.bookFile(pdf, "fallback.pdf")
        val body = converted.data.decodeToString()

        assertEquals("PDF Book", converted.title)
        assertEquals(true, body.contains("@author PDF Author"))
        assertEquals(true, body.contains("@source fallback.pdf"))
        assertEquals(true, body.indexOf("First page text.") < body.indexOf("Second page text."))
    }

    @Test
    fun fileWithoutTextFailsClearly() = runTest {
        val pdf = testPdf("")

        assertFailsWith<RsvpConversionError> {
            RsvpConverter.bookFile(pdf, "scan.pdf")
        }
    }

    private fun testPdf(vararg pages: String): ByteArray {
        val objects = mutableListOf<String>()
        objects += "<< /Type /Catalog /Pages 2 0 R >>"
        objects += "<< /Type /Pages /Kids [${pages.indices.joinToString(" ") { "${4 + it * 2} 0 R" }}] /Count ${pages.size} >>"
        objects += "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>"
        pages.forEachIndexed { index, text ->
            val content = if (text.isEmpty()) "" else "BT /F1 12 Tf 72 720 Td (${pdfString(text)}) Tj ET"
            objects += "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] /Resources << /Font << /F1 3 0 R >> >> /Contents ${5 + index * 2} 0 R >>"
            objects += "<< /Length ${content.length} >>\nstream\n$content\nendstream"
        }
        val infoId = objects.size + 1
        objects += "<< /Title (PDF Book) /Author (PDF Author) >>"

        val output = StringBuilder("%PDF-1.4\n")
        val offsets = objects.mapIndexed { index, objectBody ->
            output.length.also {
                output.append("${index + 1} 0 obj\n$objectBody\nendobj\n")
            }
        }
        val xrefOffset = output.length
        output.append("xref\n0 ${objects.size + 1}\n0000000000 65535 f \n")
        offsets.forEach { offset -> output.append(offset.toString().padStart(10, '0')).append(" 00000 n \n") }
        output.append("trailer\n<< /Size ${objects.size + 1} /Root 1 0 R /Info $infoId 0 R >>\n")
        output.append("startxref\n$xrefOffset\n%%EOF\n")
        return output.toString().encodeToByteArray()
    }

    private fun pdfString(value: String): String = value
        .replace("\\", "\\\\")
        .replace("(", "\\(")
        .replace(")", "\\)")
}
