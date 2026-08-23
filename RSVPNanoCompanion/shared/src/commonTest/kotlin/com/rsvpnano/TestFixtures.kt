package com.rsvpnano

import com.rsvpnano.models.NanoSettings
import com.rsvpnano.models.NanoBook
import com.rsvpnano.models.NanoBookMetadata

internal fun sampleSettings(): NanoSettings = NanoSettings(
    reading = NanoSettings.Reading(
        wpm = 250,
        pauseMode = "sentenceEnd",
        footerMetric = "percentage",
        batteryLabel = "percentage",
        phantomWords = false,
        typography = NanoSettings.Typography(
            fontId = "serif",
            fontSizeIndex = 1,
            focusHighlight = true,
            tracking = 0,
            anchor = 30,
            guideWidth = 12,
            guideGap = 2,
        ),
        pacing = NanoSettings.Pacing(longWordDelayMs = 0, complexWordDelayMs = 0, punctuationDelayMs = 0),
    ),
    `interface` = NanoSettings.Interface(brightnessPercent = 10),
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
