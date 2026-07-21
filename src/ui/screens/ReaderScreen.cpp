#include "ui/screens/ReaderScreen.h"
#include <esp_log.h>

#include <algorithm>
#include <cstdio>

#include "board/BoardPower.h"
#include "settings/SettingsRules.h"
#include "storage/StorageManager.h"
#include "text/LatinText.h"
#include "ui/screens/Screens.h"

namespace screens {
    namespace {

        constexpr ui::Rect batteryRect(int16_t width) {
            return {static_cast<int16_t>(std::max<int16_t>(0, width - 126)), 0, 116, 36};
        }

        constexpr uint16_t kPreviousSentenceTapWidth = 112;
        constexpr uint16_t kTapSlop = 26;
        constexpr uint16_t kDoubleTapSlop = 92;
        constexpr uint16_t kSwipeThreshold = 40;
        constexpr uint16_t kAxisBias = 12;
        constexpr uint16_t kScrubStep = 22;
        constexpr uint32_t kDoubleTapWindowMs = 520;
        constexpr uint32_t kWpmFeedbackMs = 900;
        constexpr int kMaxScrubSteps = 96;
        constexpr size_t kPhantomBeforeTargets[] = {64, 96, 144};
        constexpr size_t kPhantomAfterTargets[] = {96, 144, 208};

        bool continuation(uint8_t value) {
            return (value & 0xC0U) == 0x80U;
        }

        bool nextCodepoint(std::string_view text, size_t& index, uint16_t& codepoint) {
            if (index >= text.length())
                return false;
            const uint8_t first = static_cast<uint8_t>(text[index++]);
            if (first < 0x80U) {
                codepoint = first;
                return true;
            }
            auto next = [&](uint8_t& value) {
                if (index >= text.length() || !continuation(static_cast<uint8_t>(text[index])))
                    return false;
                value = static_cast<uint8_t>(text[index++]);
                return true;
            };
            if ((first & 0xE0U) == 0xC0U) {
                uint8_t b1 = 0;
                codepoint = next(b1) ? static_cast<uint16_t>(((first & 0x1FU) << 6U) | (b1 & 0x3FU)) : '?';
                return true;
            }
            if ((first & 0xF0U) == 0xE0U) {
                uint8_t b1 = 0, b2 = 0;
                codepoint = next(b1) && next(b2)
                              ? static_cast<uint16_t>(((first & 0x0FU) << 12U) | ((b1 & 0x3FU) << 6U) | (b2 & 0x3FU))
                              : '?';
                return true;
            }
            while (index < text.length() && continuation(static_cast<uint8_t>(text[index])))
                ++index;
            codepoint = '?';
            return true;
        }

        bool wordCodepoint(uint16_t codepoint) {
            return codepoint < 0x80U
                     ? LatinText::isWordCharacter(static_cast<uint8_t>(codepoint))
                     : (codepoint >= 0x00C0U && codepoint <= 0x024FU) || (codepoint >= 0x0400U && codepoint <= 0x052FU);
        }

        int focusOrdinal(int length) {
            if (length <= 1)
                return 0;
            if (length <= 5)
                return 1;
            if (length <= 9)
                return 2;
            if (length <= 13)
                return 3;
            return 4;
        }

    } // namespace

    ReaderScreen::ReaderScreen(Arduino_GFX& gfx, settings::ReadingSettings& settings) :
            gfx_(gfx),
            text_(gfx),
            settings_(settings) {}

    void ReaderScreen::begin(const ui::themes::Theme& theme) {
        text_.begin();
        applyTheme(theme);
    }

    void ReaderScreen::applyTheme(const ui::themes::Theme& theme) {
        themeTypography_ = theme.definition.typography;
        refreshTypography();
    }

    void ReaderScreen::refreshTypography() {
        typography_ = settings::effectiveTypography(session.state.bookTypographyOverride, themeTypography_);
        const auto families = fonts.families();
        const auto selected = std::ranges::find_if(families, [this](const FontCatalog::Family& family) {
            return family.id == typography_.fontId;
        });
        const size_t familyIndex = selected == families.end() ? 0 : selected - families.begin();
        font_ = fonts.load(familyIndex, typography_.fontSizeIndex);
        ++fontRevision_;
    }

