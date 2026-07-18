package com.rsvpnano.models

import kotlinx.serialization.Serializable

@Serializable
data class NanoChapter(
    val title: String,
    val wordIndex: Int,
)

@Serializable
data class NanoBook(
    val id: String,
    val name: String,
    val bytes: Int = 0,
    val category: String,
    val metadata: NanoBookMetadata,
    val source: NanoBookSource? = null,
    val reading: NanoReadingProgress? = null,
) {
    val displayTitle: String
        get() = metadata.title.takeIf { it.isNotBlank() } ?: name.substringAfterLast('/').ifBlank { "Untitled" }
}

@Serializable
data class NanoBookMetadata(
    val title: String,
    val author: String = "",
    val wordCount: Int = 0,
    val chapterCount: Int = 0,
    val chapters: List<NanoChapter> = emptyList(),
)

@Serializable
data class NanoBookSource(
    val size: Long,
    val fingerprint: Long,
)

@Serializable
data class NanoReadingProgress(
    val wordIndex: Int,
    val percent: Int,
    val remainingWords: Int,
    val estimatedMinutes: Int,
    val currentChapter: NanoCurrentChapter? = null,
)

@Serializable
data class NanoCurrentChapter(
    val number: Int,
    val title: String,
)

@Serializable
data class PendingUpload(
    val id: String,
    val title: String,
    val sourceUrl: String? = null,
    val body: String,
    val createdAt: String // ISO-8601 timestamp string; keep simple for portability
)

@Serializable
data class NanoInfo(
    val name: String,
    val apiVersion: Int,
    val mode: String? = null,
    val networkSsid: String? = null,
    val firmwareVersion: String = "",
    val otaAsset: String = "",
)

@Serializable
data class NanoUploadResponse(
    val path: String? = null,
    val id: String? = null,
    val deleted: Boolean? = null,
    val wordIndex: Int? = null,
    val percent: Int? = null,
)

@Serializable
data class NanoRssFeeds(
    val feeds: List<String> = emptyList(),
)

@Serializable
data class NanoFocusTimer(
    val name: String = "",
    val focusMinutes: Int = 25,
    val breakMinutes: Int = 5,
    val rounds: Int = 4,
)

@Serializable
data class NanoFocusTimers(
    val timers: List<NanoFocusTimer> = emptyList(),
)

@Serializable
data class NanoWifiSettings(
    val passwordSet: Boolean = false,
)

@Serializable
data class NanoWifiUpdate(
    val ssid: String,
    val password: String,
)

@Serializable
data class NanoFontCatalogItem(
    val id: String,
    val name: String,
    val files: Map<String, String> = emptyMap(),
)

@Serializable
data class NanoFontSummary(
    val id: String,
    val name: String,
)

@Serializable
data class NanoFontsResponse(
    val fonts: List<NanoFontSummary> = emptyList(),
)

@Serializable
data class NanoThemeCatalogItem(
    val id: String,
    val name: String,
    val file: String,
)

@Serializable
data class NanoThemeSummary(
    val id: String,
    val name: String,
)

@Serializable
data class NanoThemesResponse(
    val themes: List<NanoThemeSummary> = emptyList(),
)

