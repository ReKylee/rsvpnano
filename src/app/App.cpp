#include "app/App.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>

#include "board/BoardAudio.h"
#include "board/BoardInput.h"
#include "board/BoardPower.h"
#include "board/BoardSystem.h"
#include "settings/PreferenceSpecs.h"
#include "standby/Screensaver.h"
#include "storage/index/IndexedBook.h"
#include "storage/index/ReadingProgress.h"

namespace {

    namespace pref = settings::prefs;

    constexpr uint32_t kBootSplashMs = 650;
    constexpr uint32_t kBatterySampleMs = 120000;
    constexpr uint32_t kProgressSaveMs = 15000;
    constexpr uint32_t kNoSavedWordIndex = UINT32_MAX;
    constexpr uint32_t kReaderDoubleTapWindowMs = 520;
    constexpr uint32_t kWpmFeedbackMs = 900;
    constexpr uint16_t kTapSlopPx = 26;
    constexpr uint16_t kReaderDoubleTapSlopPx = 92;
    constexpr uint16_t kSwipeThresholdPx = 40;
    constexpr uint16_t kAxisBiasPx = 12;
    constexpr uint16_t kScrubStepPx = 22;
    constexpr uint16_t kPreviousSentenceTapWidthPx = 112;
    constexpr uint8_t kStandbyCellSizePx = 4;
    constexpr uint32_t kStandbyFrameMs = 160;
    constexpr uint32_t kStandbyKindMs = 45000;
    constexpr bool kCycleStandbyKinds = false;
    constexpr int kMaxScrubStepsPerGesture = 96;
    constexpr uint8_t kBrightnessStepPercent = 5;
    constexpr size_t kBrightnessStepCount = 100 / kBrightnessStepPercent;
    constexpr std::array<uint32_t, 5> kStandbyMs = {
        0, 1UL * 60UL * 1000UL, 5UL * 60UL * 1000UL, 15UL * 60UL * 1000UL, 30UL * 60UL * 1000UL,
    };
    constexpr std::array<standby::Kind, 3> kStandbyKinds = {
        standby::Kind::Life,
        standby::Kind::Maze,
        standby::Kind::Voronoi,
    };

    constexpr std::array<size_t, 3> kPhantomBeforeCharTargets = {64, 96, 144};
    constexpr std::array<size_t, 3> kPhantomAfterCharTargets = {96, 144, 208};

    constexpr uint16_t ceilDiv(uint16_t numerator, uint16_t denominator) {
        return denominator == 0 ? 0 : static_cast<uint16_t>((numerator + denominator - 1U) / denominator);
    }

    constexpr uint8_t brightnessPercentFromIndex(uint8_t index) {
        const size_t safeIndex = std::min<size_t>(index, kBrightnessStepCount - 1);
        return static_cast<uint8_t>((safeIndex + 1) * kBrightnessStepPercent);
    }

    String percentLabel(uint8_t value) {
        return String(value) + "%";
    }

    UiRenderer::Button menuButton(const String& label, size_t index, size_t selectedIndex) {
        return {label, index == selectedIndex};
    }

} // namespace

void App::begin() {
    prefs_.begin(settings::kPrefsNamespace, false);
    brightnessIndex_ = settings::load<pref::BrightnessIndex>(prefs_, kBrightnessStepCount);
    standbyIndex_ = settings::load<pref::StandbyTimerIndex>(prefs_, kStandbyMs.size());
    batteryLabelMode_ = settings::load<pref::BatteryLabelMode>(prefs_);
    readerFontSizeIndex_ = settings::load<pref::ReaderFontSizeIndex>(prefs_, ui_.readerFontSizeCount());
    phantomWordsEnabled_ = settings::load<pref::PhantomWords>(prefs_);
    focusHighlightEnabled_ = settings::load<pref::TypographyFocusHighlight>(prefs_);
    trackingPx_ = settings::load<pref::TypographyTracking>(prefs_);
    anchorPercent_ = settings::load<pref::TypographyAnchor>(prefs_);
    guideHalfWidth_ = settings::load<pref::TypographyGuideWidth>(prefs_);
    guideGap_ = settings::load<pref::TypographyGuideGap>(prefs_);
    pauseMode_ = static_cast<PauseMode>(settings::load<pref::PauseMode>(prefs_));
    pacingLongWordDelayMs_ = settings::load<pref::PacingLongWordDelay>(prefs_);
    pacingComplexWordDelayMs_ = settings::load<pref::PacingComplexWordDelay>(prefs_);
    pacingPunctuationDelayMs_ = settings::load<pref::PacingPunctuationDelay>(prefs_);

    storage_.setStatusCallback(&App::renderStorageStatus, this);
    inputReady_ = Input::begin();
    bootMs_ = millis();
    lastActivityMs_ = bootMs_;

    if (!ui_.begin()) {
        Serial.println("[app] display init failed");
    }
    ui_.setBrightness(brightnessPercentFromIndex(brightnessIndex_));
    ui_.setReaderFont(readerTypefaceIndex_, readerFontSizeIndex_);
    applyTypographySettings();
    ui_.renderStatus("READY");

    updateBattery(bootMs_, true);
    storageReady_ = storage_.begin();
    fonts_.loadFromSd();
    ui_.setFontCatalog(&fonts_);
    const String savedTypefaceId = settings::load<pref::ReaderTypefaceId>(prefs_);
    if (savedTypefaceId.isEmpty() || !fonts_.indexForId(savedTypefaceId, readerTypefaceIndex_)) {
        readerTypefaceIndex_ = settings::load<pref::ReaderTypefaceIndex>(prefs_, fonts_.typefaceCount());
    }
    ui_.setReaderFont(readerTypefaceIndex_, readerFontSizeIndex_);
    themes_.loadFromSd();
    const String savedThemeId = settings::load<pref::ThemeId>(prefs_);
    if (!savedThemeId.isEmpty()) {
        themes_.selectById(savedThemeId);
    }
    ui_.setTheme(themes_.selected());

    reader_.setWpm(settings::load<pref::Wpm>(prefs_));
    applyPacingSettings();
    focusTimer_.begin();
    loadBootBook(bootMs_);
    setState(AppState::Booting, bootMs_);
}

void App::update(uint32_t nowMs) {
    Input::Event event;
    while (inputReady_ && Input::poll(event, nowMs)) {
        lastActivityMs_ = nowMs;
        handleInput(event, nowMs);
    }

    if (state_ == AppState::Standby) {
        updateStandby(nowMs);
        return;
    }

    if (state_ == AppState::Booting && nowMs - bootMs_ >= kBootSplashMs) {
        setState(AppState::ReaderPaused, nowMs);
    }

    updateBattery(nowMs);
    updateReader(nowMs);
    updateSync(nowMs);
    updateUsbTransfer(nowMs);
    updateFocusTimer(nowMs);
    saveProgress(false);

    if ((state_ == AppState::ReaderPaused || state_ == AppState::Menu) && kStandbyMs[standbyIndex_] > 0
        && nowMs - lastActivityMs_ >= kStandbyMs[standbyIndex_]) {
        enterStandby(nowMs);
    }
}

void App::setState(AppState state, uint32_t nowMs) {
    if (state_ == state) {
        render();
        return;
    }
    if (state_ == AppState::ReaderPlaying && state != AppState::ReaderPlaying) {
        saveProgress(true);
    }
    state_ = state;
    Serial.printf("[app] state -> %s at %lu\n", stateName(state_), static_cast<unsigned long>(nowMs));
    render();
}

void App::render() {
    switch (state_) {
    case AppState::Booting:
        ui_.renderStatus("READY");
        break;
    case AppState::ReaderPaused:
    case AppState::ReaderPlaying:
        renderReader();
        break;
    case AppState::Menu:
        (void) renderMenu();
        break;
    case AppState::Sync:
        ui_.renderStatus("Sync", sync_.statusLine1(), sync_.statusLine2());
        break;
    case AppState::UsbTransfer:
        ui_.renderStatus("USB", usbTransfer_.statusMessage(), "hold PWR to exit");
        break;
    case AppState::FocusTimer:
        renderFocusTimerSession(millis());
        break;
    case AppState::Ota:
        ui_.renderStatus("OTA", "Checking", "");
        break;
    case AppState::Standby:
        renderStandbyFrame();
        break;
    case AppState::Sleeping:
        ui_.renderStatus("OFF", "Release PWR", "");
        break;
    }
}

void App::renderReader() {
    UiRenderer::ReaderChrome chrome;
    const bool reading = state_ == AppState::ReaderPlaying;
    chrome.showBattery = !reading;
    chrome.showChapter = !reading;
    chrome.showProgress = true;
    chrome.showPreviousSentenceHint = true;
    ui_.renderReader(phantomWordsEnabled_ ? phantomBeforeText() : "", reader_.currentWord(),
                     phantomWordsEnabled_ ? phantomAfterText() : "", currentChapterLabel(), progressPercent(),
                     footerStatusLabel(), true,
                     wpmFeedbackUntilMs_ > millis() ? String(reader_.wpm()) + " WPM" : String(), chrome);
}

int App::renderMenu(UiRenderer::Tap tap) {
    switch (menuScreen_) {
    case MenuScreen::Books:
        return renderBooks(tap);
    case MenuScreen::Chapters:
        return renderChapters(tap);
    case MenuScreen::Settings:
        return renderSettings(tap);
    case MenuScreen::SettingsReading:
        return renderSettingsReading(tap);
    case MenuScreen::SettingsDisplay:
        return renderSettingsDisplay(tap);
    case MenuScreen::SettingsPacing:
        return renderSettingsPacing(tap);
    case MenuScreen::SettingsTypography:
        return renderSettingsTypography(tap);
    case MenuScreen::Device:
        return renderDevice(tap);
    case MenuScreen::SyncOptions:
        return renderSyncOptions(tap);
    case MenuScreen::FocusTimerGenres:
        return renderFocusTimerGenres(tap);
    case MenuScreen::OtaConfirm:
        return renderOtaConfirm(tap);
    case MenuScreen::Main:
    default:
        return ui_.renderMainMenu(bookTitle(), progressPercent(), selected_, tap);
    }
}

