package com.rsvpnano.converters

internal object EpubZipReader {
    fun readEntries(data: ByteArray): Map<String, ByteArray> =
        ZipArchiveReader.readEntries(data).mapKeys { (path, _) -> path.lowercase() }
}