@Serializable
data class NanoSettings(
    val reading: Reading = Reading(),
    val `interface`: Interface = Interface(),
    val network: Network = Network(),
    val updates: Updates = Updates(),
) {
    @Serializable
    data class Reading(
        val wpm: Int = 300,
        val pauseMode: String = NanoSettingsSchema.PAUSE_MODE_SENTENCE_END,
        val phantomWords: Boolean = true,
        val chapterScrollReversed: Boolean = false,
        val footerMetric: String = NanoSettingsSchema.FOOTER_PERCENTAGE,
        val batteryLabel: String = NanoSettingsSchema.BATTERY_PERCENTAGE,
        val batteryIconVisible: Boolean = true,
        val batteryVisibleWhileReading: Boolean = true,
        val chapterVisibleWhileReading: Boolean = false,
        val progressVisibleWhileReading: Boolean = false,
        val leftHanded: Boolean = false,
        val typography: Typography = Typography(),
        val pacing: Pacing = Pacing(),
    )

    @Serializable
    data class Pacing(
        val longWordDelayMs: Int = 200,
        val complexWordDelayMs: Int = 200,
        val punctuationDelayMs: Int = 200,
    )

    @Serializable
    data class Typography(
        val fontId: String = NanoSettingsSchema.TYPEFACE_DEFAULT,
        val fontSizeIndex: Int = 0,
        val focusHighlight: Boolean = true,
        val tracking: Int = 0,
        val anchor: Int = 30,
        val guideWidth: Int = 30,
        val guideGap: Int = 5,
    )

    @Serializable
    data class Interface(
        val brightnessPercent: Int = 70,
        val language: String = NanoSettingsSchema.LANGUAGE_ENGLISH,
        val standbyTimerIndex: Int = NanoSettingsSchema.STANDBY_TIMER_NEVER,
        val screensaver: String = NanoSettingsSchema.SCREENSAVER_LIFE,
        val selectedThemeId: String = NanoSettingsSchema.THEME_DEFAULT,
    )

    @Serializable
    data class Network(
        val wifiSsid: String = "",
    )

    @Serializable
    data class Updates(
        val automatic: Boolean = false,
        val repositoryOwner: String = "",
        val releaseTag: String = "",
    )

    fun withWpm(value: Int): NanoSettings =
        copy(reading = reading.copy(wpm = NanoSettingsSchema.snapWpm(value)))

    fun withPauseMode(value: String): NanoSettings =
        copy(reading = reading.copy(pauseMode = value))

    fun withPacingLongWordMs(value: Int): NanoSettings =
        copy(
            reading = reading.copy(
                pacing = reading.pacing.copy(longWordDelayMs = NanoSettingsSchema.snapPacingMs(value)),
            ),
        )

    fun withPacingComplexWordMs(value: Int): NanoSettings =
        copy(
            reading = reading.copy(
                pacing = reading.pacing.copy(complexWordDelayMs = NanoSettingsSchema.snapPacingMs(value)),
            ),
        )

    fun withPacingPunctuationMs(value: Int): NanoSettings =
        copy(
            reading = reading.copy(
                pacing = reading.pacing.copy(punctuationDelayMs = NanoSettingsSchema.snapPacingMs(value)),
            ),
        )

    fun withBrightnessPercent(value: Int): NanoSettings =
        copy(`interface` = `interface`.copy(brightnessPercent = NanoSettingsSchema.coerceBrightnessPercent(value)))

    fun withThemeId(value: String): NanoSettings {
        return copy(`interface` = `interface`.copy(selectedThemeId = value.ifBlank { NanoSettingsSchema.THEME_DEFAULT }))
    }

    fun withHandedness(value: String): NanoSettings =
        copy(reading = reading.copy(leftHanded = value == NanoSettingsSchema.HANDEDNESS_LEFT))

    fun withFooterMetric(value: String): NanoSettings =
        copy(reading = reading.copy(footerMetric = value))

    fun withBatteryLabel(value: String): NanoSettings =
        copy(reading = reading.copy(batteryLabel = value))

    fun withBatteryIconVisible(value: Boolean): NanoSettings =
        copy(reading = reading.copy(batteryIconVisible = value))

    fun withReadingBattery(value: Boolean): NanoSettings =
        copy(reading = reading.copy(batteryVisibleWhileReading = value))

    fun withReadingChapter(value: Boolean): NanoSettings =
        copy(reading = reading.copy(chapterVisibleWhileReading = value))

    fun withReadingProgress(value: Boolean): NanoSettings =
        copy(reading = reading.copy(progressVisibleWhileReading = value))

    fun withScreensaver(value: String): NanoSettings =
        copy(`interface` = `interface`.copy(screensaver = NanoSettingsSchema.coerceScreensaver(value)))

    fun withStandbyTimerIndex(value: Int): NanoSettings =
        copy(`interface` = `interface`.copy(standbyTimerIndex = NanoSettingsSchema.coerceStandbyTimerIndex(value)))

    fun withLanguage(value: String): NanoSettings =
        copy(`interface` = `interface`.copy(language = NanoSettingsSchema.coerceLanguage(value)))

    fun withPhantomWords(value: Boolean): NanoSettings =
        copy(reading = reading.copy(phantomWords = value))

    fun withFontSizeIndex(value: Int): NanoSettings =
        copy(
            reading = reading.copy(
                typography = reading.typography.copy(
                    fontSizeIndex = NanoSettingsSchema.coerceFontSizeIndex(value),
                ),
            ),
        )

    fun withTypeface(value: String): NanoSettings =
        copy(reading = reading.copy(typography = reading.typography.copy(fontId = value)))

    fun withFocusHighlight(value: Boolean): NanoSettings =
        copy(reading = reading.copy(typography = reading.typography.copy(focusHighlight = value)))

    fun withTracking(value: Int): NanoSettings =
        copy(
            reading = reading.copy(
                typography = reading.typography.copy(tracking = NanoSettingsSchema.coerceTracking(value)),
            ),
        )

    fun withAnchorPercent(value: Int): NanoSettings =
        copy(
            reading = reading.copy(
                typography = reading.typography.copy(anchor = NanoSettingsSchema.coerceAnchorPercent(value)),
            ),
        )

    fun withGuideWidth(value: Int): NanoSettings =
        copy(
            reading = reading.copy(
                typography = reading.typography.copy(guideWidth = NanoSettingsSchema.snapGuideWidth(value)),
            ),
        )

    fun withGuideGap(value: Int): NanoSettings =
        copy(
            reading = reading.copy(
                typography = reading.typography.copy(guideGap = NanoSettingsSchema.coerceGuideGap(value)),
            ),
        )

    fun withUpdateOwner(value: String): NanoSettings =
        copy(updates = updates.copy(repositoryOwner = value))

    fun withUpdateTag(value: String): NanoSettings =
        copy(updates = updates.copy(releaseTag = value))

    fun withAutomaticUpdateChecks(value: Boolean): NanoSettings =
        copy(updates = updates.copy(automatic = value))
}

