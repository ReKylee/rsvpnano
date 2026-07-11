#pragma once

#include "fonts/FontCatalog.h"
#include "reader/ReaderSettings.h"
#include "reader/ReadingLoop.h"
#include "storage/index/IndexedBookStore.h"
#include "storage/index/ReadingProgress.h"
#include "ui/Ui.h"
#include "ui/fonts/Font.h"

#include <Preferences.h>

namespace screens {

    struct BatteryModel {
        uint8_t percent = 0;
        float voltage = 0;
        bool charging = false;
        bool showVoltage = false;
    };

    struct BatteryState {
        uint32_t lastSampleMs = 0;
        uint8_t labelMode = 0;
        uint8_t percent = 0;
        float voltage = 0;
        bool charging = false;

        BatteryModel view() const {
            return {percent, voltage, charging, (labelMode & 1U) != 0};
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

        void begin(Preferences& preferences, uint32_t nowMs);
        bool openBook(ui::Context& ui, StorageManager& storage, Preferences& preferences, size_t index, uint32_t nowMs);
        void loadInitialBook(ui::Context& ui, StorageManager& storage, Preferences& preferences, uint32_t nowMs);
        void draw(ui::Context& ui, const StorageManager& storage, uint32_t nowMs);
        bool batteryTapped(const ui::Touch& touch) const;
        bool previousSentenceTapped(uint16_t x) const;
        void handleTouch(ui::Context& ui, uint32_t nowMs, Preferences& preferences);
        void toggle(Preferences& preferences, uint32_t nowMs);
        void update(Preferences& preferences, uint32_t nowMs);

    private:
        int focusIndex(const String& word) const;
        int16_t textWidth(const String& text) const;
        void drawText(const String& text, int16_t x, int16_t baseline, uint16_t color);
        void drawWord(const String& word, int16_t x, int16_t baseline, int focus, ui::Context& ui);
        String phantomBefore(const ReadingLoop& reader, uint8_t sizeIndex) const;
        String phantomAfter(const ReadingLoop& reader, uint8_t sizeIndex) const;
        uint32_t frameSignature(const String& before, const String& word, const String& after, const String& overlay,
                                const ReaderSettings& settings) const;

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
        mutable ui::fonts::TextRenderer<640> text_;
        ui::fonts::Font font_;
        ReaderTypography typography_;
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