    bool ReaderScreen::openBook(ui::Context& ui, StorageManager& storage, Preferences& preferences, size_t index,
                                uint32_t nowMs) {
        if (!storage.mounted() || index >= storage.bookCount())
            return false;
        status(ui, ui.text(UiText::OpeningBook), storage.bookDisplayName(index), {}, 5);
        ReadingProgress::save(session, preferences, true, nowMs);
        ReadingProgress::mirror(session, store);
        store.close();
        session.metadata.clear();
        StorageManager::IndexedBookLoadOptions options;
        std::string loadedPath;
        size_t loadedIndex = index;
        options.loadedPath = &loadedPath;
        options.loadedIndex = &loadedIndex;
        if (!storage.loadIndexedBook(index, store, session.metadata, options)) {
            status(ui, ui.text(UiText::BookFailed), storage.bookDisplayName(index), ui.text(UiText::CheckSdCard));
            delay(1200);
            return false;
        }
        session.bookIndex = loadedIndex;
        session.path = loadedPath;
        session.fromStorage = true;
        session.state = {};
        session.lastSavedWordIndex = static_cast<size_t>(-1);
        ReadingLoop::setBookStore(session, store, nowMs);

        const uint32_t savedWord = ReadingProgress::restore(session, store);
        if (savedWord != ReadingProgress::kNoSavedWordIndex) {
            ReadingLoop::seekTo(session, savedWord);
            ReadingProgress::cache(session, preferences, static_cast<uint32_t>(session.currentIndex));
        } else {
            preferences.putString("book", session.path.c_str());
        }
        refreshTypography();
        return true;
    }

    void ReaderScreen::loadInitialBook(ui::Context& ui, StorageManager& storage, Preferences& preferences,
                                       uint32_t nowMs) {
        storage.refreshBooks();
        const std::string savedPath = preferences.getString("book").c_str();
        if (!savedPath.empty()) {
            const int savedBook = storage.bookIndex(savedPath);
            if (savedBook >= 0 && openBook(ui, storage, preferences, static_cast<size_t>(savedBook), nowMs))
                return;
            ESP_LOGE("reader", "saved book not found: %s", savedPath.c_str());
        }
        if (storage.bookCount() > 0 && openBook(ui, storage, preferences, 0, nowMs))
            return;
        session.metadata.clear();
        session.path = "";
        session.fromStorage = false;
        session.state = {};
        refreshTypography();
        ReadingLoop::begin(session, nowMs);
    }

