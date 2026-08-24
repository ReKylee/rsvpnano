package com.rsvpnano.converters

object RsvpSupportedFileTypes {
    private val convertibleExtensions = listOf(
        ".epub",
        ".html",
        ".htm",
        ".xhtml",
        ".md",
        ".markdown",
        ".pdf",
        ".txt",
    )

    val importExtensions = convertibleExtensions.map { it.removePrefix(".") } + "rsvp"

    fun extensionFor(filename: String): String {
        val base = filename.substringAfterLast('/').substringAfterLast('\\')
        val index = base.lastIndexOf('.')
        return if (index >= 0) base.substring(index).lowercase() else ""
    }

    fun isRsvp(filename: String): Boolean = extensionFor(filename) == ".rsvp"

    fun isEpub(filename: String): Boolean = extensionFor(filename) == ".epub"

    fun isPdf(filename: String): Boolean = extensionFor(filename) == ".pdf"

    fun isTextLike(filename: String): Boolean {
        val extension = extensionFor(filename)
        return extension in convertibleExtensions && extension != ".epub" && extension != ".pdf"
    }

    fun isConvertible(filename: String): Boolean = extensionFor(filename) in convertibleExtensions

    fun isUploadPassthrough(filename: String): Boolean = isRsvp(filename)
}
