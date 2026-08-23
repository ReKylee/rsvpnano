package com.rsvpnano.web

import kotlin.test.Test
import kotlin.test.assertContentEquals
import kotlin.test.assertEquals
import kotlin.test.assertTrue

class SerialProtocolTest {
    @Test
    fun decoderResynchronizesAndAcceptsFragmentedFrames() {
        val first = SerialFrame(SerialFrameType.Request, 17u, payload = "hello".encodeToByteArray())
        val second = SerialFrame(SerialFrameType.End, 17u, 1u)
        val encoded = byteArrayOf(1, 2, 3) + SerialFrameCodec.encode(first) + SerialFrameCodec.encode(second)
        val decoder = SerialFrameCodec.Decoder()

        assertTrue(decoder.feed(encoded.copyOfRange(0, 9)).isEmpty())
        val frames = decoder.feed(encoded.copyOfRange(9, encoded.size))

        assertEquals(listOf(SerialFrameType.Request, SerialFrameType.End), frames.map(SerialFrame::type))
        assertContentEquals(first.payload, frames.first().payload)
    }

    @Test
    fun decoderDropsCorruptFrameAndFindsTheNextMagic() {
        val corrupt = SerialFrameCodec.encode(SerialFrame(SerialFrameType.Data, 1u, payload = byteArrayOf(9, 8, 7)))
        corrupt[18] = 0
        val valid = SerialFrame(SerialFrameType.Pong)

        val frames = SerialFrameCodec.Decoder().feed(corrupt + SerialFrameCodec.encode(valid))

        assertEquals(listOf(SerialFrameType.Pong), frames.map(SerialFrame::type))
    }
}
