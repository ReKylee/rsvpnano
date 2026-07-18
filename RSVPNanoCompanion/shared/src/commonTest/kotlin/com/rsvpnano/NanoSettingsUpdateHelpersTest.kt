package com.rsvpnano

import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFalse
import kotlin.test.assertNotSame
import kotlin.test.assertTrue

class NanoSettingsUpdateHelpersTest {
    @Test
    fun readingHelpersReturnUpdatedCopiesWithoutMutatingOriginal() {
        val original = sampleSettings()
        val updated = original
            .withWpm(320)
            .withPauseMode("instant")
            .withPacingLongWordMs(120)
            .withPacingComplexWordMs(80)
            .withPacingPunctuationMs(200)

        assertNotSame(original, updated)
        assertEquals(250, original.reading.wpm)
        assertEquals(320, updated.reading.wpm)
        assertEquals("instant", updated.reading.pauseMode)
        assertEquals(100, updated.reading.pacing.longWordDelayMs)
        assertEquals(100, updated.reading.pacing.complexWordDelayMs)
        assertEquals(200, updated.reading.pacing.punctuationDelayMs)
        assertEquals(original.`interface`, updated.`interface`)
    }

    @Test
    fun numericHelpersNormalizeSharedSettingsValues() {
        val updated = sampleSettings()
            .withWpm(103)
            .withPacingLongWordMs(626)
            .withPacingComplexWordMs(-20)
            .withBrightnessIndex(99)
            .withFontSizeIndex(-1)
            .withTracking(10)
            .withAnchorPercent(12)
            .withGuideWidth(19)
            .withGuideGap(99)

        assertEquals(100, updated.reading.wpm)
        assertEquals(600, updated.reading.pacing.longWordDelayMs)
        assertEquals(0, updated.reading.pacing.complexWordDelayMs)
        assertEquals(19, updated.`interface`.brightnessIndex)
        assertEquals(0, updated.reading.typography.fontSizeIndex)
        assertEquals(3, updated.reading.typography.tracking)
        assertEquals(30, updated.reading.typography.anchor)
        assertEquals(20, updated.reading.typography.guideWidth)
        assertEquals(8, updated.reading.typography.guideGap)
    }

    @Test
    fun displayHelpersReturnUpdatedCopiesWithoutMutatingOriginal() {
        val original = sampleSettings()
        val updated = original
            .withBrightnessIndex(4)
            .withHandedness("left")
            .withFooterMetric("chapterTime")
            .withBatteryLabel("timeRemaining")
            .withThemeId("night")
            .withPhantomWords(true)
            .withFontSizeIndex(2)

        assertNotSame(original, updated)
        assertEquals(1, original.`interface`.brightnessIndex)
        assertEquals(4, updated.`interface`.brightnessIndex)
        assertTrue(updated.reading.leftHanded)
        assertEquals("chapterTime", updated.reading.footerMetric)
        assertEquals("timeRemaining", updated.reading.batteryLabel)
        assertEquals("night", updated.`interface`.selectedThemeId)
        assertTrue(updated.reading.phantomWords)
        assertEquals(2, updated.reading.typography.fontSizeIndex)
    }

    @Test
    fun themeIdHelperFallsBackToDefaultWhenBlank() {
        val custom = sampleSettings().withThemeId("catppuccin-mocha")
        val blank = sampleSettings().withThemeId("")

        assertEquals("catppuccin-mocha", custom.`interface`.selectedThemeId)
        assertEquals("default", blank.`interface`.selectedThemeId)
    }

    @Test
    fun typographyHelpersReturnUpdatedCopiesWithoutMutatingOriginal() {
        val original = sampleSettings()
        val updated = original
            .withTypeface("atkinson")
            .withFocusHighlight(false)
            .withTracking(2)
            .withAnchorPercent(36)
            .withGuideWidth(18)
            .withGuideGap(4)

        assertNotSame(original, updated)
        assertEquals("serif", original.reading.typography.fontId)
        assertEquals("atkinson", updated.reading.typography.fontId)
        assertFalse(updated.reading.typography.focusHighlight)
        assertEquals(2, updated.reading.typography.tracking)
        assertEquals(36, updated.reading.typography.anchor)
        assertEquals(18, updated.reading.typography.guideWidth)
        assertEquals(4, updated.reading.typography.guideGap)
        assertEquals(original.`interface`, updated.`interface`)
    }
}