object NanoSettingsSchema {
    const val THEME_DEFAULT = "default"
    const val PAUSE_MODE_SENTENCE_END = "sentenceEnd"
    const val PAUSE_MODE_INSTANT = "instant"
    const val HANDEDNESS_LEFT = "left"
    const val HANDEDNESS_RIGHT = "right"
    const val FOOTER_PERCENTAGE = "percentage"
    const val FOOTER_CHAPTER_TIME = "chapterTime"
    const val FOOTER_BOOK_TIME = "bookTime"
    const val BATTERY_PERCENTAGE = "percentage"
    const val BATTERY_TIME_REMAINING = "timeRemaining"
    const val BATTERY_VOLTAGE = "voltage"
    const val SCREENSAVER_LIFE = "life"
    const val SCREENSAVER_MAZE = "maze"
    const val SCREENSAVER_VORONOI = "voronoi"
    const val SCREENSAVER_SCREEN_OFF = "screenOff"
    const val SCREENSAVER_REACTION = "reaction"
    const val LANGUAGE_ENGLISH = "english"
    const val LANGUAGE_SPANISH = "spanish"
    const val LANGUAGE_FRENCH = "french"
    const val LANGUAGE_GERMAN = "german"
    const val LANGUAGE_ROMANIAN = "romanian"
    const val LANGUAGE_POLISH = "polish"
    const val LANGUAGE_RUSSIAN = "russian"
    const val TYPEFACE_DEFAULT = "literata"

