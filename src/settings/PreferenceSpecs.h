#pragma once

#include "settings/Setting.h"

namespace settings {

    inline constexpr char kPrefsNamespace[] = "rsvp";
    static_assert(sizeof(kPrefsNamespace) - 1 <= 15);

    namespace prefs {

        struct BookPath : StringSetting<"book"> {};

        struct Wpm : Bounded<"wpm", uint16_t, 300, 10, 1000> {};

        struct BrightnessIndex : CountedIndex<"bright", uint8_t, 13> {};

        struct ThemeId : StringSetting<"theme_id"> {};

        struct UiLanguage : Bounded<"ui_lang", uint8_t, 0, 0, 5> {};

        struct Handedness : Bounded<"handed", uint8_t, 0, 0, 1> {};

        struct PhantomWords : Scalar<"phantom_on", bool, true> {};

        struct ChapterScrollReversed : Scalar<"ch_scroll_rev", bool, false> {};

        struct FooterMetricMode : Bounded<"prog_md", uint8_t, 0, 0, 2> {};

        struct BatteryLabelMode : Bounded<"bat_md", uint8_t, 0, 0, 2> {};

        struct ScreensaverMode : Scalar<"scrn_sv", uint8_t, 0> {};

        struct ReaderBatteryVisible : Scalar<"read_bat", bool, true> {};

        struct ReaderChapterVisible : Scalar<"read_ch", bool, false> {};

        struct ReaderProgressVisible : Scalar<"read_pct", bool, false> {};

        struct ReaderFontSizeIndex : CountedIndex<"font_size", uint8_t, 0> {};

        struct ReaderTypefaceId : StringSetting<"font_id"> {};

        struct TypographyFocusHighlight : Scalar<"type_hlt", bool, true> {};

        struct TypographyTracking : Bounded<"type_trk", int8_t, 0, -2, 3> {};

        struct TypographyAnchor : Bounded<"type_anc", uint8_t, 30, 30, 40> {};

        struct TypographyGuideWidth : Bounded<"type_wid", uint8_t, 30, 12, 30, 2> {};

        struct TypographyGuideGap : Bounded<"type_gap", uint8_t, 5, 2, 8> {};

        struct PacingLongWordDelay : Bounded<"pace_lms", uint16_t, 200, 0, 600, 50> {};

        struct PacingComplexWordDelay : Bounded<"pace_cms", uint16_t, 200, 0, 600, 50> {};

        struct PacingPunctuationDelay : Bounded<"pace_pms", uint16_t, 200, 0, 600, 50> {};

        struct PauseMode : Bounded<"pause_md", uint8_t, 0, 0, 1> {};

        struct StandbyTimerIndex : CountedIndex<"stby_tmr", uint8_t, 1> {};

        struct WifiSsid : StringSetting<"wifi_ssid"> {};

        struct WifiPassword : StringSetting<"wifi_pass"> {};

        struct OtaAuto : Scalar<"ota_auto", bool, false> {};

        struct OtaOwner : StringSetting<"ota_owner"> {};

        struct OtaTag : StringSetting<"ota_tag"> {};

    } // namespace prefs

} // namespace settings
