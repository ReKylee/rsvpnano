package com.rsvpnano.web

internal const val SerialProtocolVersion: Int = 1
internal const val SerialChunkBytes: Int = 4096

internal enum class SerialFrameType(val code: Int) {
    Request(1),
    Data(2),
    End(3),
    Response(4),
    Acknowledgement(5),
    Error(6),
    Ping(7),
    Pong(8),
    Close(9),
    ;

    companion object {
        fun fromCode(code: Int): SerialFrameType? = entries.firstOrNull { it.code == code }
    }
}

internal data class SerialFrame(
    val type: SerialFrameType,
    val requestId: UInt = 0u,
    val sequence: UInt = 0u,
    val payload: ByteArray = byteArrayOf(),
)

internal object SerialFrameCodec {
    private val magic = byteArrayOf('R'.code.toByte(), 'S'.code.toByte(), 'V'.code.toByte(), '2'.code.toByte())
    private const val headerBytes = 18
    private const val trailerBytes = 4
    private const val maximumPayloadBytes = 64 * 1024

    fun encode(frame: SerialFrame): ByteArray {
        require(frame.payload.size <= maximumPayloadBytes) { "Serial frame payload is too large." }
        val output = ByteArray(headerBytes + frame.payload.size + trailerBytes)
        magic.copyInto(output)
        output[4] = SerialProtocolVersion.toByte()
        output[5] = frame.type.code.toByte()
        output.writeUInt(6, frame.requestId)
        output.writeUInt(10, frame.sequence)
        output.writeUInt(14, frame.payload.size.toUInt())
        frame.payload.copyInto(output, headerBytes)
        output.writeUInt(headerBytes + frame.payload.size, crc32(output, 4, headerBytes + frame.payload.size))
        return output
    }

    class Decoder {
        private var buffer = byteArrayOf()

        fun feed(bytes: ByteArray): List<SerialFrame> {
            if (bytes.isNotEmpty()) buffer += bytes
            val frames = mutableListOf<SerialFrame>()
            while (true) {
                val magicOffset = buffer.indexOfMagic()
                if (magicOffset < 0) {
                    buffer = buffer.takeLast(minOf(buffer.size, magic.size - 1)).toByteArray()
                    break
                }
                if (magicOffset > 0) buffer = buffer.copyOfRange(magicOffset, buffer.size)
                if (buffer.size < headerBytes + trailerBytes) break

                val payloadSize = buffer.readUInt(14).toInt()
                if (buffer[4].toInt() and 0xff != SerialProtocolVersion || payloadSize !in 0..maximumPayloadBytes) {
                    buffer = buffer.copyOfRange(1, buffer.size)
                    continue
                }
                val frameBytes = headerBytes + payloadSize + trailerBytes
                if (buffer.size < frameBytes) break

                val expectedCrc = buffer.readUInt(headerBytes + payloadSize)
                val actualCrc = crc32(buffer, 4, headerBytes + payloadSize)
                val type = SerialFrameType.fromCode(buffer[5].toInt() and 0xff)
                if (type != null && expectedCrc == actualCrc) {
                    frames += SerialFrame(
                        type = type,
                        requestId = buffer.readUInt(6),
                        sequence = buffer.readUInt(10),
                        payload = buffer.copyOfRange(headerBytes, headerBytes + payloadSize),
                    )
                    buffer = buffer.copyOfRange(frameBytes, buffer.size)
                } else {
                    buffer = buffer.copyOfRange(1, buffer.size)
                }
            }
            return frames
        }

        private fun ByteArray.indexOfMagic(): Int {
            for (index in 0..size - magic.size) {
                if (magic.indices.all { offset -> this[index + offset] == magic[offset] }) return index
            }
            return -1
        }
    }

    internal fun crc32(bytes: ByteArray, from: Int = 0, until: Int = bytes.size): UInt {
        var crc = 0xffffffffu
        for (index in from until until) {
            crc = crc xor (bytes[index].toUInt() and 0xffu)
            repeat(8) {
                crc = (crc shr 1) xor (0xedb88320u and (0u - (crc and 1u)))
            }
        }
        return crc.inv()
    }

    private fun ByteArray.writeUInt(offset: Int, value: UInt) {
        this[offset] = value.toByte()
        this[offset + 1] = (value shr 8).toByte()
        this[offset + 2] = (value shr 16).toByte()
        this[offset + 3] = (value shr 24).toByte()
    }

    private fun ByteArray.readUInt(offset: Int): UInt =
        (this[offset].toUInt() and 0xffu) or
            ((this[offset + 1].toUInt() and 0xffu) shl 8) or
            ((this[offset + 2].toUInt() and 0xffu) shl 16) or
            ((this[offset + 3].toUInt() and 0xffu) shl 24)
}
