#pragma once

#include <Preferences.h>
#include "board/BoardPower.h"
#include "fonts/AlphaFont.h"
#include "fonts/FontCatalog.h"
#include "reader/ReadingLoop.h"
#include "settings/SettingsModel.h"
#include "settings/SettingsStore.h"
#include "storage/index/IndexedBookStore.h"
#include "storage/index/ReadingProgress.h"
#include "ui/Ui.h"
#include "ui/screens/PageReaderScreen.h"

namespace screens {

    class ReaderScreen {
    public:
        ReaderScreen(Arduino_GFX& gfx, settings::ReadingSettings& settings);

        ReadingSession session;
        IndexedBookStore store;
        FontCatalog fonts;
        void begin(const ui::themes::Theme& theme);
        void applyTheme(const ui::themes::Theme& theme);
        void refreshTypography();
        bool openBook(ui::Context& ui, StorageManager& storage, Preferences& preferences, size_t index, uint32_t nowMs);
        void loadInitialBook(ui::Context& ui, StorageManager& storage, Preferences& preferences, uint32_t nowMs);
        void draw(ui::Context& ui, const StorageManager& storage, const Board::Power::BatteryState& battery,
                  uint32_t nowMs);
        bool batteryTapped(const ui::Touch& touch) const;
        bool batteryLongPressed(const ui::Touch& touch) const;
        bool batteryTouched(const ui::Touch& touch) const;
        bool previousSentenceTapped(uint16_t x, uint16_t y) const;
        void handleTouch(ui::Context& ui, uint32_t nowMs, Preferences& preferences,
                         settings::SettingsStore& settingsStore);
        void toggle(Preferences& preferences, uint32_t nowMs);
        void update(Preferences& preferences, uint32_t nowMs);

    private:
        int focusIndex(std::string_view word) const;
        void drawWord(std::string_view word, int16_t x, int16_t baseline, int focus, ui::Context& ui);
        std::string phantomBefore(const ReadingSession& reader, uint8_t sizeIndex) const;
        std::string phantomAfter(const ReadingSession& reader, uint8_t sizeIndex) const;
        uint32_t frameSignature(std::string_view before, std::string_view word, std::string_view after,
                                std::string_view overlay, const settings::ReadingSettings& settings) const;

        enum class TouchIntent : uint8_t {
            None,
            PlayHold,
            Scrub,
            Wpm,
            Paragraph
        };
        void browseParagraphs(uint16_t y, uint32_t nowMs);
        bool doubleTap(uint16_t x, uint16_t y, uint32_t nowMs);
        void resetTouch();
        int scrubSteps(int deltaX) const;
        void start(uint32_t nowMs, bool locked);
        void requestPause(Preferences& preferences, uint32_t nowMs);
        bool shouldFinishPause(uint32_t nowMs) const;
        void finishPause(Preferences& preferences, uint32_t nowMs);

        Arduino_GFX& gfx_;
        mutable ui::fonts::AlphaTextRenderer<640> text_;
        settings::ReadingSettings& settings_;
        const ui::fonts::AlphaFont* font_ = nullptr;
        uint32_t fontRevision_ = 0;
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
        uint32_t wpmFeedbackUntilMs_ = 0;
        bool playLocked_ = false;
        bool pauseAtSentenceEndRequested_ = false;
        PageReader::State pageState_;
        bool pagePreview_ = false;
        uint32_t paragraphTickMs_ = 0;
        int32_t paragraphRemainder_ = 0;
    };

} // namespace screens