    void ReaderScreen::draw(ui::Context& ui, const StorageManager& storage, const Board::Power::BatteryState& battery,
                            uint32_t nowMs) {
        const std::string bookTitle = ReadingProgress::title(session, storage);
        const bool reading = session.playing;
        const settings::ReadingSettings& settings = settings_;
        const std::string before = settings.phantomWords ? phantomBefore(session, typography_.fontSizeIndex) : "";
        const std::string after = settings.phantomWords ? phantomAfter(session, typography_.fontSizeIndex) : "";
        const ChapterMarker* chapter = session.metadata.chapterAt(session.currentIndex);
        const std::string_view chapterLabel = chapter != nullptr && !chapter->title.empty()
                                                ? std::string_view{chapter->title}
                                                : std::string_view{bookTitle};
        const uint8_t progress = ReadingProgress::percent(session.currentIndex, ReadingLoop::wordCount(session));
        std::string footer;
        if (reading || settings.footerMetric == settings::FooterMetric::percentage) {
            footer = std::to_string(progress) + "%";
        } else {
            size_t remainingWords = ReadingLoop::wordCount(session) > session.currentIndex
                                      ? ReadingLoop::wordCount(session) - session.currentIndex
                                      : 0;
            if (settings.footerMetric == settings::FooterMetric::chapterTime) {
                for (const ChapterMarker& marker: session.metadata.chapters) {
                    if (marker.wordIndex > session.currentIndex) {
                        remainingWords = marker.wordIndex - session.currentIndex;
                        break;
                    }
                }
            }
            const uint32_t minutes = static_cast<uint32_t>((remainingWords + settings.wpm - 1) / settings.wpm);
            footer = ui.text(settings.footerMetric == settings::FooterMetric::chapterTime ? UiText::ChapterShort
                                                                                          : UiText::BookShort);
            footer += ' ';
            footer += minutes >= 60 ? std::to_string(minutes / 60) + "h" : std::to_string(minutes) + "m";
        }
        const std::string overlay = wpmFeedbackUntilMs_ > nowMs ? std::to_string(settings.wpm) + " WPM" : "";

        const ui::Rect readingArea{0, 36, ui.width(), static_cast<int16_t>(std::max<int16_t>(0, ui.height() - 72))};
        if (ui.redraw(readingArea, frameSignature(before, session.currentWord, after, overlay, settings))) {
            Arduino_GFX& gfx = ui.gfx();
            background_ = ui.color(ui::themes::ColorRole::Background);
            text_.setFont(font_);
            text_.setTextColor(ui.color(ui::themes::ColorRole::Foreground),
                               ui.color(ui::themes::ColorRole::Background));

            const std::string& word = session.currentWord;
            const int focus = focusIndex(word);
            const int16_t wordWidth = textWidth(word);
            int16_t focusCenter = wordWidth / 2;
            if (focus >= 0) {
                int glyph = 0;
                int16_t cursor = 0;
                for (size_t index = 0; index < word.length();) {
                    uint16_t codepoint = 0;
                    nextCodepoint(word, index, codepoint);
                    const int16_t advance = text_.glyphAdvance(codepoint);
                    if (glyph++ == focus) {
                        focusCenter = static_cast<int16_t>(cursor + advance / 2);
                        break;
                    }
                    cursor = static_cast<int16_t>(cursor + advance + typography_.tracking);
                }
            }
            const int16_t anchor = static_cast<int16_t>((ui.width() * typography_.anchor) / 100);
            const int16_t x = static_cast<int16_t>(anchor - focusCenter);
            const int16_t inkTop = font_->wordInkTop;
            const int16_t inkBottom = font_->wordInkBottom;
            const int16_t baseline = static_cast<int16_t>(((ui.height() - (inkBottom - inkTop + 1)) / 2) - inkTop);
            const int16_t guideTop = static_cast<int16_t>(baseline + inkTop - 6);
            const int16_t guideBottom = static_cast<int16_t>(baseline + inkBottom + 6);
            const uint16_t guide = ui.blend(ui::themes::ColorRole::Foreground, 96);
            gfx.drawFastHLine(static_cast<int16_t>(anchor - typography_.guideWidth), guideTop,
                              static_cast<int16_t>(typography_.guideWidth - typography_.guideGap), guide);
            gfx.drawFastHLine(static_cast<int16_t>(anchor + typography_.guideGap), guideTop,
                              static_cast<int16_t>(typography_.guideWidth - typography_.guideGap), guide);
            gfx.drawFastHLine(static_cast<int16_t>(anchor - typography_.guideWidth), guideBottom,
                              static_cast<int16_t>(typography_.guideWidth - typography_.guideGap), guide);
            gfx.drawFastHLine(static_cast<int16_t>(anchor + typography_.guideGap), guideBottom,
                              static_cast<int16_t>(typography_.guideWidth - typography_.guideGap), guide);
            const uint16_t marker = typography_.focusHighlight ? ui.color(ui::themes::ColorRole::Accent) : guide;
            gfx.drawFastVLine(anchor, guideTop, 5, marker);
            gfx.drawFastVLine(anchor, static_cast<int16_t>(guideBottom - 4), 5, marker);

            if (!before.empty())
                drawText(before, static_cast<int16_t>(x - 24 - textWidth(before)), baseline,
                         ui.blend(ui::themes::ColorRole::Foreground, 62));
            drawWord(word, x, baseline, focus, ui);
            if (!after.empty())
                drawText(after, static_cast<int16_t>(x + wordWidth + 24), baseline,
                         ui.blend(ui::themes::ColorRole::Foreground, 62));

            gfx.setFont(static_cast<const GFXfont*>(nullptr));
            gfx.setTextWrap(false);
            gfx.setTextSize(2);
            gfx.setTextColor(ui.color(ui::themes::ColorRole::Muted));
            gfx.setCursor(settings.leftHanded ? 18 : static_cast<int16_t>(ui.width() - 42),
                          static_cast<int16_t>(ui.height() / 2 - 8));
            gfx.print("<<");
            if (!overlay.empty()) {
                gfx.setTextColor(ui.color(ui::themes::ColorRole::Accent));
                gfx.setCursor(static_cast<int16_t>((ui.width() - overlay.size() * 12) / 2),
                              static_cast<int16_t>(ui.height() - 56));
                gfx.print(overlay.c_str());
            }
        }

        const bool showChapter = !reading || settings.chapterVisibleWhileReading;
        const bool showProgress = !reading || settings.progressVisibleWhileReading;
        const bool showBattery = !reading || settings.batteryVisibleWhileReading;
        const bool showBatteryIcon = settings.batteryIconVisible && showBattery;
        const int16_t footerWidth = showProgress ? static_cast<int16_t>(footer.size() * 12) : 0;
        const int16_t footerX = settings.leftHanded ? 18 : static_cast<int16_t>(ui.width() - 18 - footerWidth);
        const int16_t chapterX =
            settings.leftHanded && showProgress ? static_cast<int16_t>(footerX + footerWidth + 24) : 18;
        const int16_t chapterWidth =
            showProgress ? static_cast<int16_t>(ui.width() - 60 - footerWidth) : static_cast<int16_t>(ui.width() - 36);
        ui.label({chapterX, static_cast<int16_t>(ui.height() - 26), chapterWidth, 16},
                 showChapter ? chapterLabel.empty() ? ui.text(UiText::Start) : chapterLabel : std::string_view{}, 2,
                 ui::themes::ColorRole::Muted, settings.leftHanded ? ui::TextAlign::Right : ui::TextAlign::Left);
        ui.label({footerX, static_cast<int16_t>(ui.height() - 26), footerWidth, 16}, footer, 2,
                 ui::themes::ColorRole::Muted, settings.leftHanded ? ui::TextAlign::Left : ui::TextAlign::Right);

        char batteryText[12];
        if (settings.batteryLabel == settings::BatteryLabel::voltage && battery.status.voltage > 0)
            std::snprintf(batteryText, sizeof(batteryText), "%.2fV", battery.status.voltage);
        else if (settings.batteryLabel == settings::BatteryLabel::timeRemaining) {
            constexpr uint32_t kNominalRuntimeMinutes = 600;
            const uint32_t minutes = static_cast<uint32_t>(battery.status.percent) * kNominalRuntimeMinutes / 100;
            if (minutes >= 60)
                std::snprintf(batteryText, sizeof(batteryText), "%lu.%luh", static_cast<unsigned long>(minutes / 60),
                              static_cast<unsigned long>(minutes % 60 / 6));
            else
                std::snprintf(batteryText, sizeof(batteryText), "%lum", static_cast<unsigned long>(minutes));
        } else
            std::snprintf(batteryText, sizeof(batteryText), "%u%%", static_cast<unsigned int>(battery.status.percent));
        const std::string_view batteryLabel{batteryText};
        const ui::Rect batteryArea = batteryRect(ui.width());

        ui.battery(batteryArea, battery.status.percent, battery.charging, batteryLabel, showBatteryIcon);
    }

