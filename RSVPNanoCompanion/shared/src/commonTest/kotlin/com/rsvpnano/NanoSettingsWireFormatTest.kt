package com.rsvpnano

import com.rsvpnano.models.NanoSettings
import kotlinx.serialization.encodeToString
import kotlinx.serialization.json.Json
import kotlinx.serialization.json.jsonObject
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFalse
import kotlin.test.assertTrue

class NanoSettingsWireFormatTest {
    private val json = Json {
        encodeDefaults = true
        ignoreUnknownKeys = true
    }

    @Test
    fun serializesTheFirmwareDeviceSettingsShapeWithoutLegacyFields() {
        val document = json.parseToJsonElement(json.encodeToString(sampleSettings())).jsonObject

        assertEquals(setOf("reading", "interface", "network", "updates"), document.keys)
        assertFalse("display" in document)
        assertFalse("themes" in document)
        assertFalse("fonts" in document)

        val reading = document.getValue("reading").jsonObject
        assertEquals(
            setOf("fontId", "fontSizeIndex", "focusHighlight", "tracking", "anchor", "guideWidth", "guideGap"),
            reading.getValue("typography").jsonObject.keys,
        )
        assertEquals(
            setOf("longWordDelayMs", "complexWordDelayMs", "punctuationDelayMs"),
            reading.getValue("pacing").jsonObject.keys,
        )
    }

    @Test
    fun decodesStableEnumNamesFromFirmware() {
        val settings = json.decodeFromString<NanoSettings>(
            """{"obsolete":true,"interface":{"language":"russian","screensaver":"screenOff"},"reading":{"pauseMode":"sentenceEnd","footerMetric":"bookTime","batteryLabel":"timeRemaining"}}""",
        )

        assertEquals("russian", settings.`interface`.language)
        assertEquals("screenOff", settings.`interface`.screensaver)
        assertEquals("sentenceEnd", settings.reading.pauseMode)
        assertEquals("bookTime", settings.reading.footerMetric)
        assertEquals("timeRemaining", settings.reading.batteryLabel)
        assertTrue(settings.reading.batteryIconVisible)
    }
}
