package com.rsvpnano

import com.rsvpnano.models.NanoLocales
import com.rsvpnano.models.NanoFontSummary
import com.rsvpnano.models.NanoSettings
import com.rsvpnano.models.NanoSettingsSchema
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
            """{"obsolete":true,"interface":{"locale":"ru","screensaver":"screenOff"},"reading":{"pauseMode":"sentenceEnd","footerMetric":"bookTime","batteryLabel":"timeRemaining"}}""",
        )

        assertEquals("ru", settings.`interface`.locale)
        assertEquals("screenOff", settings.`interface`.screensaver)
        assertEquals("sentenceEnd", settings.reading.pauseMode)
        assertEquals("bookTime", settings.reading.footerMetric)
        assertEquals("timeRemaining", settings.reading.batteryLabel)
        assertTrue(settings.reading.batteryIconVisible)
    }

    @Test
    fun acceptsExternalLocaleTags() {
        assertEquals("es", NanoSettingsSchema.coerceLocale("es"))
        assertEquals("zh-Hans", NanoSettingsSchema.coerceLocale("zh-Hans"))
        assertEquals("ja", NanoSettingsSchema.coerceLocale("ja"))
        assertEquals(NanoLocales.DEFAULT, NanoSettingsSchema.coerceLocale(""))
    }

    @Test
    fun localeAffinityKeepsMixedScriptFontsSelectable() {
        val font = NanoFontSummary("hebrew", "Noto Serif Hebrew", listOf("he"), scriptMask = 8)

        assertTrue(font.usableFor("he", 9))
        assertTrue(font.usableFor("he-IL", 9))
        assertFalse(font.usableFor("en", 9))

        val math = NanoFontSummary("math", "STIX Two Math", scriptMask = 1 shl 9)
        assertFalse(math.usableFor("en", (1 shl 9) or 1))
        assertTrue(math.usableFor("en", 1 shl 9))
        assertFalse(math.usableFor("en", 1))
    }
}