    bool ReaderScreen::batteryTouched(const ui::Touch& touch) const {
        return ui::contains(batteryRect(gfx_.width()), touch.x, touch.y);
    }

    bool ReaderScreen::batteryTapped(const ui::Touch& touch) const {
        return ui::hasTouch(touch, ui::TouchTap) && batteryTouched(touch);
    }

    bool ReaderScreen::batteryLongPressed(const ui::Touch& touch) const {
        return ui::hasTouch(touch, ui::TouchHold) && batteryTouched(touch);
    }

    bool ReaderScreen::previousSentenceTapped(uint16_t x, uint16_t y) const {
        if (ui::contains(batteryRect(gfx_.width()), x, y))
            return false;
        return settings_.leftHanded
                 ? x <= kPreviousSentenceTapWidth
                 : x >= static_cast<uint16_t>(std::max<int16_t>(0, gfx_.width() - kPreviousSentenceTapWidth));
    }

    void ReaderScreen::handleTouch(ui::Context& ui, uint32_t nowMs, Preferences& preferences,
                                   settings::SettingsStore& settingsStore) {
        const ui::Touch* event = ui.touch();
        if (event == nullptr)
            return;
        const ui::Touch& touch = *event;
        const bool ended = ui::hasTouch(touch, ui::TouchRelease);
        const bool held = ui::hasTouch(touch, ui::TouchHold);

        if (ended && touchIntent_ == TouchIntent::PlayHold) {
            resetTouch();
            requestPause(preferences, nowMs);
            return;
        }
        if (ui::hasTouch(touch, ui::TouchStart)) {
            touching_ = true;
            touchStartX_ = touch.x;
            touchStartY_ = touch.y;
            touchStartWord_ = session.currentIndex;
            scrubSteps_ = 0;
            touchIntent_ = TouchIntent::None;
            return;
        }
        if (!touching_)
            return;

        const int deltaX = static_cast<int>(touch.x) - touchStartX_;
        const int deltaY = static_cast<int>(touch.y) - touchStartY_;
        const int absX = std::abs(deltaX);
        const int absY = std::abs(deltaY);
        const bool tapLike = absX <= kTapSlop && absY <= kTapSlop;

        if (touchIntent_ == TouchIntent::None && tapLike && batteryLongPressed(touch)) {
            settings_.batteryIconVisible = !settings_.batteryIconVisible;
            settingsStore.acceptChanges();
            lastTapValid_ = false;
            resetTouch();
            return;
        }
        if (touchIntent_ == TouchIntent::None && ended && tapLike && batteryTapped({ui::TouchTap, touch.x, touch.y})) {
            settings_.batteryLabel = settings::cycleEnum(settings_.batteryLabel);
            settingsStore.acceptChanges();
            lastTapValid_ = false;
            resetTouch();
            return;
        }

        if (session.playing) {
            if (held && tapLike && !playLocked_) {
                resetTouch();
                requestPause(preferences, nowMs);
                return;
            }
            if (!ended)
                return;
            resetTouch();
            if (!tapLike) {
                lastTapValid_ = false;
                return;
            }
            if (previousSentenceTapped(touch.x, touch.y)) {
                lastTapValid_ = false;
                ReadingLoop::rewindSentence(session);
                ReadingLoop::pause(session);
                pauseAtSentenceEndRequested_ = false;
                playLocked_ = false;
                ReadingProgress::save(session, preferences, true, nowMs);
                return;
            }
            if (playLocked_ || pauseAtSentenceEndRequested_) {
                lastTapValid_ = false;
                requestPause(preferences, nowMs);
                return;
            }
            if (doubleTap(touch.x, touch.y, nowMs))
                requestPause(preferences, nowMs);
            return;
        }

        if (touchIntent_ == TouchIntent::None && !ended && held && tapLike) {
            lastTapValid_ = false;
            touchIntent_ = TouchIntent::PlayHold;
            start(nowMs, false);
            return;
        }
        if (touchIntent_ == TouchIntent::None) {
            if (absX >= kSwipeThreshold && absX > absY + kAxisBias) {
                lastTapValid_ = false;
                touchIntent_ = TouchIntent::Scrub;
            } else if (absY >= kSwipeThreshold && absY > absX + kAxisBias) {
                lastTapValid_ = false;
                touchIntent_ = TouchIntent::Wpm;
            }
        }
        if (touchIntent_ == TouchIntent::Scrub) {
            const int steps = scrubSteps(deltaX);
            const bool changed = steps != scrubSteps_;
            scrubSteps_ = steps;
            if (changed)
                ReadingLoop::seekRelative(session, touchStartWord_, steps);
            if (ended) {
                resetTouch();
                ReadingProgress::save(session, preferences, true, nowMs);
            }
            return;
        }
        if (touchIntent_ == TouchIntent::Wpm) {
            if (!ended)
                return;
            resetTouch();
            ReadingLoop::adjustWpm(settings_, deltaY < 0 ? 1 : -1);
            settingsStore.acceptChanges();
            wpmFeedbackUntilMs_ = nowMs + kWpmFeedbackMs;
            return;
        }
        if (!ended)
            return;

        resetTouch();
        if (!tapLike) {
            lastTapValid_ = false;
            return;
        }
        const uint16_t footerTapWidth = std::min<uint16_t>(220, static_cast<uint16_t>(gfx_.width() / 2));
        if (touch.y >= static_cast<uint16_t>(std::max<int16_t>(0, gfx_.height() - 40))
            && (settings_.leftHanded ? touch.x <= footerTapWidth
                                     : touch.x >= static_cast<uint16_t>(gfx_.width() - footerTapWidth))) {
            settings_.footerMetric = settings::cycleEnum(settings_.footerMetric);
            settingsStore.acceptChanges();
            lastTapValid_ = false;
            return;
        }
        if (previousSentenceTapped(touch.x, touch.y)) {
            lastTapValid_ = false;
            ReadingLoop::rewindSentence(session);
            pauseAtSentenceEndRequested_ = false;
            playLocked_ = false;
            ReadingProgress::save(session, preferences, true, nowMs);
            return;
        }
        if (doubleTap(touch.x, touch.y, nowMs))
            start(nowMs, true);
    }

