#pragma once

#include <Preferences.h>
#include "fonts/AlphaFont.h"
#include "fonts/FontCatalog.h"
#include "reader/ReadingLoop.h"
#include "settings/SettingsModel.h"
#include "settings/SettingsStore.h"
#include "storage/index/IndexedBookStore.h"
#include "storage/index/ReadingProgress.h"
#include "ui/Ui.h"

namespace screens {

    struct ReaderSession {
        uint32_t wpmFeedbackUntilMs = 0;
        bool playLocked = false;
        bool pauseAtSentenceEndRequested = false;
    };

    struct BatteryModel {
        uint8_t percent = 0;
        float voltage = 0;
        bool charging = false;
        settings::BatteryLabel label = settings::BatteryLabel::percentage;
    };

    struct BatteryState {
        uint32_t lastSampleMs = 0;
        settings::BatteryLabel label = settings::BatteryLabel::percentage;
        uint8_t percent = 0;
        float voltage = 0;
        bool charging = false;

        BatteryModel view() const {
            return {percent, voltage, charging, label};
        }
        void update(uint32_t nowMs, bool force = false);
    };

    class ReaderScreen {
    public:
        explicit ReaderScreen(Arduino_GFX& gfx);

        ReadingLoop reader;
        ReaderSession session;
        ReadingProgress::Session book;
        IndexedBookStore store;
        FontCatalog fonts;
        BatteryState battery;

        void begin(settings::ReadingSettings& settings, const ui::themes::Theme& theme, uint32_t nowMs);
        void applyTheme(const ui::themes::Theme& theme);
        void refreshTypography();
        bool openBook(ui::Context& ui, StorageManager& storage, Preferences& preferences, size_t index, uint32_t nowMs);
        void loadInitialBook(ui::Context& ui, StorageManager& storage, Preferences& preferences, uint32_t nowMs);
        void draw(ui::Context& ui, const StorageManager& storage, uint32_t nowMs);
        bool batteryTapped(const ui::Touch& touch) const;
        bool previousSentenceTapped(uint16_t x) const;
        void handleTouch(ui::Context& ui, uint32_t nowMs, Preferences& preferences,
                         settings::SettingsStore& settingsStore);
        void toggle(Preferences& preferences, uint32_t nowMs);
        void update(Preferences& preferences, uint32_t nowMs);

    private:
        int focusIndex(std::string_view word) const;
        int16_t textWidth(std::string_view text) const;
        void drawText(std::string_view text, int16_t x, int16_t baseline, uint16_t color);
        void drawWord(std::string_view word, int16_t x, int16_t baseline, int focus, ui::Context& ui);
        std::string phantomBefore(const ReadingLoop& reader, uint8_t sizeIndex) const;
        std::string phantomAfter(const ReadingLoop& reader, uint8_t sizeIndex) const;
        uint32_t frameSignature(std::string_view before, std::string_view word, std::string_view after,
                                std::string_view overlay, const settings::ReadingSettings& settings) const;

        enum class TouchIntent : uint8_t {
            None,
            PlayHold,
            Scrub,
            Wpm
        };
        bool doubleTap(uint16_t x, uint16_t y, uint32_t nowMs);
        void resetTouch();
        int scrubSteps(int deltaX) const;
        void start(uint32_t nowMs, bool locked);
        void requestPause(Preferences& preferences, uint32_t nowMs);
        bool shouldFinishPause(uint32_t nowMs) const;
        void finishPause(Preferences& preferences, uint32_t nowMs);

        Arduino_GFX& gfx_;
        mutable ui::fonts::AlphaTextRenderer<640> text_;
        const ui::fonts::AlphaFont* font_ = nullptr;
        uint32_t fontRevision_ = 0;
        settings::ReadingSettings* settings_ = nullptr;
        settings::TypographySettings themeTypography_;
        settings::TypographySettings typography_;
        uint16_t background_ = 0;
        bool touching_ = false;
        uint16_t touchStartX_ = 0;
        uint16_t touchStartY_ = 0;
        size_t touchStartWord_ = 0;
        int scrubSteps_ = 0;
        TouchIntent touchIntent_ = TouchIntent::None;
        uint32_t lastTapMs_ = 0;
        uint16_t lastTapX_ = 0;
        uint16_t lastTapY_ = 0;
        bool lastTapValid_ = false;
    };

} // namespace screens
