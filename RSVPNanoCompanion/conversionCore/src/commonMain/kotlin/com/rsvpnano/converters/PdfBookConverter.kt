package com.rsvpnano.converters

import io.github.limuyang2.pdf.core.PdfSource
import io.github.limuyang2.pdf.core.PdfViewer
import kotlinx.coroutines.CancellationException

internal object PdfBookConverter {
    suspend fun convert(data: ByteArray, filename: String): RsvpBookFile {
        try {
            val document = PdfViewer.open(PdfSource.Bytes(data))
            try {
                val metadata = document.metadata()
                val fallbackTitle = RsvpConverter.filenameWithoutExtension(filename)
                val title = metadata.title?.let(RsvpTextUtils::cleanedLine).orEmpty().ifEmpty { fallbackTitle }
                val author = metadata.author?.let(RsvpTextUtils::cleanedLine).orEmpty()
                val writer = RsvpWriter(title = title, author = author, source = filename)
                repeat(document.pageCount) { pageIndex ->
                    writer.addEvents(RsvpTextUtils.textEvents(document[pageIndex].extractText()))
                }

                if (!writer.hasReadableText()) {
                    throw RsvpConversionError.unsupportedPdf
                }
                return writer.finalize(fallbackChapterTitle = title)
            } finally {
                document.close()
            }
        } catch (error: Throwable) {
            if (error is CancellationException || error is RsvpConversionError) throw error
            throw RsvpConversionError.unsupportedPdf
        }
    }
}