    void ReaderScreen::toggle(Preferences& preferences, uint32_t nowMs) {
        if (session.playing)
            requestPause(preferences, nowMs);
        else
            start(nowMs, true);
    }

    void ReaderScreen::update(Preferences& preferences, uint32_t nowMs) {
        if (wpmFeedbackUntilMs_ > 0 && nowMs >= wpmFeedbackUntilMs_) {
            wpmFeedbackUntilMs_ = 0;
        }
        if (shouldFinishPause(nowMs)) {
            finishPause(preferences, nowMs);
            return;
        }
        if (!session.playing)
            return;
        const size_t previousIndex = session.currentIndex;
        if (ReadingLoop::update(session, settings_, nowMs)) {
            ReadingProgress::saveChapterTransition(session, preferences, store, previousIndex, session.currentIndex,
                                                   nowMs);
        }
    }

    void ReaderScreen::start(uint32_t nowMs, bool locked) {
        playLocked_ = locked;
        pauseAtSentenceEndRequested_ = false;
        wpmFeedbackUntilMs_ = 0;
        ReadingLoop::start(session, nowMs);
    }

    void ReaderScreen::requestPause(Preferences& preferences, uint32_t nowMs) {
        if (!session.playing)
            return;
        playLocked_ = false;
        if (settings_.pauseMode == settings::PauseMode::instant) {
            finishPause(preferences, nowMs);
            return;
        }
        pauseAtSentenceEndRequested_ = true;
        if (shouldFinishPause(nowMs)) {
            finishPause(preferences, nowMs);
        }
    }

