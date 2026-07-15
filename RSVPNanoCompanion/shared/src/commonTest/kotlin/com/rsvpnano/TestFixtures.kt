package com.rsvpnano

import com.rsvpnano.models.NanoSettings
import com.rsvpnano.models.NanoBook
import com.rsvpnano.models.NanoBookMetadata

internal fun sampleSettings(): NanoSettings = NanoSettings(
    version = 1,
    applied = true,
    reading = NanoSettings.Reading(
        wpm = 250,
        pauseMode = "sentence",
        pacing = NanoSettings.Pacing(longWordMs = 0, complexWordMs = 0, punctuationMs = 0),
    ),
    display = NanoSettings.Display(
        brightnessIndex = 1,
        handedness = "right",
        footerMetric = "battery",
        batteryLabel = "battery",
        language = 0,
        phantomWords = false,
        fontSizeIndex = 1,
    ),
    typography = NanoSettings.Typography(
        typeface = "serif",
        focusHighlight = true,
        tracking = 0,
        anchorPercent = 50,
        guideWidth = 1,
        guideGap = 1,
    ),
)

internal fun sampleBook(
    id: String,
    title: String = id.substringBeforeLast('.'),
    wordCount: Int = 0,
): NanoBook = NanoBook(
    id = id,
    name = id,
    category = "book",
    metadata = NanoBookMetadata(title = title, wordCount = wordCount),
)