int App::renderBooks(UiRenderer::Tap tap) {
    const std::vector<UiRenderer::LibraryItem>& items = libraryItems();
    if (items.empty()) {
        selected_ = 0;
        libraryShelfOffsetPx_ = 0;
    } else if (selected_ >= items.size()) {
        selected_ = items.size() - 1;
    }

    if (!libraryShelfTouchActive_) {
        libraryShelfOffsetPx_ = ui_.libraryShelfOffsetFor(items, selected_);
    } else {
        libraryShelfOffsetPx_ = ui_.libraryShelfClampedOffset(items, libraryShelfOffsetPx_);
    }

    if (libraryChromeDirty_) {
        ui_.renderLibraryChrome();
        libraryChromeDirty_ = false;
    }

    return ui_.renderLibraryShelfAndDetail(items, selected_, libraryShelfOffsetPx_, tap, libraryShelfTouchActive_);
}

void App::invalidateLibraryCache() {
    libraryItemsCache_.clear();
    libraryItemsCacheBookCount_ = 0;
    libraryItemsCacheValid_ = false;
}

String App::librarySpineLabelForTitle(const String& title) const {
    String cleaned = title;
    cleaned.trim();
    String lowered = cleaned;
    lowered.toLowerCase();
    if (lowered.startsWith("the ")) {
        cleaned = cleaned.substring(4);
    } else if (lowered.startsWith("an ")) {
        cleaned = cleaned.substring(3);
    } else if (lowered.startsWith("a ")) {
        cleaned = cleaned.substring(2);
    }

    String out;
    out.reserve(7);
    for (size_t i = 0; i < cleaned.length() && out.length() < 7; ++i) {
        char ch = cleaned[i];
        if (ch >= 'a' && ch <= 'z') {
            ch = static_cast<char>(ch - 'a' + 'A');
        }
        if ((ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9')) {
            out += ch;
        }
    }
    return out.isEmpty() ? String("BOOK") : out;
}


String App::libraryDetailLineFor(const String& author, const String& chapter) const {
    String detail = author.isEmpty() ? String("Unknown") : author;
    if (!chapter.isEmpty()) {
        detail += " · ";
        detail += chapter;
    }
    return detail;
}

String App::libraryProgressLabelFor(uint8_t progressPercent) const {
    if (progressPercent == 0) {
        return "new";
    }
    if (progressPercent >= 100) {
        return "done";
    }
    return String(progressPercent) + "%";
}

const std::vector<UiRenderer::LibraryItem>& App::libraryItems() {
    const size_t bookCount = storage_.bookCount();
    if (libraryItemsCacheValid_ && libraryItemsCacheBookCount_ == bookCount) {
        if (usingStorageBook_ && currentBook_ < libraryItemsCache_.size() && bookStore_.isOpen()) {
            UiRenderer::LibraryItem& current = libraryItemsCache_[currentBook_];
            current.progressPercent = progressPercent();
            current.chapter = chapterLabelForMetadata(metadata_, reader_.currentIndex(), current.title);
            current.detailLine = libraryDetailLineFor(current.author, current.chapter);
            current.progressLabel = libraryProgressLabelFor(current.progressPercent);
        }
        return libraryItemsCache_;
    }

    libraryItemsCache_.clear();
    libraryItemsCache_.reserve(bookCount);

    for (size_t i = 0; i < bookCount; ++i) {
        UiRenderer::LibraryItem item;
        item.title = storage_.bookDisplayName(i);
        item.author = storage_.bookAuthorName(i);
        item.article = storage_.bookIsArticle(i);
        item.spineLabel = librarySpineLabelForTitle(item.title);

        const String path = storage_.bookPath(i);
        BookMetadata itemMetadata;
        IndexedBookStore::Header header;
        const bool metadataLoaded = IndexedBook::readMetadata(path, itemMetadata, &header);

        uint32_t wordIndex = 0;
        uint32_t wordCount = 0;
        bool hasWordPosition = false;

        if (usingStorageBook_ && i == currentBook_ && bookStore_.isOpen()) {
            wordIndex = static_cast<uint32_t>(reader_.currentIndex());
            wordCount = static_cast<uint32_t>(reader_.wordCount());
            hasWordPosition = wordCount > 0;
            item.progressPercent = progressPercent();
            item.chapter = chapterLabelForMetadata(metadata_, wordIndex, item.title);
        } else if (metadataLoaded && header.wordCount > 0) {
            wordCount = header.wordCount;

            ReadingProgress::BookIdentity identity;
            identity.sourceSize = header.sourceSize;
            identity.sourceFingerprint = header.sourceFingerprint;
            identity.wordCount = header.wordCount;

            if (ReadingProgress::readPositionSidecar(path, identity, wordIndex)) {
                hasWordPosition = true;
            } else {
                const String positionKey = bookPositionKey(path);
                if (prefs_.isKey(positionKey.c_str())) {
                    const String countKey = bookWordCountKey(path);
                    const String sizeKey = bookSourceSizeKey(path);
                    const String fingerprintKey = bookSourceFingerprintKey(path);
                    const bool countMatches =
                        !prefs_.isKey(countKey.c_str()) || prefs_.getUInt(countKey.c_str(), 0) == header.wordCount;
                    const bool sourceMatches =
                        !prefs_.isKey(sizeKey.c_str()) || !prefs_.isKey(fingerprintKey.c_str())
                        || (prefs_.getUInt(sizeKey.c_str(), 0) == header.sourceSize
                            && prefs_.getUInt(fingerprintKey.c_str(), 0) == header.sourceFingerprint);

                    if (countMatches && sourceMatches) {
                        wordIndex = prefs_.getUInt(positionKey.c_str(), 0);
                        wordIndex = std::min<uint32_t>(wordIndex, header.wordCount - 1);
                        hasWordPosition = true;
                    }
                }
            }

            item.progressPercent = hasWordPosition && header.wordCount > 1
                ? static_cast<uint8_t>(
                    std::clamp<uint32_t>((wordIndex * 100UL) / (header.wordCount - 1), uint32_t{0}, uint32_t{100}))
                : uint8_t{0};
            item.chapter = chapterLabelForMetadata(itemMetadata, hasWordPosition ? wordIndex : 0, item.title);
        } else {
            item.progressPercent = storedProgressPercentForBook(i);
        }

        item.detailLine = libraryDetailLineFor(item.author, item.chapter);
        item.progressLabel = libraryProgressLabelFor(item.progressPercent);
        libraryItemsCache_.push_back(item);
    }

    libraryItemsCacheBookCount_ = bookCount;
    libraryItemsCacheValid_ = true;
    return libraryItemsCache_;
}

int App::libraryBookIndexForRow(size_t row) const {
    return row < storage_.bookCount() ? static_cast<int>(row) : -1;
}

uint8_t App::storedProgressPercentForBook(size_t index) {
    if (usingStorageBook_ && index == currentBook_ && bookStore_.isOpen()) {
        return progressPercent();
    }

    const String path = storage_.bookPath(index);
    const String positionKey = bookPositionKey(path);
    const String countKey = bookWordCountKey(path);
    if (!prefs_.isKey(positionKey.c_str()) || !prefs_.isKey(countKey.c_str())) {
        return 0;
    }

    const uint32_t wordIndex = prefs_.getUInt(positionKey.c_str(), 0);
    const uint32_t wordCount = prefs_.getUInt(countKey.c_str(), 0);
    if (wordCount <= 1) {
        return 0;
    }

    return static_cast<uint8_t>(
        std::clamp<uint32_t>((wordIndex * 100UL) / (wordCount - 1), uint32_t{0}, uint32_t{100}));
}

int App::renderChapters(UiRenderer::Tap tap) {
    std::vector<UiRenderer::Button> rows;
    rows.push_back(menuButton("Back", 0, selected_));
    for (const ChapterMarker& chapter: metadata_.chapters) {
        rows.push_back(menuButton(chapter.title.isEmpty() ? String("Chapter") : chapter.title, rows.size(), selected_));
    }
    if (rows.size() == 1) {
        rows.push_back(menuButton("Start", 1, selected_));
    }
    return ui_.renderMenu("Chapters", rows, tap);
}

int App::renderSettings(UiRenderer::Tap tap) {
    return ui_.renderSettingsHub(selected_, tap);
}

int App::renderSettingsReading(UiRenderer::Tap tap) {
    std::vector<UiRenderer::Button> rows = {
        menuButton("Back", 0, selected_),
        menuButton("WPM: " + String(reader_.wpm()), 1, selected_),
        menuButton("Pause: " + pauseModeLabel(), 2, selected_),
        menuButton("Phantom Words: " + onOffLabel(phantomWordsEnabled_), 3, selected_),
    };
    return ui_.renderMenu("Reading", rows, tap);
}

int App::renderSettingsDisplay(UiRenderer::Tap tap) {
    std::vector<UiRenderer::Button> rows = {
        menuButton("Back", 0, selected_),
        menuButton("Brightness: " + percentLabel(brightnessPercentFromIndex(brightnessIndex_)), 1, selected_),
        menuButton("Theme: " + themes_.selected().name, 2, selected_),
        menuButton("Standby: "
                       + (standbyIndex_ == 0 ? String("Off") : String(kStandbyMs[standbyIndex_] / 60000UL) + "m"),
                   3, selected_),
    };
    return ui_.renderMenu("Display", rows, tap);
}

int App::renderSettingsPacing(UiRenderer::Tap tap) {
    std::vector<UiRenderer::Button> rows = {
        menuButton("Back", 0, selected_),
        menuButton("Long Words: " + pacingDelayLabel(pacingLongWordDelayMs_), 1, selected_),
        menuButton("Complexity: " + pacingDelayLabel(pacingComplexWordDelayMs_), 2, selected_),
        menuButton("Punctuation: " + pacingDelayLabel(pacingPunctuationDelayMs_), 3, selected_),
    };
    return ui_.renderMenu("Pacing", rows, tap);
}

int App::renderSettingsTypography(UiRenderer::Tap tap) {
    std::vector<UiRenderer::Button> rows = {
        menuButton("Back", 0, selected_),
        menuButton("Reader Size: " + String(ui_.readerFontSizeLabel(readerFontSizeIndex_)), 1, selected_),
        menuButton("Typeface: " + String(ui_.readerTypefaceLabel(readerTypefaceIndex_)), 2, selected_),
        menuButton("Focus Letter: " + onOffLabel(focusHighlightEnabled_), 3, selected_),
        menuButton("Spacing: " + String(static_cast<int>(trackingPx_)) + "px", 4, selected_),
        menuButton("Anchor: " + String(anchorPercent_) + "%", 5, selected_),
        menuButton("Guide Width: " + String(guideHalfWidth_), 6, selected_),
        menuButton("Guide Gap: " + String(guideGap_), 7, selected_),
        menuButton("Reset Typography", 8, selected_),
    };
    return ui_.renderMenu("Typography / Aa", rows, tap);
}

int App::renderDevice(UiRenderer::Tap tap) {
    return ui_.renderDeviceHub(selected_, tap);
}

int App::renderSyncOptions(UiRenderer::Tap tap) {
    std::vector<UiRenderer::Button> rows = {
        menuButton("Back", 0, selected_),
        menuButton("Companion Sync", 1, selected_),
        menuButton("Refresh RSS", 2, selected_),
        menuButton("USB Transfer", 3, selected_),
    };
    return ui_.renderMenu("Sync / Import", rows, tap);
}

int App::renderOtaConfirm(UiRenderer::Tap tap) {
    std::vector<UiRenderer::Button> rows = {
        menuButton("Back", 0, selected_),
        menuButton("Check only", 1, selected_),
        menuButton("Install update", 2, selected_),
    };
    return ui_.renderMenu("OTA", rows, tap);
}

int App::renderFocusTimerGenres(UiRenderer::Tap tap) {
    return ui_.renderFocusHub(selected_, tap);
}

void App::renderFocusTimerSession(uint32_t nowMs) {
    if (!focusTimer_.available()) {
        ui_.renderStatus("Focus Timer", "IMU unavailable", "");
        return;
    }
    const uint32_t remainingMs = focusTimer_.remainingMs(nowMs);
    const uint32_t seconds = remainingMs / 1000UL;
    char timeLabel[8];
    std::snprintf(timeLabel, sizeof(timeLabel), "%02lu:%02lu", static_cast<unsigned long>(seconds / 60UL),
                  static_cast<unsigned long>(seconds % 60UL));
    ui_.renderProgress(FocusTimer::genreLabel(focusTimer_.genre()), focusTimer_.isActiveTimerRunning() ? timeLabel : "",
                       "tap or press to exit", {focusTimer_.progressPercent(nowMs)});
}

void App::handleInput(const Input::Event& event, uint32_t nowMs) {
    if (state_ == AppState::Standby) {
        exitStandby(nowMs);
        return;
    }
    if (state_ == AppState::UsbTransfer && Input::hasAction(event.actions, Input::ActionPowerOff)) {
        exitUsbTransfer(nowMs);
        return;
    }
    if (state_ == AppState::FocusTimer
        && (Input::hasAction(event.actions, Input::ActionTap) || Input::hasAction(event.actions, Input::ActionBack)
            || Input::hasAction(event.actions, Input::ActionSelect))) {
        focusTimer_.abandon();
        setState(AppState::ReaderPaused, nowMs);
        return;
    }
    if (Input::hasAction(event.actions, Input::ActionPowerOff)) {
        powerOff(nowMs);
        return;
    }
    if (Input::hasAction(event.actions, Input::ActionStandby)) {
        enterStandby(nowMs);
        return;
    }
    if (Input::isTouchEvent(event)) {
        handleTouch(event, nowMs);
        return;
    }
    if (Input::hasAction(event.actions, Input::ActionOpenMenu) || Input::hasAction(event.actions, Input::ActionBack)) {
        if (state_ == AppState::Menu) {
            back(nowMs);
        } else {
            openMenu();
            setState(AppState::Menu, nowMs);
        }
        return;
    }
    if (state_ == AppState::Menu && Input::hasAction(event.actions, Input::ActionUp)) {
        moveSelection(-1);
        renderMenu();
        return;
    }
    if (state_ == AppState::Menu && Input::hasAction(event.actions, Input::ActionDown)) {
        moveSelection(1);
        renderMenu();
        return;
    }
    if (Input::hasAction(event.actions, Input::ActionSelect)
        || Input::hasAction(event.actions, Input::ActionPlayPause)) {
        if (state_ == AppState::Menu) {
            select(nowMs);
        } else if (state_ == AppState::ReaderPaused || state_ == AppState::ReaderPlaying) {
            toggleReaderShortcut(nowMs);
        }
    } else if (state_ == AppState::ReaderPaused || state_ == AppState::ReaderPlaying) {
        if (Input::hasAction(event.actions, Input::ActionDown)) {
            toggleReaderShortcut(nowMs);
        }
    }
}

void App::handleTouch(const Input::Event& event, uint32_t nowMs) {
    const UiRenderer::Tap tap{Input::hasAction(event.actions, Input::ActionTap), event.x, event.y};
    if (state_ == AppState::Menu) {
        if (menuScreen_ == MenuScreen::Books) {
            handleLibraryTouch(event, nowMs);
            return;
        }

        if (!tap.active) {
            return;
        }
        const int row = renderMenu(tap);
        if (row >= 0) {
            switch (row) {
            case UiRenderer::kMenuNavRead:
                selected_ = 0;
                menuScreen_ = MenuScreen::Main;
                renderMenu();
                return;
            case UiRenderer::kMenuNavSettings:
                selected_ = 0;
                menuScreen_ = MenuScreen::Settings;
                renderMenu();
                return;
            case UiRenderer::kMenuNavDevice:
                selected_ = 0;
                menuScreen_ = MenuScreen::Device;
                renderMenu();
                return;
            case UiRenderer::kMenuNavFocus:
                openFocusTimer();
                return;
            case UiRenderer::kMenuNavPower:
                powerOff(nowMs);
                return;
            default:
                selected_ = static_cast<size_t>(row);
                select(nowMs);
                return;
            }
        }
        return;
    }

    if (state_ != AppState::ReaderPaused && state_ != AppState::ReaderPlaying) {
        return;
    }
    handleReaderTouch(event, nowMs);
}

void App::handleLibraryTouch(const Input::Event& event, uint32_t nowMs) {
    const UiRenderer::Tap tap{Input::hasAction(event.actions, Input::ActionTap), event.x, event.y};
    const std::vector<UiRenderer::LibraryItem>& items = libraryItems();

    if (Input::hasAction(event.actions, Input::ActionTouchStart)) {
        libraryShelfTouchActive_ = ui_.hitLibraryShelf({true, event.x, event.y});
        libraryShelfMoved_ = false;
        libraryTouchStartX_ = event.x;
        libraryTouchStartY_ = event.y;
        libraryTouchStartOffsetPx_ = libraryShelfOffsetPx_;
        lastLibraryDragRenderMs_ = 0;
        return;
    }

    if (libraryShelfTouchActive_ && Input::hasAction(event.actions, Input::ActionTouchMove)) {
        const int deltaX = static_cast<int>(event.x) - static_cast<int>(libraryTouchStartX_);
        const int deltaY = static_cast<int>(event.y) - static_cast<int>(libraryTouchStartY_);
        if (std::abs(deltaX) > 5 || std::abs(deltaY) > 5) {
            libraryShelfMoved_ = true;
        }
        libraryShelfOffsetPx_ = ui_.libraryShelfClampedOffset(
            items, static_cast<int16_t>(libraryTouchStartOffsetPx_ + deltaX));
        constexpr uint32_t kLibraryDragRenderIntervalMs = 33;
        if (lastLibraryDragRenderMs_ == 0 || nowMs - lastLibraryDragRenderMs_ >= kLibraryDragRenderIntervalMs) {
            lastLibraryDragRenderMs_ = nowMs;
            renderMenu();
        }
        return;
    }

    if (libraryShelfTouchActive_ && Input::hasAction(event.actions, Input::ActionTouchRelease)) {
        const bool wasDrag = libraryShelfMoved_ || !tap.active;
        libraryShelfTouchActive_ = false;
        if (wasDrag && !items.empty()) {
            centerLibraryShelfOn(ui_.libraryShelfNearestIndex(items, libraryShelfOffsetPx_, 94 + (498 / 2)), true);
            renderMenu();
            return;
        }
    }

    if (!tap.active) {
        return;
    }

    const int row = renderMenu(tap);
    if (row < 0) {
        return;
    }

    switch (row) {
    case UiRenderer::kMenuNavRead:
        selected_ = 0;
        menuScreen_ = MenuScreen::Main;
        renderMenu();
        return;
    case UiRenderer::kMenuNavSettings:
        selected_ = 0;
        menuScreen_ = MenuScreen::Settings;
        renderMenu();
        return;
    case UiRenderer::kMenuNavDevice:
        selected_ = 0;
        menuScreen_ = MenuScreen::Device;
        renderMenu();
        return;
    case UiRenderer::kMenuNavFocus:
        openFocusTimer();
        return;
    case UiRenderer::kMenuNavPower:
        powerOff(nowMs);
        return;
    case UiRenderer::kLibraryOpenSelected:
        openSelectedLibraryItem(nowMs);
        return;
    default:
        if (row >= UiRenderer::kLibraryShelfItemBase) {
            const size_t rowIndex = static_cast<size_t>(row - UiRenderer::kLibraryShelfItemBase);
            if (rowIndex >= items.size()) {
                return;
            }
            if (rowIndex == selected_) {
                openSelectedLibraryItem(nowMs);
                return;
            }
            centerLibraryShelfOn(rowIndex, true);
            renderMenu();
            return;
        }
        selected_ = static_cast<size_t>(row);
        select(nowMs);
        return;
    }
}

void App::handleReaderTouch(const Input::Event& event, uint32_t nowMs) {
    const bool ended = Input::hasAction(event.actions, Input::ActionTouchRelease);
    const bool held = Input::hasAction(event.actions, Input::ActionTouchHold);
    if (ended && readerTouchIntent_ == TouchIntent::PlayHold) {
        readerTouch_ = {};
        readerTouchIntent_ = TouchIntent::None;
        requestReaderPause(nowMs);
        return;
    }

    if (Input::hasAction(event.actions, Input::ActionTouchStart)) {
        readerTouch_.active = true;
        readerTouch_.startX = event.x;
        readerTouch_.startY = event.y;
        readerTouch_.lastX = event.x;
        readerTouch_.lastY = event.y;
        readerTouch_.startMs = nowMs;
        readerTouch_.startWordIndex = reader_.currentIndex();
        readerTouch_.scrubStepsApplied = 0;
        readerTouchIntent_ = TouchIntent::None;
        return;
    }

    if (!readerTouch_.active) {
        return;
    }

    readerTouch_.lastX = event.x;
    readerTouch_.lastY = event.y;
    const int deltaX = static_cast<int>(readerTouch_.lastX) - static_cast<int>(readerTouch_.startX);
    const int deltaY = static_cast<int>(readerTouch_.lastY) - static_cast<int>(readerTouch_.startY);
    const int absDeltaX = abs(deltaX);
    const int absDeltaY = abs(deltaY);
    const bool tapLike = absDeltaX <= static_cast<int>(kTapSlopPx) && absDeltaY <= static_cast<int>(kTapSlopPx);

    if (state_ == AppState::ReaderPlaying) {
        if (held && tapLike && !playLocked_) {
            readerTouch_ = {};
            readerTouchIntent_ = TouchIntent::None;
            requestReaderPause(nowMs);
            return;
        }

        if (ended) {
            readerTouch_ = {};
            readerTouchIntent_ = TouchIntent::None;
            if (!tapLike) {
                resetReaderTapTracking();
                return;
            }
            if (handleBatteryTap(event.x, event.y, nowMs)) {
                return;
            }
            if (handlePreviousSentenceTap(event.x, event.y, nowMs)) {
                return;
            }
            if (playLocked_ || pauseAtSentenceEndRequested_) {
                resetReaderTapTracking();
                requestReaderPause(nowMs);
            } else {
                handleReaderTap(event.x, event.y, nowMs);
            }
        }
        return;
    }

    if (readerTouchIntent_ == TouchIntent::None && !ended && held && tapLike) {
        resetReaderTapTracking();
        readerTouchIntent_ = TouchIntent::PlayHold;
        startReader(nowMs, false);
        return;
    }

    if (readerTouchIntent_ == TouchIntent::None) {
        if (absDeltaX >= static_cast<int>(kSwipeThresholdPx) && absDeltaX > absDeltaY + static_cast<int>(kAxisBiasPx)) {
            resetReaderTapTracking();
            readerTouchIntent_ = TouchIntent::Scrub;
        } else if (absDeltaY >= static_cast<int>(kSwipeThresholdPx)
                   && absDeltaY > absDeltaX + static_cast<int>(kAxisBiasPx)) {
            resetReaderTapTracking();
            readerTouchIntent_ = TouchIntent::Wpm;
        }
    }

    if (readerTouchIntent_ == TouchIntent::Scrub) {
        applyScrubTarget(scrubStepsForDrag(deltaX), nowMs);
        if (ended) {
            readerTouch_ = {};
            readerTouchIntent_ = TouchIntent::None;
            saveProgress(true);
        }
        return;
    }

    if (readerTouchIntent_ == TouchIntent::Wpm) {
        if (ended) {
            adjustWpm(deltaY < 0 ? 1 : -1);
            wpmFeedbackUntilMs_ = nowMs + kWpmFeedbackMs;
            renderReader();
            readerTouch_ = {};
            readerTouchIntent_ = TouchIntent::None;
        }
        return;
    }

    if (ended) {
        readerTouch_ = {};
        readerTouchIntent_ = TouchIntent::None;
        if (tapLike && handleBatteryTap(event.x, event.y, nowMs)) {
            return;
        }
        if (tapLike && handlePreviousSentenceTap(event.x, event.y, nowMs)) {
            return;
        }
        if (tapLike) {
            handleReaderTap(event.x, event.y, nowMs);
        } else {
            resetReaderTapTracking();
        }
    }
}

void App::handleReaderTap(uint16_t x, uint16_t y, uint32_t nowMs) {
    const bool recentTap = lastReaderTapValid_ && nowMs - lastReaderTapMs_ <= kReaderDoubleTapWindowMs;
    const bool sameRegion = recentTap
                         && abs(static_cast<int>(x) - static_cast<int>(lastReaderTapX_)) <= kReaderDoubleTapSlopPx
                         && abs(static_cast<int>(y) - static_cast<int>(lastReaderTapY_)) <= kReaderDoubleTapSlopPx;

    if (sameRegion) {
        resetReaderTapTracking();
        toggleReaderShortcut(nowMs);
        return;
    }

    lastReaderTapValid_ = true;
    lastReaderTapMs_ = nowMs;
    lastReaderTapX_ = x;
    lastReaderTapY_ = y;
}

bool App::handleBatteryTap(uint16_t x, uint16_t y, uint32_t nowMs) {
    if (state_ == AppState::ReaderPlaying) {
        return false;
    }

    const UiRenderer::Tap tap{true, x, y};
    if (!ui_.hitBattery(tap)) {
        return false;
    }

    batteryLabelMode_ = static_cast<uint8_t>((batteryLabelMode_ + 1U) & 1U);
    settings::save<pref::BatteryLabelMode>(prefs_, batteryLabelMode_);
    ui_.setBatteryStatus(batteryPercent_, batteryVoltage_, batteryCharging_, batteryShowVoltage());
    resetReaderTapTracking();
    renderReader();
    (void) nowMs;
    return true;
}

bool App::handlePreviousSentenceTap(uint16_t x, uint16_t y, uint32_t nowMs) {
    if (x > kPreviousSentenceTapWidthPx) {
        return false;
    }
    resetReaderTapTracking();
    reader_.rewindSentence();
    pauseAtSentenceEndRequested_ = false;
    playLocked_ = false;
    if (state_ == AppState::ReaderPlaying) {
        setState(AppState::ReaderPaused, nowMs);
    } else {
        renderReader();
        saveProgress(true);
    }
    return true;
}

void App::openMenu() {
    saveProgress(true);
    invalidateLibraryCache();
    selected_ = 0;
    menuScreen_ = MenuScreen::Main;
}

void App::closeMenu(uint32_t nowMs) {
    setState(AppState::ReaderPaused, nowMs);
}

void App::back(uint32_t nowMs) {
    if (menuScreen_ == MenuScreen::Main) {
        closeMenu(nowMs);
        return;
    }

    selected_ = 0;
    switch (menuScreen_) {
    case MenuScreen::SyncOptions:
        menuScreen_ = MenuScreen::Device;
        break;
    case MenuScreen::SettingsReading:
    case MenuScreen::SettingsDisplay:
    case MenuScreen::SettingsPacing:
    case MenuScreen::SettingsTypography:
        menuScreen_ = MenuScreen::Settings;
        break;
    case MenuScreen::OtaConfirm:
        menuScreen_ = MenuScreen::Device;
        break;
    default:
        menuScreen_ = MenuScreen::Main;
        break;
    }
    renderMenu();
}

void App::moveSelection(int delta) {
    const size_t count = menuRowCount();
    if (count == 0) {
        selected_ = 0;
        return;
    }
    selected_ =
        static_cast<size_t>((static_cast<int>(selected_) + delta + static_cast<int>(count)) % static_cast<int>(count));
}

size_t App::menuRowCount() const {
    switch (menuScreen_) {
    case MenuScreen::Books:
        return storage_.bookCount();
    case MenuScreen::Chapters:
        return std::max<size_t>(2, metadata_.chapters.size() + 1);
    case MenuScreen::Settings:
        return static_cast<size_t>(SettingsItem::Count);
    case MenuScreen::SettingsReading:
        return static_cast<size_t>(SettingsReadingItem::Count);
    case MenuScreen::SettingsDisplay:
        return static_cast<size_t>(SettingsDisplayItem::Count);
    case MenuScreen::SettingsPacing:
        return static_cast<size_t>(SettingsPacingItem::Count);
    case MenuScreen::SettingsTypography:
        return static_cast<size_t>(SettingsTypographyItem::Count);
    case MenuScreen::Device:
        return static_cast<size_t>(DeviceItem::Count);
    case MenuScreen::SyncOptions:
        return static_cast<size_t>(SyncItem::Count);
    case MenuScreen::FocusTimerGenres:
        return 4;
    case MenuScreen::OtaConfirm:
        return 3;
    case MenuScreen::Main:
    default:
        return static_cast<size_t>(MainItem::Count);
    }
}

void App::select(uint32_t nowMs) {
    switch (menuScreen_) {
    case MenuScreen::Main:
        selectMain(nowMs);
        break;
    case MenuScreen::Settings:
        selectSettings(nowMs);
        break;
    case MenuScreen::SettingsReading:
        selectSettingsReading(nowMs);
        break;
    case MenuScreen::SettingsDisplay:
        selectSettingsDisplay(nowMs);
        break;
    case MenuScreen::SettingsPacing:
        selectSettingsPacing(nowMs);
        break;
    case MenuScreen::SettingsTypography:
        selectSettingsTypography(nowMs);
        break;
    case MenuScreen::Device:
        selectDevice(nowMs);
        break;
    case MenuScreen::SyncOptions:
        selectSyncOptions(nowMs);
        break;
    case MenuScreen::FocusTimerGenres:
        selectFocusTimerGenre(nowMs);
        break;
    case MenuScreen::Books:
        selectBook(selected_, nowMs);
        break;
    case MenuScreen::Chapters:
        selectChapter(selected_, nowMs);
        break;
    case MenuScreen::OtaConfirm:
        if (selected_ == 0) {
            back(nowMs);
        } else {
            runOtaCheck(nowMs, selected_ == 2);
        }
        break;
    }
}

void App::selectMain(uint32_t nowMs) {
    switch (static_cast<MainItem>(selected_)) {
    case MainItem::Resume:
        closeMenu(nowMs);
        break;
    case MainItem::Chapters:
        selected_ = 0;
        menuScreen_ = MenuScreen::Chapters;
        renderMenu();
        break;
    case MainItem::Library:
        selected_ = 0;
        libraryShelfTouchActive_ = false;
        libraryShelfOffsetPx_ = 0;
        libraryChromeDirty_ = true;
        menuScreen_ = MenuScreen::Books;
        renderMenu();
        break;
    case MainItem::Settings:
        selected_ = 0;
        menuScreen_ = MenuScreen::Settings;
        renderMenu();
        break;
    case MainItem::Device:
        selected_ = 0;
        menuScreen_ = MenuScreen::Device;
        renderMenu();
        break;
    case MainItem::FocusTimer:
        openFocusTimer();
        break;
    case MainItem::PowerOff:
        powerOff(nowMs);
        break;
    default:
        break;
    }
}

void App::selectSyncOptions(uint32_t nowMs) {
    switch (static_cast<SyncItem>(selected_)) {
    case SyncItem::Back:
        back(nowMs);
        break;
    case SyncItem::CompanionSync:
        setState(AppState::Sync, nowMs);
        sync_.begin({configuredWifiSsid(), configuredWifiPassword()});
        render();
        break;
    case SyncItem::RssRefresh:
        runRss(nowMs);
        break;
    case SyncItem::UsbTransfer:
        enterUsbTransfer(nowMs);
        break;
    default:
        break;
    }
}

void App::selectSettings(uint32_t nowMs) {
    switch (static_cast<SettingsItem>(selected_)) {
    case SettingsItem::Reading:
        selected_ = 0;
        menuScreen_ = MenuScreen::SettingsReading;
        renderMenu();
        break;
    case SettingsItem::Display:
        selected_ = 0;
        menuScreen_ = MenuScreen::SettingsDisplay;
        renderMenu();
        break;
    case SettingsItem::Pacing:
        selected_ = 0;
        menuScreen_ = MenuScreen::SettingsPacing;
        renderMenu();
        break;
    case SettingsItem::Typography:
        selected_ = 0;
        menuScreen_ = MenuScreen::SettingsTypography;
        renderMenu();
        break;
    default:
        break;
    }
    (void) nowMs;
}

void App::selectSettingsReading(uint32_t nowMs) {
    switch (static_cast<SettingsReadingItem>(selected_)) {
    case SettingsReadingItem::Back:
        back(nowMs);
        break;
    case SettingsReadingItem::Wpm:
        adjustWpm(1);
        renderMenu();
        break;
    case SettingsReadingItem::PauseMode:
        pauseMode_ = pauseMode_ == PauseMode::SentenceEnd ? PauseMode::Instant : PauseMode::SentenceEnd;
        settings::save<pref::PauseMode>(prefs_, static_cast<uint8_t>(pauseMode_));
        renderMenu();
        break;
    case SettingsReadingItem::PhantomWords:
        phantomWordsEnabled_ = settings::toggle<pref::PhantomWords>(prefs_);
        renderMenu();
        break;
    default:
        break;
    }
}

void App::selectSettingsDisplay(uint32_t nowMs) {
    switch (static_cast<SettingsDisplayItem>(selected_)) {
    case SettingsDisplayItem::Back:
        back(nowMs);
        break;
    case SettingsDisplayItem::Brightness:
        brightnessIndex_ = settings::cycle<pref::BrightnessIndex>(prefs_, kBrightnessStepCount);
        ui_.setBrightness(brightnessPercentFromIndex(brightnessIndex_));
        renderMenu();
        break;
    case SettingsDisplayItem::Theme:
        themes_.selectNext();
        settings::save<pref::ThemeId>(prefs_, themes_.selected().id);
        ui_.setTheme(themes_.selected());
        renderMenu();
        break;
    case SettingsDisplayItem::Standby:
        standbyIndex_ = settings::cycle<pref::StandbyTimerIndex>(prefs_, (kStandbyMs.size()));
        renderMenu();
        break;
    default:
        break;
    }
}

void App::selectSettingsPacing(uint32_t nowMs) {
    switch (static_cast<SettingsPacingItem>(selected_)) {
    case SettingsPacingItem::Back:
        back(nowMs);
        break;
    case SettingsPacingItem::LongWords:
        pacingLongWordDelayMs_ = settings::cycle<pref::PacingLongWordDelay>(prefs_);
        applyPacingSettings();
        renderMenu();
        break;
    case SettingsPacingItem::Complexity:
        pacingComplexWordDelayMs_ = settings::cycle<pref::PacingComplexWordDelay>(prefs_);
        applyPacingSettings();
        renderMenu();
        break;
    case SettingsPacingItem::Punctuation:
        pacingPunctuationDelayMs_ = settings::cycle<pref::PacingPunctuationDelay>(prefs_);
        applyPacingSettings();
        renderMenu();
        break;
    default:
        break;
    }
}

void App::selectSettingsTypography(uint32_t nowMs) {
    switch (static_cast<SettingsTypographyItem>(selected_)) {
    case SettingsTypographyItem::Back:
        back(nowMs);
        break;
    case SettingsTypographyItem::ReaderFontSize:
        readerFontSizeIndex_ = settings::cycle<pref::ReaderFontSizeIndex>(prefs_, ui_.readerFontSizeCount());
        ui_.setReaderFont(readerTypefaceIndex_, readerFontSizeIndex_);
        renderMenu();
        break;
    case SettingsTypographyItem::ReaderTypeface:
        readerTypefaceIndex_ = settings::cycle<pref::ReaderTypefaceIndex>(prefs_, ui_.readerTypefaceCount());
        settings::save<pref::ReaderTypefaceId>(prefs_, fonts_.typefaceId(readerTypefaceIndex_));
        ui_.setReaderFont(readerTypefaceIndex_, readerFontSizeIndex_);
        renderMenu();
        break;
    case SettingsTypographyItem::FocusHighlight:
        focusHighlightEnabled_ = settings::toggle<pref::TypographyFocusHighlight>(prefs_);
        applyTypographySettings();
        renderMenu();
        break;
    case SettingsTypographyItem::Tracking:
        trackingPx_ = settings::cycle<pref::TypographyTracking>(prefs_);
        applyTypographySettings();
        renderMenu();
        break;
    case SettingsTypographyItem::Anchor:
        anchorPercent_ = settings::cycle<pref::TypographyAnchor>(prefs_);
        applyTypographySettings();
        renderMenu();
        break;
    case SettingsTypographyItem::GuideWidth:
        guideHalfWidth_ = settings::cycle<pref::TypographyGuideWidth>(prefs_);
        applyTypographySettings();
        renderMenu();
        break;
    case SettingsTypographyItem::GuideGap:
        guideGap_ = settings::cycle<pref::TypographyGuideGap>(prefs_);
        applyTypographySettings();
        renderMenu();
        break;
    case SettingsTypographyItem::ResetTypography:
        resetTypographySettings();
        renderMenu();
        break;
    default:
        break;
    }
}

void App::selectDevice(uint32_t nowMs) {
    switch (static_cast<DeviceItem>(selected_)) {
    case DeviceItem::StorageStatus:
        ui_.renderStatus("Storage", storageReady_ ? "SD ready" : "SD unavailable",
                         String(static_cast<unsigned int>(storage_.bookCount())) + " library entries");
        delay(1200);
        renderMenu();
        break;
    case DeviceItem::SyncImport:
        selected_ = 0;
        menuScreen_ = MenuScreen::SyncOptions;
        renderMenu();
        break;
    case DeviceItem::Ota:
        selected_ = 0;
        menuScreen_ = MenuScreen::OtaConfirm;
        renderMenu();
        break;
    default:
        break;
    }
    (void) nowMs;
}

void App::selectBook(size_t row, uint32_t nowMs) {
    selected_ = row;
    openSelectedLibraryItem(nowMs);
}

void App::centerLibraryShelfOn(size_t row, bool animate) {
    const std::vector<UiRenderer::LibraryItem>& items = libraryItems();
    if (items.empty()) {
        selected_ = 0;
        libraryShelfOffsetPx_ = 0;
        libraryShelfTouchActive_ = false;
        return;
    }

    selected_ = std::min(row, items.size() - 1);
    const int16_t targetOffset = ui_.libraryShelfOffsetFor(items, selected_);
    if (!animate) {
        libraryShelfOffsetPx_ = targetOffset;
        libraryShelfTouchActive_ = false;
        return;
    }

    const int16_t startOffset = libraryShelfOffsetPx_;
    libraryShelfTouchActive_ = true;
    for (uint8_t step = 1; step <= 4; ++step) {
        libraryShelfOffsetPx_ = static_cast<int16_t>(startOffset + ((targetOffset - startOffset) * step) / 4);
        renderMenu();
        delay(14);
    }
    libraryShelfOffsetPx_ = targetOffset;
    libraryShelfTouchActive_ = false;
}

bool App::openSelectedLibraryItem(uint32_t nowMs) {
    const int bookIndex = libraryBookIndexForRow(selected_);
    if (bookIndex < 0) {
        renderMenu();
        return false;
    }
    if (loadBook(static_cast<size_t>(bookIndex), nowMs)) {
        closeMenu(nowMs);
        return true;
    }
    return false;
}

void App::selectChapter(size_t row, uint32_t nowMs) {
    if (row == 0) {
        back(nowMs);
        return;
    }
    if (metadata_.chapters.empty()) {
        reader_.seekTo(0);
    } else {
        const size_t chapterIndex = std::clamp<size_t>(row - 1, size_t{0}, metadata_.chapters.size() - 1);
        reader_.seekTo(metadata_.chapters[chapterIndex].wordIndex);
    }
    closeMenu(nowMs);
}

bool App::loadBook(size_t index, uint32_t nowMs) {
    if (!storageReady_ || index >= storage_.bookCount()) {
        return false;
    }
    ui_.renderProgress("Opening book", storage_.bookDisplayName(index), "", UiRenderer::Slider{5});
    bookStore_.close();
    metadata_.clear();
    StorageManager::IndexedBookLoadOptions options;
    String loadedPath;
    size_t loadedIndex = index;
    options.loadedPath = &loadedPath;
    options.loadedIndex = &loadedIndex;
    if (!storage_.loadIndexedBook(index, bookStore_, metadata_, options)) {
        ui_.renderStatus("Book failed", storage_.bookDisplayName(index), "Check SD card");
        delay(1200);
        renderMenu();
        return false;
    }
    currentBook_ = loadedIndex;
    currentBookPath_ = loadedPath;
    usingStorageBook_ = true;
    lastSavedWordIndex_ = static_cast<size_t>(-1);
    reader_.setWordSource(&bookStore_, nowMs);

    const uint32_t savedWord = restoredWordIndexForBook();
    if (savedWord != kNoSavedWordIndex) {
        reader_.seekTo(savedWord);
        cacheProgress(static_cast<uint32_t>(reader_.currentIndex()));
    } else {
        settings::save<pref::BookPath>(prefs_, currentBookPath_);
        prefs_.putUInt(bookWordCountKey(currentBookPath_).c_str(), static_cast<uint32_t>(reader_.wordCount()));
    }

    return true;
}

void App::loadBootBook(uint32_t nowMs) {
    storage_.refreshBooks();
    invalidateLibraryCache();

    const String savedPath = settings::load<pref::BookPath>(prefs_);
    if (!savedPath.isEmpty()) {
        const int savedBook = findBookIndexByPath(savedPath);
        if (savedBook >= 0 && loadBook(static_cast<size_t>(savedBook), nowMs)) {
            return;
        }
        Serial.printf("[app] saved book not found: %s\n", savedPath.c_str());
    }

    if (storage_.bookCount() > 0 && loadBook(0, nowMs)) {
        return;
    }
    metadata_.clear();
    currentBookPath_ = "";
    reader_.begin(nowMs);
    usingStorageBook_ = false;
}

void App::startReader(uint32_t nowMs, bool locked) {
    playLocked_ = locked;
    pauseAtSentenceEndRequested_ = false;
    wpmFeedbackUntilMs_ = 0;
    reader_.start(nowMs);
    setState(AppState::ReaderPlaying, nowMs);
}

void App::requestReaderPause(uint32_t nowMs) {
    if (state_ != AppState::ReaderPlaying) {
        return;
    }

    playLocked_ = false;
    if (pauseMode_ == PauseMode::Instant) {
        pauseAtSentenceEndRequested_ = false;
        setState(AppState::ReaderPaused, nowMs);
        return;
    }

    pauseAtSentenceEndRequested_ = true;
    if (shouldFinalizeReaderPause(nowMs)) {
        finalizeReaderPause(nowMs);
    }
}

bool App::shouldFinalizeReaderPause(uint32_t nowMs) const {
    if (state_ != AppState::ReaderPlaying || !pauseAtSentenceEndRequested_) {
        return false;
    }
    const uint32_t durationMs = reader_.currentWordDurationMs();
    if (durationMs == 0 || reader_.elapsedInCurrentWordMs(nowMs) < durationMs) {
        return false;
    }
    return reader_.currentWordEndsSentence() || reader_.atEnd();
}

void App::finalizeReaderPause(uint32_t nowMs) {
    pauseAtSentenceEndRequested_ = false;
    playLocked_ = false;
    setState(AppState::ReaderPaused, nowMs);
}

void App::toggleReaderShortcut(uint32_t nowMs) {
    if (state_ == AppState::ReaderPlaying) {
        requestReaderPause(nowMs);
        return;
    }
    if (state_ == AppState::ReaderPaused) {
        startReader(nowMs, true);
    }
}

int App::scrubStepsForDrag(int deltaX) const {
    const int absDeltaX = abs(deltaX);
    if (absDeltaX < static_cast<int>(kSwipeThresholdPx)) {
        return 0;
    }
    int steps = 1 + ((absDeltaX - static_cast<int>(kSwipeThresholdPx)) / static_cast<int>(kScrubStepPx));
    steps = std::min(steps, kMaxScrubStepsPerGesture);
    return deltaX > 0 ? steps : -steps;
}

void App::applyScrubTarget(int targetSteps, uint32_t nowMs) {
    if (targetSteps == readerTouch_.scrubStepsApplied) {
        return;
    }
    reader_.seekRelative(readerTouch_.startWordIndex, targetSteps);
    readerTouch_.scrubStepsApplied = targetSteps;
    renderReader();
    (void) nowMs;
}

void App::resetReaderTapTracking() {
    lastReaderTapValid_ = false;
}

void App::updateReader(uint32_t nowMs) {
    if (wpmFeedbackUntilMs_ > 0 && nowMs >= wpmFeedbackUntilMs_) {
        wpmFeedbackUntilMs_ = 0;
        if (state_ == AppState::ReaderPaused || state_ == AppState::ReaderPlaying) {
            renderReader();
        }
    }

    if (shouldFinalizeReaderPause(nowMs)) {
        finalizeReaderPause(nowMs);
        return;
    }

    if (state_ != AppState::ReaderPlaying) {
        return;
    }

    const size_t previousIndex = reader_.currentIndex();
    if (reader_.update(nowMs)) {
        if (mirrorProgressAtChapterTransition(previousIndex, reader_.currentIndex())) {
            renderReader();
            return;
        }
        renderReader();
    }
}

void App::applyPacingSettings() {
    ReadingLoop::PacingConfig config;
    config.longWordDelayMs = pacingLongWordDelayMs_;
    config.complexWordDelayMs = pacingComplexWordDelayMs_;
    config.punctuationDelayMs = pacingPunctuationDelayMs_;
    reader_.setPacingConfig(config);
}

void App::applyTypographySettings() {
    ui_.setTypographyConfig({focusHighlightEnabled_, trackingPx_, anchorPercent_, guideHalfWidth_, guideGap_});
}

void App::resetTypographySettings() {
    readerFontSizeIndex_ = pref::ReaderFontSizeIndex::defaultValue();
    readerTypefaceIndex_ = pref::ReaderTypefaceIndex::defaultValue();
    focusHighlightEnabled_ = pref::TypographyFocusHighlight::defaultValue();
    trackingPx_ = pref::TypographyTracking::defaultValue();
    anchorPercent_ = pref::TypographyAnchor::defaultValue();
    guideHalfWidth_ = pref::TypographyGuideWidth::defaultValue();
    guideGap_ = pref::TypographyGuideGap::defaultValue();

    settings::save<pref::ReaderFontSizeIndex>(prefs_, readerFontSizeIndex_, ui_.readerFontSizeCount());
    settings::save<pref::ReaderTypefaceIndex>(prefs_, readerTypefaceIndex_, ui_.readerTypefaceCount());
    settings::save<pref::ReaderTypefaceId>(prefs_, fonts_.typefaceId(readerTypefaceIndex_));
    settings::save<pref::TypographyFocusHighlight>(prefs_, focusHighlightEnabled_);
    settings::save<pref::TypographyTracking>(prefs_, trackingPx_);
    settings::save<pref::TypographyAnchor>(prefs_, anchorPercent_);
    settings::save<pref::TypographyGuideWidth>(prefs_, guideHalfWidth_);
    settings::save<pref::TypographyGuideGap>(prefs_, guideGap_);

    ui_.setReaderFont(readerTypefaceIndex_, readerFontSizeIndex_);
    applyTypographySettings();
}

void App::adjustWpm(int delta) {
    reader_.adjustWpm(delta);
    settings::save<pref::Wpm>(prefs_, reader_.wpm());
}

String App::phantomBeforeText() const {
    if (reader_.wordCount() == 0) {
        return "";
    }
    const size_t sizeIndex =
        std::clamp<size_t>(readerFontSizeIndex_, size_t{0}, kPhantomBeforeCharTargets.size() - 1);
    return collectPhantomBeforeText(reader_.currentIndex(), kPhantomBeforeCharTargets[sizeIndex]);
}

String App::phantomAfterText() const {
    if (reader_.wordCount() == 0) {
        return "";
    }
    const size_t sizeIndex =
        std::clamp<size_t>(readerFontSizeIndex_, size_t{0}, kPhantomAfterCharTargets.size() - 1);
    return collectPhantomAfterText(reader_.currentIndex(), kPhantomAfterCharTargets[sizeIndex]);
}

String App::collectPhantomBeforeText(size_t currentIndex, size_t charTarget) const {
    if (currentIndex == 0 || charTarget == 0) {
        return "";
    }

    size_t startIndex = currentIndex;
    size_t totalChars = 0;
    while (startIndex > 0 && totalChars < charTarget) {
        --startIndex;
        totalChars += reader_.wordAt(startIndex).length();
        if (startIndex + 1 < currentIndex) {
            ++totalChars;
        }
    }

    String text;
    text.reserve(totalChars);
    for (size_t index = startIndex; index < currentIndex; ++index) {
        if (!text.isEmpty()) {
            text += ' ';
        }
        text += reader_.wordAt(index);
    }
    return text;
}

String App::collectPhantomAfterText(size_t currentIndex, size_t charTarget) const {
    const size_t wordCount = reader_.wordCount();
    if (wordCount == 0 || currentIndex + 1 >= wordCount || charTarget == 0) {
        return "";
    }

    size_t endIndex = currentIndex + 1;
    size_t totalChars = 0;
    while (endIndex < wordCount && totalChars < charTarget) {
        totalChars += reader_.wordAt(endIndex).length();
        if (endIndex > currentIndex + 1) {
            ++totalChars;
        }
        ++endIndex;
    }

    String text;
    text.reserve(totalChars);
    for (size_t index = currentIndex + 1; index < endIndex; ++index) {
        if (!text.isEmpty()) {
            text += ' ';
        }
        text += reader_.wordAt(index);
    }
    return text;
}

String App::pacingDelayLabel(uint16_t delayMs) const {
    return delayMs == 0 ? String("Off") : String(delayMs) + "ms";
}

String App::pauseModeLabel() const {
    return pauseMode_ == PauseMode::Instant ? String("Instant") : String("Sentence End");
}

String App::onOffLabel(bool enabled) const {
    return enabled ? String("On") : String("Off");
}

String App::footerStatusLabel() const {
    return state_ == AppState::ReaderPlaying ? String(progressPercent()) + "%" : String("PAUSED");
}

String App::currentChapterLabel() const {
    if (metadata_.chapters.empty()) {
        return bookTitle();
    }
    const size_t currentWord = reader_.currentIndex();
    size_t chapterIndex = 0;
    for (size_t i = 0; i < metadata_.chapters.size(); ++i) {
        if (metadata_.chapters[i].wordIndex > currentWord) {
            break;
        }
        chapterIndex = i;
    }
    return metadata_.chapters[chapterIndex].title.isEmpty() ? bookTitle() : metadata_.chapters[chapterIndex].title;
}


String App::chapterLabelForMetadata(const BookMetadata& metadata, size_t wordIndex, const String& fallbackTitle) const {
    (void) fallbackTitle;
    if (metadata.chapters.empty()) {
        return "";
    }

    size_t chapterIndex = 0;
    for (size_t i = 0; i < metadata.chapters.size(); ++i) {
        if (metadata.chapters[i].wordIndex > wordIndex) {
            break;
        }
        chapterIndex = i;
    }

    return metadata.chapters[chapterIndex].title;
}

void App::updateBattery(uint32_t nowMs, bool force) {
    if (!force && nowMs - lastBatteryMs_ < kBatterySampleMs) {
        return;
    }
    lastBatteryMs_ = nowMs;
    Board::Power::BatteryStatus status;
    if (Board::Power::readBatteryStatus(status)) {
        batteryPercent_ = status.percent;
        batteryVoltage_ = status.voltage;
        batteryCharging_ = Board::Power::externalPowerPresent();
        ui_.setBatteryStatus(batteryPercent_, batteryVoltage_, batteryCharging_, batteryShowVoltage());
    }
}

void App::updateSync(uint32_t nowMs) {
    if (state_ != AppState::Sync) {
        return;
    }
    sync_.update();
    ui_.renderStatus("Sync", sync_.statusLine1(), sync_.statusLine2());
    (void) nowMs;
}

void App::updateUsbTransfer(uint32_t nowMs) {
    if (state_ != AppState::UsbTransfer) {
        return;
    }
    ui_.renderStatus("USB", usbTransfer_.statusMessage(), "hold PWR to exit");
    (void) nowMs;
}

void App::updateFocusTimer(uint32_t nowMs) {
    if (state_ != AppState::FocusTimer) {
        return;
    }
    focusTimer_.update(nowMs);
    renderFocusTimerSession(nowMs);
    if (focusTimer_.consumeCompletionCue()) {
        Board::Audio::beep();
    }
}

void App::runRss(uint32_t nowMs) {
    setState(AppState::Ota, nowMs);
    const RssFeedManager::Result result = rss_.checkFeeds(otaConfig(), prefs_, &App::renderStorageStatus, this);
    storage_.refreshBooks();
    invalidateLibraryCache();
    ui_.renderStatus("RSS", result.summary, result.detail);
    delay(1400);
    setState(AppState::ReaderPaused, millis());
}

void App::enterUsbTransfer(uint32_t nowMs) {
#if RSVP_USB_TRANSFER_ENABLED
    saveProgress(true);
    if (!usbTransfer_.begin(true)) {
        ui_.renderStatus("USB", "Could not start", usbTransfer_.statusMessage());
        delay(1200);
        setState(AppState::ReaderPaused, millis());
        return;
    }
    setState(AppState::UsbTransfer, nowMs);
#else
    ui_.renderStatus("USB", "Unavailable", "");
    delay(1000);
    setState(AppState::ReaderPaused, millis());
#endif
}

void App::exitUsbTransfer(uint32_t nowMs) {
    usbTransfer_.end();
    storage_.refreshBooks();
    invalidateLibraryCache();
    setState(AppState::ReaderPaused, nowMs);
}

void App::openFocusTimer() {
    selected_ = 0;
    menuScreen_ = MenuScreen::FocusTimerGenres;
    focusTimer_.open();
    renderMenu();
}

void App::selectFocusTimerGenre(uint32_t nowMs) {
    constexpr std::array<FocusTimer::Genre, 4> kGenres = {
        FocusTimer::Genre::RsvpNano,
        FocusTimer::Genre::StrengthLabs,
        FocusTimer::Genre::SelfCare,
        FocusTimer::Genre::Other,
    };
    const size_t index = std::clamp<size_t>(selected_, size_t{0}, kGenres.size() - 1);
    focusTimer_.chooseGenre(kGenres[index], nowMs);
    setState(AppState::FocusTimer, nowMs);
}

void App::runOtaCheck(uint32_t nowMs, bool install) {
    setState(AppState::Ota, nowMs);
    const OtaUpdater::Result result = install ? ota_.checkAndInstall(otaConfig(), &App::renderStorageStatus, this)
                                              : ota_.checkOnly(otaConfig(), &App::renderStorageStatus, this);
    ui_.renderStatus("OTA", result.summary, result.detail);
    delay(install && result.rebootRequired ? 500 : 1400);
    if (install && result.rebootRequired) {
        ESP.restart();
    }
    setState(AppState::ReaderPaused, millis());
}

void App::enterStandby(uint32_t nowMs) {
    if (state_ == AppState::Sleeping) {
        return;
    }
    saveProgress(true);
    ui_.wake();
    ui_.clearToBackground();
    standbyEnteredMs_ = nowMs;
    seedStandby(nowMs, kStandbyKinds[0]);
    setState(AppState::Standby, nowMs);
}

void App::exitStandby(uint32_t nowMs) {
    standbyScreensaver_.reset();
    ui_.wake();
    lastActivityMs_ = nowMs;
    setState(AppState::ReaderPaused, nowMs);
}

void App::seedStandby(uint32_t nowMs, standby::Kind kind) {
    standbyKindIndex_ = 0;
    for (uint8_t i = 0; i < kStandbyKinds.size(); ++i) {
        if (kStandbyKinds[i] == kind) {
            standbyKindIndex_ = i;
            break;
        }
    }
    const int16_t displayWidth = std::max<int16_t>(1, ui_.width());
    const int16_t displayHeight = std::max<int16_t>(1, ui_.height());
    const auto rawColumns = ceilDiv(static_cast<uint16_t>(displayWidth), kStandbyCellSizePx);
    const auto rawRows = ceilDiv(static_cast<uint16_t>(displayHeight), kStandbyCellSizePx);
    standbyColumns_ = std::clamp<uint16_t>(rawColumns, uint16_t{1}, standby::kMaxStandbyColumns);
    standbyRows_ = std::clamp<uint16_t>(rawRows, uint16_t{1}, standby::kMaxStandbyRows);
    standbyScreensaver_.select(kind, standbyColumns_, standbyRows_);
    if (standbyScreensaver_) {
        const uint32_t seed = nowMs ^ (static_cast<uint32_t>(currentBook_) << 16U)
                              ^ (static_cast<uint32_t>(reader_.currentIndex()) * 2654435761UL);
        standbyScreensaver_.seed(seed == 0 ? 1U : seed);
    }
    nextStandbyFrameMs_ = nowMs;
    nextStandbyKindMs_ = nowMs + kStandbyKindMs;
}

void App::updateStandby(uint32_t nowMs) {
    if (!standbyScreensaver_) {
        seedStandby(nowMs, kStandbyKinds[0]);
    }

    if (kCycleStandbyKinds && static_cast<int32_t>(nowMs - nextStandbyKindMs_) >= 0) {
        const uint8_t nextKind = static_cast<uint8_t>((standbyKindIndex_ + 1U) % kStandbyKinds.size());
        seedStandby(nowMs, kStandbyKinds[nextKind]);
    }

    if (static_cast<int32_t>(nowMs - nextStandbyFrameMs_) < 0) {
        return;
    }

    uint8_t steps = 0;
    do {
        standbyScreensaver_.step();
        nextStandbyFrameMs_ += kStandbyFrameMs;
        ++steps;
    } while (steps < 3 && static_cast<int32_t>(nowMs - nextStandbyFrameMs_) >= 0);

    renderStandbyFrame();
}

void App::renderStandbyFrame() {
    if (!standbyScreensaver_) {
        ui_.renderStandby();
        return;
    }
    ui_.renderStandby(standbyScreensaver_.frame(), standbyColumns_, standbyRows_, kStandbyCellSizePx);
}

void App::powerOff(uint32_t nowMs) {
    saveProgress(true);
    mirrorProgressSidecar();
    setState(AppState::Sleeping, nowMs);
    delay(250);
    ui_.sleep();
    bookStore_.close();
    storage_.end();
    Input::end();
    Board::System::holdBacklightOffForDeepSleep();
    if (Board::Power::shouldRequestShutdownOnPowerOff() || Board::Power::shouldReleaseBatteryPowerBeforeDeepSleep()) {
        Board::Power::releaseBatteryPowerHold();
        delay(1200);
    }
    Board::System::deepSleepUntilConfiguredWake();
}

void App::saveProgress(bool force) {
    if (!usingStorageBook_ || currentBookPath_.isEmpty()) {
        return;
    }

    const uint32_t nowMs = millis();
    if (!force && nowMs - lastProgressSaveMs_ < kProgressSaveMs) {
        return;
    }

    const size_t wordIndex = reader_.currentIndex();
    if (!force && wordIndex == lastSavedWordIndex_) {
        return;
    }

    lastProgressSaveMs_ = nowMs;
    cacheProgress(static_cast<uint32_t>(wordIndex));
    Serial.printf("[app] saved position word=%u path=%s\n", static_cast<unsigned int>(wordIndex),
                  currentBookPath_.c_str());
}

void App::cacheProgress(uint32_t wordIndex) {
    if (!usingStorageBook_ || currentBookPath_.isEmpty()) {
        return;
    }

    const size_t wordCount = reader_.wordCount();
    if (wordCount > 0) {
        wordIndex = std::min<uint32_t>(wordIndex, static_cast<uint32_t>(wordCount - 1));
    }

    settings::save<pref::BookPath>(prefs_, currentBookPath_);
    prefs_.putUInt(bookPositionKey(currentBookPath_).c_str(), wordIndex);
    prefs_.putUInt(bookWordCountKey(currentBookPath_).c_str(), static_cast<uint32_t>(wordCount));
    if (bookStore_.isOpen()) {
        prefs_.putUInt(bookSourceSizeKey(currentBookPath_).c_str(), bookStore_.sourceSize());
        prefs_.putUInt(bookSourceFingerprintKey(currentBookPath_).c_str(), bookStore_.sourceFingerprint());
    }
    settings::save<pref::Wpm>(prefs_, reader_.wpm());
    lastSavedWordIndex_ = wordIndex;
}

bool App::writeProgressSidecar(uint32_t wordIndex, uint32_t wordCount) {
    if (!usingStorageBook_ || currentBookPath_.isEmpty() || !bookStore_.isOpen() || wordCount == 0) {
        return false;
    }

    return ReadingProgress::writePositionSidecar(currentBookPath_,
                                                 {bookStore_.sourceSize(), bookStore_.sourceFingerprint(), wordCount},
                                                 wordIndex);
}

void App::mirrorProgressSidecar() {
    if (!usingStorageBook_ || currentBookPath_.isEmpty()) {
        return;
    }

    writeProgressSidecar(static_cast<uint32_t>(reader_.currentIndex()), static_cast<uint32_t>(reader_.wordCount()));
}

bool App::readProgressSidecar(uint32_t& wordIndex) {
    wordIndex = kNoSavedWordIndex;
    if (!usingStorageBook_ || currentBookPath_.isEmpty() || !bookStore_.isOpen()) {
        return false;
    }

    const uint32_t wordCount = static_cast<uint32_t>(reader_.wordCount());
    if (wordCount == 0) {
        return false;
    }

    return ReadingProgress::readPositionSidecar(currentBookPath_,
                                                {bookStore_.sourceSize(), bookStore_.sourceFingerprint(), wordCount},
                                                wordIndex);
}

uint32_t App::restoredWordIndexForBook() {
    uint32_t sidecarWordIndex = kNoSavedWordIndex;
    if (readProgressSidecar(sidecarWordIndex)) {
        return sidecarWordIndex;
    }

    const uint32_t nvsWordIndex = savedWordIndexForBook(currentBookPath_);
    if (nvsWordIndex != kNoSavedWordIndex) {
        writeProgressSidecar(nvsWordIndex, static_cast<uint32_t>(reader_.wordCount()));
        return nvsWordIndex;
    }

    return kNoSavedWordIndex;
}

uint32_t App::savedWordIndexForBook(const String& bookPath) {
    const String positionKey = bookPositionKey(bookPath);
    if (!prefs_.isKey(positionKey.c_str())) {
        return kNoSavedWordIndex;
    }

    const String countKey = bookWordCountKey(bookPath);
    if (prefs_.isKey(countKey.c_str())
        && prefs_.getUInt(countKey.c_str(), 0) != static_cast<uint32_t>(reader_.wordCount())) {
        Serial.printf("[app] ignored stale NVS position count for %s\n", bookPath.c_str());
        return kNoSavedWordIndex;
    }

    const String sizeKey = bookSourceSizeKey(bookPath);
    const String fingerprintKey = bookSourceFingerprintKey(bookPath);
    if (prefs_.isKey(sizeKey.c_str()) && prefs_.isKey(fingerprintKey.c_str())
        && (prefs_.getUInt(sizeKey.c_str(), 0) != bookStore_.sourceSize()
            || prefs_.getUInt(fingerprintKey.c_str(), 0) != bookStore_.sourceFingerprint())) {
        Serial.printf("[app] ignored stale NVS position source for %s\n", bookPath.c_str());
        return kNoSavedWordIndex;
    }

    return prefs_.getUInt(positionKey.c_str(), 0);
}

bool App::mirrorProgressAtChapterTransition(size_t previousWordIndex, size_t currentWordIndex) {
    if (!usingStorageBook_ || currentBookPath_.isEmpty() || metadata_.chapters.empty()) {
        return false;
    }

    for (const ChapterMarker& chapter: metadata_.chapters) {
        const size_t chapterWordIndex = chapter.wordIndex;
        if (chapterWordIndex == 0 || chapterWordIndex <= previousWordIndex || chapterWordIndex > currentWordIndex) {
            continue;
        }

        reader_.seekTo(chapterWordIndex);
        saveProgress(true);
        mirrorProgressSidecar();
        Serial.printf("[chapter] saved sidecar word=%u title=%s\n", static_cast<unsigned int>(chapterWordIndex),
                      chapter.title.c_str());
        return true;
    }

    return false;
}

int App::findBookIndexByPath(const String& path) const {
    for (size_t i = 0; i < storage_.bookCount(); ++i) {
        if (storage_.bookPath(i) == path) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

String App::bookPositionKey(const String& bookPath) const {
    char key[10];
    std::snprintf(key, sizeof(key), "p%08lx", static_cast<unsigned long>(hashBookPath(bookPath)));
    return String(key);
}

String App::bookWordCountKey(const String& bookPath) const {
    char key[10];
    std::snprintf(key, sizeof(key), "c%08lx", static_cast<unsigned long>(hashBookPath(bookPath)));
    return String(key);
}

String App::bookSourceSizeKey(const String& bookPath) const {
    char key[10];
    std::snprintf(key, sizeof(key), "s%08lx", static_cast<unsigned long>(hashBookPath(bookPath)));
    return String(key);
}

String App::bookSourceFingerprintKey(const String& bookPath) const {
    char key[10];
    std::snprintf(key, sizeof(key), "f%08lx", static_cast<unsigned long>(hashBookPath(bookPath)));
    return String(key);
}

uint32_t App::hashBookPath(const String& path) {
    uint32_t hash = 2166136261UL;
    for (size_t i = 0; i < path.length(); ++i) {
        hash ^= static_cast<uint8_t>(path[i]);
        hash *= 16777619UL;
    }
    return hash;
}

uint8_t App::progressPercent() const {
    if (reader_.wordCount() <= 1) {
        return 0;
    }
    return static_cast<uint8_t>(
        std::clamp<size_t>((reader_.currentIndex() * 100UL) / (reader_.wordCount() - 1), size_t{0}, size_t{100}));
}

bool App::batteryShowVoltage() const {
    return (batteryLabelMode_ & 1U) != 0;
}

String App::bookTitle() const {
    if (!metadata_.title.isEmpty()) {
        return metadata_.title;
    }
    return usingStorageBook_ ? storage_.bookDisplayName(currentBook_) : String("Demo");
}

String App::configuredWifiSsid() {
    return settings::load<pref::WifiSsid>(prefs_);
}

String App::configuredWifiPassword() {
    return settings::load<pref::WifiPassword>(prefs_);
}

OtaUpdater::Config App::otaConfig() {
    OtaUpdater::Config config;
    ota_.loadConfig(config);
    const String ssid = configuredWifiSsid();
    if (!ssid.isEmpty()) {
        config.wifiSsid = ssid;
        config.wifiPassword = configuredWifiPassword();
    }
    const String owner = settings::load<pref::OtaOwner>(prefs_);
    if (!owner.isEmpty()) {
        config.githubOwner = owner;
    }
    config.githubTag = settings::nvs::get(prefs_, pref::OtaTag::key(), config.githubTag);
    return config;
}

const char* App::stateName(AppState state) const {
    switch (state) {
    case AppState::Booting:
        return "Booting";
    case AppState::ReaderPaused:
        return "ReaderPaused";
    case AppState::ReaderPlaying:
        return "ReaderPlaying";
    case AppState::Menu:
        return "Menu";
    case AppState::Sync:
        return "Sync";
    case AppState::UsbTransfer:
        return "UsbTransfer";
    case AppState::FocusTimer:
        return "FocusTimer";
    case AppState::Ota:
        return "Ota";
    case AppState::Standby:
        return "Standby";
    case AppState::Sleeping:
        return "Sleeping";
    }
    return "Unknown";
}

void App::renderStorageStatus(void* context, const char* title, const char* line1, const char* line2,
                              int progressPercent) {
    if (context == nullptr) {
        return;
    }
    static_cast<App*>(context)->ui_.renderProgress(title == nullptr ? "SD" : title, line1 == nullptr ? "" : line1,
                                                   line2 == nullptr ? "" : line2, {progressPercent});
}