    bool ReaderScreen::shouldFinishPause(uint32_t nowMs) const {
        if (!session.playing || !pauseAtSentenceEndRequested_)
            return false;
        const uint32_t durationMs = ReadingLoop::currentWordDurationMs(session, settings_);
        return durationMs > 0 && ReadingLoop::elapsedInCurrentWordMs(session, nowMs) >= durationMs
            && (ReadingLoop::currentWordEndsSentence(session) || ReadingLoop::atEnd(session));
    }

    void ReaderScreen::finishPause(Preferences& preferences, uint32_t nowMs) {
        ReadingLoop::pause(session);
        pauseAtSentenceEndRequested_ = false;
        playLocked_ = false;
        ReadingProgress::save(session, preferences, true, nowMs);
    }

    bool ReaderScreen::doubleTap(uint16_t x, uint16_t y, uint32_t nowMs) {
        const bool matched = lastTapValid_ && nowMs - lastTapMs_ <= kDoubleTapWindowMs
                          && std::abs(static_cast<int>(x) - lastTapX_) <= kDoubleTapSlop
                          && std::abs(static_cast<int>(y) - lastTapY_) <= kDoubleTapSlop;
        if (matched) {
            lastTapValid_ = false;
            return true;
        }
        lastTapValid_ = true;
        lastTapMs_ = nowMs;
        lastTapX_ = x;
        lastTapY_ = y;
        return false;
    }

    void ReaderScreen::resetTouch() {
        touching_ = false;
        touchIntent_ = TouchIntent::None;
        scrubSteps_ = 0;
    }

    int ReaderScreen::scrubSteps(int deltaX) const {
        const int distance = std::abs(deltaX);
        if (distance < kSwipeThreshold)
            return 0;
        const int steps = std::min(1 + (distance - kSwipeThreshold) / kScrubStep, kMaxScrubSteps);
        return deltaX > 0 ? steps : -steps;
    }

    std::string ReaderScreen::phantomBefore(const ReadingSession& reader, uint8_t sizeIndex) const {
        if (ReadingLoop::wordCount(reader) == 0)
            return "";
        const size_t target = kPhantomBeforeTargets[std::min<size_t>(sizeIndex, 2)];
        size_t start = reader.currentIndex;
        size_t characters = 0;
        while (start > 0 && characters < target) {
            --start;
            characters += ReadingLoop::wordAt(reader, start).length() + (start + 1 < reader.currentIndex);
        }
        std::string result;
        result.reserve(characters);
        for (size_t index = start; index < reader.currentIndex; ++index) {
            if (!result.empty())
                result += ' ';
            result += ReadingLoop::wordAt(reader, index);
        }
        return result;
    }

