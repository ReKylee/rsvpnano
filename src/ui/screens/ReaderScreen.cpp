#include "ui/screens/ReaderScreen.h"
#include <esp_log.h>

#include <algorithm>
#include <array>
#include <cstdio>

#include "board/BoardPower.h"
#include "settings/SettingsRules.h"
#include "storage/StorageManager.h"
#include "text/UnicodeText.h"
#include "text/Utf8Text.h"
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
        constexpr int32_t kParagraphScrollScale = 1'000'000;
        constexpr int32_t kMaximumParagraphRate = 5'000;
        constexpr size_t kPhantomBeforeTargets[] = {64, 96, 144};
        constexpr size_t kPhantomAfterTargets[] = {96, 144, 208};

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
            settings_(settings),
            face_(fonts.loadFace(0, 0)) {}

    void ReaderScreen::begin(const ui::themes::Theme& theme) {
        text_.begin();
        applyTheme(theme);
        refreshTypography();
    }

    void ReaderScreen::applyTheme(const ui::themes::Theme& theme) {
        background_ = theme.definition.colors.background;
        renderedWordIndex_ = SIZE_MAX;
        ++typographyRevision_;
    }

    void ReaderScreen::refreshTypography() {
        if (settings_.mode == settings::ReadingMode::page)
            pagePreview_ = false;
        typography_ = settings_.typography;
        fonts.clearLoaded();
        loadedWordIndex_ = SIZE_MAX;
        loadedFamilyIndex_ = SIZE_MAX;
        renderedWordIndex_ = SIZE_MAX;
        prefetchedWordIndex_ = SIZE_MAX;
        rsvpBidi_.clear();
        rsvpLine_.clear();
        rsvpParagraph_ = {};
        ++typographyRevision_;

        const auto families = fonts.families();
        if (families.size() <= 1 || ReadingLoop::wordCount(session) == 0)
            return;
        std::vector<uint8_t> prepared(families.size());
        std::vector<size_t> firstWords(families.size(), SIZE_MAX);
        const size_t sizeIndex = settings_.mode == settings::ReadingMode::page
                                   ? RFont4::kCompactStrikeIndex
                                   : static_cast<size_t>(typography_.fontSizeIndex);
        const auto prepare = [&](size_t wordIndex, std::string_view locale, uint32_t scripts) {
            for (const UnicodeText::ScriptTag& script: UnicodeText::SupportedScripts) {
                if ((scripts & script.mask) == 0)
                    continue;
                const std::string_view requested = settings::fontForText(
                    session.state.overrides, locale, script.mask, typography_.fontId);
                const size_t family = FontCatalog::selectFamily(families, requested, locale, script.mask);
                if (family != 0 && !prepared[family]) {
                    fonts.loadFace(family, sizeIndex);
                    prepared[family] = true;
                    firstWords[family] = wordIndex;
                }
            }
        };
        prepare(0, session.metadata.localeAt(0), session.metadata.scriptMaskAt(0));
        for (const BookTextRun& run: session.metadata.textRuns) {
            const std::string_view locale = run.locale.empty() ? std::string_view{session.metadata.locale}
                                                               : std::string_view{run.locale};
            prepare(run.wordIndex, locale, run.scriptMask);
        }

        if (settings_.mode != settings::ReadingMode::rsvp)
            return;
        for (size_t family = 1; family < firstWords.size(); ++family) {
            const size_t wordIndex = firstWords[family];
            if (wordIndex == SIZE_MAX || wordIndex >= ReadingLoop::wordCount(session)
                || fontChoice(wordIndex) != family)
                continue;
            const FontCatalog::Face face = fonts.loadFace(family, sizeIndex);
            activateFace(face);
            const std::string_view word = ReadingLoop::wordAt(session, wordIndex);
            if (!face.shaper) {
                text_.prepare(word);
                continue;
            }
            const ReadingLoop::TextParagraph paragraph = ReadingLoop::paragraphAt(session, wordIndex);
            const size_t localWord = wordIndex - paragraph.firstWord;
            if (localWord >= paragraph.wordOffsets.size())
                continue;
            rsvpGlyphs_.clear();
            const size_t offset = paragraph.wordOffsets[localWord];
            const bool rightToLeft = session.metadata.directionAt(wordIndex) == TextDirection::rtl;
            if (face.shaper->get().shape(paragraph.text, offset, word.size(), rightToLeft,
                                         session.metadata.localeAt(wordIndex), text_, rsvpGlyphs_))
                text_.prepare(std::span<const ui::fonts::PositionedGlyph>{rsvpGlyphs_});
        }
        rsvpGlyphs_.clear();
    }

    size_t ReaderScreen::fontChoice(size_t wordIndex) const {
        const std::string_view word = ReadingLoop::wordAt(session, wordIndex);
        const std::string_view locale = session.metadata.localeAt(wordIndex);
        const uint32_t requiredScripts = UnicodeText::scriptsIn(word);
        const std::string_view requested = settings::fontForText(
            session.state.overrides, locale, requiredScripts, typography_.fontId);
        const auto families = fonts.families();
        if (families.empty())
            return 0;
        return FontCatalog::selectFamily(families, requested, locale, requiredScripts);
    }

    void ReaderScreen::activateFace(const FontCatalog::Face& face) {
        text_.setFont(face.raster.get());
    }

    void ReaderScreen::refreshTypeface() {
        if (loadedWordIndex_ == session.currentIndex && loadedFontSizeIndex_ == typography_.fontSizeIndex) {
            activateFace(face_);
            return;
        }
        loadedWordIndex_ = session.currentIndex;
        const size_t family = fontChoice(session.currentIndex);
        if (loadedFamilyIndex_ != family || loadedFontSizeIndex_ != typography_.fontSizeIndex) {
            loadedFamilyIndex_ = family;
            loadedFontSizeIndex_ = typography_.fontSizeIndex;
            ++fontRevision_;
        }
        face_ = fonts.loadFace(family, typography_.fontSizeIndex);
        activateFace(face_);
    }

    void ReaderScreen::prefetchNextWord() {
        if (settings_.mode == settings::ReadingMode::page || pagePreview_
            || renderedWordIndex_ != session.currentIndex)
            return;
        const size_t next = session.currentIndex + 1;
        if (next == prefetchedWordIndex_ || next >= ReadingLoop::wordCount(session))
            return;
        prefetchedWordIndex_ = next;
        if (face_.shaper || face_.raster.get().bitmap != nullptr || fontChoice(next) != loadedFamilyIndex_)
            return;
        activateFace(face_);
        text_.prepare(ReadingLoop::wordAt(session, next));
    }

    FontCatalog::Face ReaderScreen::pageTypeface(size_t wordIndex) {
        return fonts.loadFace(fontChoice(wordIndex), RFont4::kCompactStrikeIndex);
    }

    bool ReaderScreen::openBook(ui::Context& ui, StorageManager& storage, Preferences& preferences, size_t index,
                                uint32_t nowMs) {
        if (!storage.mounted() || index >= storage.bookCount())
            return false;
        status(ui, ui.text(UiText::OpeningBook), storage.bookDisplayName(index), {}, 5);
        prepareBookOpen(preferences, nowMs);
        StorageManager::IndexedBookLoadOptions options;
        std::string loadedPath;
        size_t loadedIndex = index;
        options.loadedPath = &loadedPath;
        options.loadedIndex = &loadedIndex;
        if (!storage.loadIndexedBook(index, store, session.metadata, options)) {
            status(ui, ui.text(UiText::BookFailed), storage.bookDisplayName(index), ui.text(UiText::CheckSdCard));
            return false;
        }
        finishBookOpen(preferences, loadedIndex, loadedPath, nowMs);
        return true;
    }

    void ReaderScreen::prepareBookOpen(Preferences& preferences, uint32_t nowMs) {
        ReadingProgress::save(session, preferences, true, nowMs);
        ReadingProgress::mirror(session, store);
        store.close();
        session.metadata.clear();
    }

    void ReaderScreen::finishBookOpen(Preferences& preferences, size_t loadedIndex, std::string_view loadedPath,
                                      uint32_t nowMs) {
        session.bookIndex = loadedIndex;
        session.path = loadedPath;
        session.fromStorage = true;
        session.state = {};
        pagePreview_ = false;
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
        pagePreview_ = false;
        refreshTypography();
        ReadingLoop::begin(session, nowMs);
    }

    void ReaderScreen::draw(ui::Context& ui, const StorageManager& storage, const Board::Power::BatteryState& battery,
                            uint32_t nowMs) {
        const std::string bookTitle = ReadingProgress::title(session, storage);
        const bool reading = session.playing;
        const settings::ReadingSettings& settings = settings_;
        const bool pageView = settings.mode == settings::ReadingMode::page || pagePreview_;
        if (!pageView)
            refreshTypeface();
        const std::string before =
            !pageView && settings.phantomWords ? phantomBefore(session, typography_.fontSizeIndex) : "";
        const std::string after =
            !pageView && settings.phantomWords ? phantomAfter(session, typography_.fontSizeIndex) : "";
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
                const auto next = std::ranges::upper_bound(session.metadata.chapters, session.currentIndex, {},
                                                           &ChapterMarker::wordIndex);
                if (next != session.metadata.chapters.end())
                    remainingWords = next->wordIndex - session.currentIndex;
            }
            const uint32_t minutes = static_cast<uint32_t>((remainingWords + settings.wpm - 1) / settings.wpm);
            footer = ui.text(settings.footerMetric == settings::FooterMetric::chapterTime ? UiText::ChapterShort
                                                                                          : UiText::BookShort);
            footer += ' ';
            footer += minutes >= 60 ? std::to_string(minutes / 60) + "h" : std::to_string(minutes) + "m";
        }
        const bool cjkPacing = ReadingLoop::pacingMode(session) == settings::ReadingPacing::cjkPhrase;
        const std::string overlay = wpmFeedbackUntilMs_ > nowMs
                                      ? std::to_string(settings.wpm) + (cjkPacing ? " CPM" : " WPM")
                                      : "";

        const ui::Rect readingArea{0, 36, ui.width(), static_cast<int16_t>(std::max<int16_t>(0, ui.height() - 72))};
        if (pageView) {
            const auto typeface = [this](size_t wordIndex) -> FontCatalog::Face {
                return pageTypeface(wordIndex);
            };
            PageReader::draw(pageState_, ui, text_, typeface, typography_, typographyRevision_, session, readingArea,
                             overlay);
        } else if (ui.redraw(readingArea, frameSignature(before, session.currentWord, after, overlay, settings))) {
            Arduino_GFX& gfx = ui.gfx();
            background_ = ui.color(ui::themes::ColorRole::Background);
            activateFace(face_);
            text_.setTextColor(ui.color(ui::themes::ColorRole::Foreground),
                               ui.color(ui::themes::ColorRole::Background));

            const std::string& word = session.currentWord;
            const bool bidi = session.metadata.requiresBidi(session.currentIndex, session.currentIndex + 1);
            const bool shaping = face_.shaper.has_value();
            size_t wordOffset = 0;
            bool shaped = false;
            int32_t shapedWidth = 0;
            if (bidi || shaping) {
                if (session.currentIndex < rsvpParagraph_.firstWord
                    || session.currentIndex >= rsvpParagraph_.lastWord) {
                    rsvpParagraph_ = ReadingLoop::paragraphAt(session, session.currentIndex);
                    rsvpBidi_.clear();
                    if (bidi) {
                        const auto analyzed = rsvpBidi_.reset(
                            rsvpParagraph_.text, session.metadata.directionAt(rsvpParagraph_.firstWord));
                        if (!analyzed)
                            ESP_LOGW("reader", "bidi analysis failed: %s", analyzed.error().c_str());
                    }
                }
                const size_t localWord = session.currentIndex - rsvpParagraph_.firstWord;
                wordOffset = rsvpParagraph_.wordOffsets[localWord];

                rsvpLine_.clear();
                if (bidi) {
                    if (const auto direction = rsvpBidi_.uniformRightToLeft(wordOffset, word.size())) {
                        rsvpLine_.push_back({wordOffset, word.size(), *direction});
                    } else {
                        if (auto resolved = rsvpBidi_.resolve({wordOffset, word.size()}, rsvpLine_); !resolved) {
                            ESP_LOGW("reader", "bidi layout failed: %s", resolved.error().c_str());
                            rsvpLine_.push_back({wordOffset, word.size(), false});
                        }
                    }
                } else {
                    rsvpLine_.push_back({wordOffset, word.size(), false});
                }

                rsvpGlyphs_.clear();
                if (shaping) {
                    const std::string_view locale = session.metadata.localeAt(session.currentIndex);
                    if (!bidi) {
                        const auto result = face_.shaper->get().shape(rsvpParagraph_.text, wordOffset, word.size(),
                                                                    false, locale, text_, rsvpGlyphs_);
                        shaped = result.has_value();
                        if (result)
                            shapedWidth = *result;
                        if (!result)
                            ESP_LOGW("reader", "shaping failed: %s", result.error().c_str());
                    } else {
                        shaped = true;
                        for (const BidiText::Run& run: rsvpLine_) {
                            const auto result = face_.shaper->get().shape(rsvpParagraph_.text, run.offset, run.length,
                                                                        run.rightToLeft, locale, text_, rsvpGlyphs_);
                            if (!result) {
                                ESP_LOGW("reader", "shaping failed: %s", result.error().c_str());
                                shaped = false;
                                break;
                            }
                            shapedWidth += *result;
                        }
                    }
                    if (!shaped)
                        rsvpGlyphs_.clear();
                }
                if (!shaped)
                    BidiText::visualCodepoints(rsvpParagraph_.text, rsvpLine_, rsvpVisual_);
            }
            const int focus = focusOffset(word);
            uint32_t shapedFocusCluster = UINT32_MAX;
            const int16_t wordWidth = shaped
                                        ? static_cast<int16_t>(std::clamp<int32_t>(shapedWidth, 0, INT16_MAX))
                                        : bidi ? wordAdvance(rsvpVisual_)
                                               : text_.textAdvance(word, typography_.tracking);
            int16_t focusCenter = wordWidth / 2;
            if (focus >= 0) {
                int16_t cursor = 0;
                if (shaped) {
                    const uint32_t target = static_cast<uint32_t>(wordOffset + focus);
                    for (const auto& glyph: rsvpGlyphs_) {
                        if (glyph.cluster <= target
                            && (shapedFocusCluster == UINT32_MAX || glyph.cluster > shapedFocusCluster))
                            shapedFocusCluster = glyph.cluster;
                    }
                    for (const auto& glyph: rsvpGlyphs_) {
                        if (glyph.cluster == shapedFocusCluster) {
                            focusCenter = static_cast<int16_t>(cursor + glyph.xOffset + glyph.xAdvance / 2);
                            break;
                        }
                        cursor = static_cast<int16_t>(cursor + glyph.xAdvance);
                    }
                } else if (bidi) {
                    uint32_t previous = 0;
                    bool previousValid = false;
                    for (const BidiText::Codepoint& codepoint: rsvpVisual_) {
                        if (previousValid && !codepoint.rightToLeft)
                            cursor = static_cast<int16_t>(cursor + text_.kerningAdjust(previous, codepoint.value));
                        const int16_t advance = text_.glyphAdvance(codepoint.value);
                        if (codepoint.offset == wordOffset + static_cast<size_t>(focus)) {
                            focusCenter = static_cast<int16_t>(cursor + advance / 2);
                            break;
                        }
                        cursor = static_cast<int16_t>(cursor + advance + typography_.tracking);
                        previous = codepoint.value;
                        previousValid = true;
                    }
                } else {
                    size_t offset = 0;
                    std::string_view remaining = word;
                    while (!remaining.empty()) {
                        const size_t bytes = remaining.size();
                        uint32_t codepoint = 0;
                        Utf8Text::next(remaining, codepoint);
                        const int16_t advance = text_.glyphAdvance(codepoint);
                        if (offset == static_cast<size_t>(focus)) {
                            focusCenter = static_cast<int16_t>(cursor + advance / 2);
                            break;
                        }
                        cursor = static_cast<int16_t>(cursor + advance + typography_.tracking);
                        offset += bytes - remaining.size();
                    }
                }
            }
            const int16_t anchor = static_cast<int16_t>((ui.width() * typography_.anchor) / 100);
            const int16_t x = static_cast<int16_t>(anchor - focusCenter);
            const int16_t inkTop = face_.raster.get().wordInkTop;
            const int16_t inkBottom = face_.raster.get().wordInkBottom;
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

            if (!before.empty()) {
                text_.setTextColor(ui.blend(ui::themes::ColorRole::Foreground, 62), background_);
                text_.drawString(before, static_cast<int16_t>(x - 24 - text_.textAdvance(before, typography_.tracking)),
                                 baseline, typography_.tracking);
            }
            if (!shaped)
                text_.prepare(word);
            if (shaped) {
                int16_t cursor = x;
                size_t first = 0;
                while (first < rsvpGlyphs_.size()) {
                    const bool highlighted = typography_.focusHighlight
                                          && rsvpGlyphs_[first].cluster == shapedFocusCluster;
                    size_t last = first + 1;
                    while (last < rsvpGlyphs_.size()
                           && (typography_.focusHighlight
                               && rsvpGlyphs_[last].cluster == shapedFocusCluster) == highlighted)
                        ++last;
                    text_.setTextColor(ui.color(highlighted ? ui::themes::ColorRole::Accent
                                                            : ui::themes::ColorRole::Foreground),
                                       background_);
                    cursor = static_cast<int16_t>(cursor + text_.drawGlyphs(
                        std::span{rsvpGlyphs_}.subspan(first, last - first), cursor, baseline));
                    first = last;
                }
            } else if (bidi)
                drawWord(rsvpVisual_, x, baseline, wordOffset, focus, ui);
            else
                drawWord(word, x, baseline, focus, ui);
            if (!after.empty()) {
                text_.setTextColor(ui.blend(ui::themes::ColorRole::Foreground, 62), background_);
                text_.drawString(after, static_cast<int16_t>(x + wordWidth + 24), baseline, typography_.tracking);
            }

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

        renderedWordIndex_ = pageView ? SIZE_MAX : session.currentIndex;
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

        if (touchIntent_ == TouchIntent::None && !ended && held && tapLike && !pagePreview_) {
            lastTapValid_ = false;
            touchIntent_ = TouchIntent::PlayHold;
            start(nowMs, false);
            return;
        }
        if (touchIntent_ == TouchIntent::None) {
            if (absX >= kSwipeThreshold && absX > absY + kAxisBias) {
                lastTapValid_ = false;
                touchIntent_ = TouchIntent::Scrub;
                pagePreview_ = settings_.mode != settings::ReadingMode::page;
            } else if (absY > absX + kAxisBias && (pagePreview_ || absY >= kSwipeThreshold)) {
                lastTapValid_ = false;
                touchIntent_ = pagePreview_ ? TouchIntent::Paragraph : TouchIntent::Wpm;
                if (touchIntent_ == TouchIntent::Paragraph) {
                    paragraphTickMs_ = nowMs;
                    paragraphRemainder_ = 0;
                }
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
        if (touchIntent_ == TouchIntent::Paragraph) {
            if (!ended)
                browseParagraphs(touch.y, nowMs);
            else {
                resetTouch();
                ReadingProgress::save(session, preferences, true, nowMs);
            }
            return;
        }
        if (!ended)
            return;

        resetTouch();
        if (!tapLike) {
            lastTapValid_ = false;
            return;
        }
        if (pagePreview_) {
            lastTapValid_ = false;
            pagePreview_ = false;
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
        prefetchNextWord();
        nowMs = millis();
        if (!session.playing)
            return;
        const size_t previousIndex = session.currentIndex;
        if (ReadingLoop::update(session, settings_, nowMs)) {
            ReadingProgress::saveChapterTransition(session, preferences, store, previousIndex, session.currentIndex,
                                                   nowMs);
        }
    }

    void ReaderScreen::start(uint32_t nowMs, bool locked) {
        pagePreview_ = false;
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
        paragraphTickMs_ = 0;
        paragraphRemainder_ = 0;
    }

    void ReaderScreen::browseParagraphs(uint16_t y, uint32_t nowMs) {
        const uint32_t elapsed = std::min<uint32_t>(nowMs - paragraphTickMs_, 100);
        paragraphTickMs_ = nowMs;
        const int32_t rate = ui::centeredDragRate(y, 0, gfx_.height(), 28, kMaximumParagraphRate);
        if (rate == 0) {
            paragraphRemainder_ = 0;
            return;
        }
        paragraphRemainder_ += rate * static_cast<int32_t>(elapsed);
        const int steps = paragraphRemainder_ / kParagraphScrollScale;
        paragraphRemainder_ %= kParagraphScrollScale;
        if (steps != 0)
            ReadingLoop::seekParagraph(session, steps);
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

    int ReaderScreen::focusOffset(std::string_view word) const {
        std::array<size_t, 5> offsets{};
        int characters = 0;
        size_t offset = 0;
        std::string_view remaining = word;
        while (!remaining.empty()) {
            const size_t bytes = remaining.size();
            uint32_t codepoint = 0;
            Utf8Text::next(remaining, codepoint);
            if (UnicodeText::isWordCharacter(codepoint)) {
                if (characters < static_cast<int>(offsets.size()))
                    offsets[characters] = offset;
                ++characters;
            }
            offset += bytes - remaining.size();
        }
        if (characters == 0)
            return word.empty() ? -1 : 0;
        const int target = std::min(focusOrdinal(characters), characters - 1);
        return static_cast<int>(offsets[target]);
    }

    int16_t ReaderScreen::wordAdvance(std::span<const BidiText::Codepoint> word) const {
        int16_t advance = 0;
        uint32_t previous = 0;
        bool previousValid = false;
        for (size_t index = 0; index < word.size(); ++index) {
            const BidiText::Codepoint& codepoint = word[index];
            if (previousValid && !codepoint.rightToLeft)
                advance = static_cast<int16_t>(advance + text_.kerningAdjust(previous, codepoint.value));
            advance = static_cast<int16_t>(advance + text_.glyphAdvance(codepoint.value)
                                           + (index + 1 < word.size() ? typography_.tracking : 0));
            previous = codepoint.value;
            previousValid = true;
        }
        return advance;
    }

    void ReaderScreen::drawWord(std::string_view word, int16_t x, int16_t baseline, int focus, ui::Context& ui) {
        size_t offset = 0;
        while (!word.empty()) {
            const size_t bytes = word.size();
            uint32_t codepoint = 0;
            Utf8Text::next(word, codepoint);
            text_.setTextColor(typography_.focusHighlight && focus >= 0 && offset == static_cast<size_t>(focus)
                                   ? ui.color(ui::themes::ColorRole::Accent)
                                   : ui.color(ui::themes::ColorRole::Foreground),
                               ui.color(ui::themes::ColorRole::Background));
            x = static_cast<int16_t>(x + text_.drawCodepoint(codepoint, x, baseline) + typography_.tracking);
            offset += bytes - word.size();
        }
    }

    void ReaderScreen::drawWord(std::span<const BidiText::Codepoint> word, int16_t x, int16_t baseline,
                                size_t wordOffset, int focus, ui::Context& ui) {
        uint32_t previous = 0;
        bool previousValid = false;
        for (size_t index = 0; index < word.size(); ++index) {
            const BidiText::Codepoint& codepoint = word[index];
            if (previousValid && !codepoint.rightToLeft)
                x = static_cast<int16_t>(x + text_.kerningAdjust(previous, codepoint.value));
            text_.setTextColor(typography_.focusHighlight && focus >= 0
                                       && codepoint.offset == wordOffset + static_cast<size_t>(focus)
                                   ? ui.color(ui::themes::ColorRole::Accent)
                                   : ui.color(ui::themes::ColorRole::Foreground),
                               ui.color(ui::themes::ColorRole::Background));
            x = static_cast<int16_t>(x + text_.drawCodepoint(codepoint.value, x, baseline)
                                     + (index + 1 < word.size() ? typography_.tracking : 0));
            previous = codepoint.value;
            previousValid = true;
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