    const val WPM_MIN = 10
    const val WPM_MAX = 1000
    const val WPM_STEP = 10
    const val PACING_MS_MIN = 0
    const val PACING_MS_MAX = 600
    const val PACING_MS_STEP = 50
    const val BRIGHTNESS_MIN = 5
    const val BRIGHTNESS_MAX = 100
    const val STANDBY_TIMER_NEVER = 0
    const val STANDBY_TIMER_1_MIN = 1
    const val STANDBY_TIMER_5_MIN = 2
    const val STANDBY_TIMER_15_MIN = 3
    const val STANDBY_TIMER_30_MIN = 4
    const val FONT_SIZE_MIN = 0
    const val FONT_SIZE_MAX = 2
    const val TRACKING_MIN = -2
    const val TRACKING_MAX = 3
    const val ANCHOR_PERCENT_MIN = 30
    const val ANCHOR_PERCENT_MAX = 40
    const val GUIDE_WIDTH_MIN = 12
    const val GUIDE_WIDTH_MAX = 30
    const val GUIDE_WIDTH_STEP = 2
    const val GUIDE_GAP_MIN = 2
    const val GUIDE_GAP_MAX = 8

    fun snapToStep(value: Int, step: Int): Int =
        ((value + step / 2) / step) * step

    fun snapWpm(value: Int): Int {
        val clamped = value.coerceIn(WPM_MIN, WPM_MAX)
        return snapToStep(clamped, WPM_STEP).coerceIn(WPM_MIN, WPM_MAX)
    }

    fun snapPacingMs(value: Int): Int =
        snapToStep(value, PACING_MS_STEP).coerceIn(PACING_MS_MIN, PACING_MS_MAX)

    fun coerceBrightnessPercent(value: Int): Int =
        value.coerceIn(BRIGHTNESS_MIN, BRIGHTNESS_MAX)

    fun coerceScreensaver(value: String): String =
        when (value) {
            SCREENSAVER_MAZE,
            SCREENSAVER_VORONOI,
            SCREENSAVER_SCREEN_OFF,
            SCREENSAVER_REACTION
            -> value
            else -> SCREENSAVER_LIFE
        }

    fun coerceStandbyTimerIndex(value: Int): Int =
        value.coerceIn(STANDBY_TIMER_NEVER, STANDBY_TIMER_30_MIN)

    fun coerceLanguage(value: String): String =
        when (value) {
            LANGUAGE_SPANISH,
            LANGUAGE_FRENCH,
            LANGUAGE_GERMAN,
            LANGUAGE_ROMANIAN,
            LANGUAGE_POLISH,
            LANGUAGE_RUSSIAN,
            -> value
            else -> LANGUAGE_ENGLISH
        }

    fun coerceFontSizeIndex(value: Int): Int =
        value.coerceIn(FONT_SIZE_MIN, FONT_SIZE_MAX)

    fun coerceTracking(value: Int): Int =
        value.coerceIn(TRACKING_MIN, TRACKING_MAX)

    fun coerceAnchorPercent(value: Int): Int =
        value.coerceIn(ANCHOR_PERCENT_MIN, ANCHOR_PERCENT_MAX)

    fun snapGuideWidth(value: Int): Int =
        snapToStep(value, GUIDE_WIDTH_STEP).coerceIn(GUIDE_WIDTH_MIN, GUIDE_WIDTH_MAX)

    fun coerceGuideGap(value: Int): Int =
        value.coerceIn(GUIDE_GAP_MIN, GUIDE_GAP_MAX)
}

@Serializable
data class RememberedNano(
    val ssid: String,
)

@Serializable
data class CompanionAppSettings(
    val rememberedNano: RememberedNano? = null,
    val firmwareNotificationsEnabled: Boolean = false,
    val firmwareUpdateTarget: FirmwareUpdateTarget? = null,
    val lastNotifiedFirmwareVersion: String? = null,
)

@Serializable
data class FirmwareUpdateTarget(
    val currentVersion: String,
    val otaAsset: String,
    val owner: String,
    val tag: String,
)

data class FirmwareRelease(
    val version: String,
    val assets: List<String>,
)

data class FirmwareUpdate(
    val currentVersion: String,
    val availableVersion: String,
)