    std::string ReaderScreen::phantomAfter(const ReadingSession& reader, uint8_t sizeIndex) const {
        if (reader.currentIndex + 1 >= ReadingLoop::wordCount(reader))
            return "";
        const size_t target = kPhantomAfterTargets[std::min<size_t>(sizeIndex, 2)];
        size_t end = reader.currentIndex + 1;
        size_t characters = 0;
        while (end < ReadingLoop::wordCount(reader) && characters < target) {
            characters += ReadingLoop::wordAt(reader, end).length() + (end > reader.currentIndex + 1);
            ++end;
        }
        std::string result;
        result.reserve(characters);
        for (size_t index = reader.currentIndex + 1; index < end; ++index) {
            if (!result.empty())
                result += ' ';
            result += ReadingLoop::wordAt(reader, index);
        }
        return result;
    }

    int ReaderScreen::focusIndex(std::string_view word) const {
        int characters = 0;
        for (size_t index = 0; index < word.length();) {
            uint16_t codepoint = 0;
            nextCodepoint(word, index, codepoint);
            if (wordCodepoint(codepoint))
                ++characters;
        }
        if (characters == 0)
            return word.empty() ? -1 : 0;
        const int target = std::min(focusOrdinal(characters), characters - 1);
        int ordinal = 0, glyph = 0;
        for (size_t index = 0; index < word.length(); ++glyph) {
            uint16_t codepoint = 0;
            nextCodepoint(word, index, codepoint);
            if (wordCodepoint(codepoint) && ordinal++ == target)
                return glyph;
        }
        return 0;
    }

    int16_t ReaderScreen::textWidth(std::string_view value) const {
        text_.setFont(font_);
        if (typography_.tracking == 0)
            return text_.textAdvance(value);
        int16_t width = 0;
        for (size_t index = 0; index < value.length();) {
            uint16_t codepoint = 0;
            nextCodepoint(value, index, codepoint);
            width = static_cast<int16_t>(width + text_.glyphAdvance(codepoint));
            if (index < value.length())
                width = static_cast<int16_t>(width + typography_.tracking);
        }
        return width;
    }

    void ReaderScreen::drawText(std::string_view value, int16_t x, int16_t baseline, uint16_t color) {
        text_.setFont(font_);
        text_.setTextColor(color, background_);
        if (typography_.tracking == 0) {
            text_.drawString(value, x, baseline);
            return;
        }
        for (size_t index = 0; index < value.length();) {
            uint16_t codepoint = 0;
            nextCodepoint(value, index, codepoint);
            text_.drawCodepoint(codepoint, x, baseline);
            x = static_cast<int16_t>(x + text_.glyphAdvance(codepoint) + typography_.tracking);
        }
    }

    void ReaderScreen::drawWord(std::string_view word, int16_t x, int16_t baseline, int focus, ui::Context& ui) {
        int glyph = 0;
        for (size_t index = 0; index < word.length(); ++glyph) {
            uint16_t codepoint = 0;
            nextCodepoint(word, index, codepoint);
            text_.setTextColor(typography_.focusHighlight && glyph == focus
                                   ? ui.color(ui::themes::ColorRole::Accent)
                                   : ui.color(ui::themes::ColorRole::Foreground),
                               ui.color(ui::themes::ColorRole::Background));
            text_.drawCodepoint(codepoint, x, baseline);
            x = static_cast<int16_t>(x + text_.glyphAdvance(codepoint) + typography_.tracking);
        }
    }

    uint32_t ReaderScreen::frameSignature(std::string_view before, std::string_view word, std::string_view after,
                                          std::string_view overlay, const settings::ReadingSettings& settings) const {
        uint32_t value = ui::Context::signature(before);
        value = ui::Context::signature(word, value);
        value = ui::Context::signature(after, value);
        value = ui::Context::signature(overlay, value);
        value = ui::Context::combine(value, static_cast<uint8_t>(typography_.tracking));
        value = ui::Context::combine(value, typography_.anchor);
        value = ui::Context::combine(value, typography_.guideWidth);
        value = ui::Context::combine(value, typography_.guideGap);
        value = ui::Context::combine(value, typography_.focusHighlight);
        value = ui::Context::combine(value, settings.leftHanded);
        value = ui::Context::combine(value, fontRevision_);
        return value;
    }

} // namespace screens
