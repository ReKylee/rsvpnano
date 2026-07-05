#include "display/UiRenderer.h"

#include "standby/LifeGrid.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <climits>
#include <cstdlib>

#include "board/BoardDisplay.h"
#include "text/LatinText.h"

namespace {

    constexpr uint16_t kWhite = 0xFFFF;
    constexpr uint16_t kBlack = 0x0000;
    constexpr int16_t kReaderChromeMarginX = 18;
    constexpr int16_t kReaderChromeMarginBottom = 10;
    constexpr uint8_t kReaderChromeTextSize = 2;
    constexpr int16_t kBatteryIconWidth = 26;
    constexpr int16_t kBatteryIconHeight = 13;
    constexpr int16_t kBatteryCapWidth = 3;
    constexpr int16_t kBatteryTapWidth = 126;
    constexpr int16_t kBatteryTapHeight = 36;
    constexpr uint16_t kBatteryGoodColor = DisplayTheme::rgb565(126, 176, 92);
    constexpr uint16_t kBatteryMediumColor = DisplayTheme::rgb565(214, 163, 58);
    constexpr uint16_t kBatteryLowColor = DisplayTheme::rgb565(200, 82, 82);
    constexpr uint8_t kBatteryMediumPercent = 35;
    constexpr uint8_t kBatteryLowPercent = 18;
    constexpr int16_t kPreviousSentenceHintX = 18;
    constexpr int16_t kMenuRowHeight = 22;
    constexpr int16_t kMenuX = 28;
    constexpr int16_t kReaderSideMargin = 12;

    constexpr auto kReaderSizeLabels = std::to_array<const char*>({
        "Large",
        "Medium",
        "Small",
    });

    struct ReaderTextStyle {
        int16_t phantomGap;
        uint8_t phantomAlpha;
    };

    constexpr std::array kReaderTextStyles = {
        ReaderTextStyle{30, 54},
        ReaderTextStyle{24, 62},
        ReaderTextStyle{20, 72},
    };
    static_assert(kReaderTextStyles.size() == kReaderSizeLabels.size());

    int16_t textWidth(const String& text, uint8_t size) {
        return static_cast<int16_t>(text.length() * 6 * std::max<uint8_t>(1, size));
    }

    int16_t textHeight(uint8_t size) {
        return static_cast<int16_t>(8 * std::max<uint8_t>(1, size));
    }


    constexpr int16_t kLibraryNavWidth = 82;
    constexpr int16_t kLibraryContentX = kLibraryNavWidth + 12;
    constexpr int16_t kLibraryViewportX = kLibraryContentX;
    constexpr int16_t kLibraryViewportY = 8;
    constexpr int16_t kLibraryViewportWidth = 498;
    constexpr int16_t kLibraryViewportHeight = 120;
    constexpr int16_t kLibraryShelfMarkerX = kLibraryViewportX + (kLibraryViewportWidth / 2);
    constexpr int16_t kLibraryShelfBottomY = kLibraryViewportY + kLibraryViewportHeight;
    constexpr int16_t kLibraryDetailY = 136;
    constexpr int16_t kLibraryDetailHeight = 30;
    constexpr int16_t kLibraryGap = 5;

    int16_t librarySpineWidthFor(const UiRenderer::LibraryItem& item, size_t index) {
        const int16_t base = item.article ? 22 : 24;
        const int16_t titleVariation = static_cast<int16_t>(std::min<size_t>(item.title.length(), 18) / 3);
        return static_cast<int16_t>(base + titleVariation + static_cast<int16_t>((index * 7) % 13));
    }

    int16_t librarySpineHeightFor(const UiRenderer::LibraryItem& item, size_t index) {
        const int16_t base = item.article ? 78 : 84;
        const int16_t titleVariation = static_cast<int16_t>(std::min<size_t>(item.title.length(), 24) / 2);
        return static_cast<int16_t>(std::min<int16_t>(112, base + titleVariation + static_cast<int16_t>((index * 5) % 17)));
    }

    int16_t librarySpineLeftFor(const std::vector<UiRenderer::LibraryItem>& items, size_t index) {
        int16_t left = 0;
        const size_t capped = std::min(index, items.size());
        for (size_t i = 0; i < capped; ++i) {
            left = static_cast<int16_t>(left + librarySpineWidthFor(items[i], i) + kLibraryGap);
        }
        return left;
    }

    String libraryStackLabel(const String& title) {
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
        if (out.isEmpty()) {
            out = "BOOK";
        }
        return out;
    }

    uint16_t librarySpineColor(size_t index, bool article, uint16_t accent, uint16_t foreground, uint16_t muted) {
        constexpr std::array<uint16_t, 8> kBookColors = {
            DisplayTheme::rgb565(153, 60, 29),
            DisplayTheme::rgb565(24, 95, 165),
            DisplayTheme::rgb565(15, 110, 86),
            DisplayTheme::rgb565(122, 112, 184),
            DisplayTheme::rgb565(63, 146, 130),
            DisplayTheme::rgb565(183, 154, 107),
            DisplayTheme::rgb565(155, 74, 46),
            DisplayTheme::rgb565(46, 95, 122),
        };
        constexpr std::array<uint16_t, 5> kArticleColors = {
            DisplayTheme::rgb565(138, 112, 72),
            DisplayTheme::rgb565(98, 123, 132),
            DisplayTheme::rgb565(128, 83, 73),
            DisplayTheme::rgb565(82, 111, 80),
            DisplayTheme::rgb565(109, 91, 128),
        };
        if (article) {
            return kArticleColors[index % kArticleColors.size()];
        }
        if (index == 0) {
            return accent;
        }
        (void) foreground;
        (void) muted;
        return kBookColors[index % kBookColors.size()];
    }

    
    uint16_t libraryBindingColor(size_t index, bool article) {
        constexpr std::array<uint16_t, 5> kBookBindingColors = {
            DisplayTheme::rgb565(229, 193, 92),
            DisplayTheme::rgb565(219, 203, 136),
            DisplayTheme::rgb565(236, 213, 139),
            DisplayTheme::rgb565(209, 191, 230),
            DisplayTheme::rgb565(230, 211, 163),
        };
        constexpr std::array<uint16_t, 3> kArticleBindingColors = {
            DisplayTheme::rgb565(91, 75, 53),
            DisplayTheme::rgb565(236, 226, 198),
            DisplayTheme::rgb565(122, 101, 72),
        };

        if (article) {
            return kArticleBindingColors[index % kArticleBindingColors.size()];
        }
        return kBookBindingColors[index % kBookBindingColors.size()];
    }

    uint16_t libraryArticleBandColor(size_t index) {
        constexpr std::array<uint16_t, 4> kArticleBandColors = {
            DisplayTheme::rgb565(234, 225, 194),
            DisplayTheme::rgb565(110, 88, 58),
            DisplayTheme::rgb565(224, 210, 170),
            DisplayTheme::rgb565(126, 101, 68),
        };
        return kArticleBandColors[index % kArticleBandColors.size()];
    }

    uint16_t batteryFillColor(uint8_t percent, bool charging) {
        if (charging) {
            return kBatteryGoodColor;
        }
        if (percent <= kBatteryLowPercent) {
            return kBatteryLowColor;
        }
        if (percent <= kBatteryMediumPercent) {
            return kBatteryMediumColor;
        }
        return kBatteryGoodColor;
    }

    bool isUtf8Continuation(uint8_t value) {
        return (value & 0xC0U) == 0x80U;
    }

    size_t utf8CharLength(const String& text, size_t index) {
        if (index >= text.length()) {
            return 0;
        }

        const uint8_t first = static_cast<uint8_t>(text[index]);
        if (first < 0x80U) {
            return 1;
        }

        const size_t remaining = text.length() - index;
        if ((first & 0xE0U) == 0xC0U && remaining >= 2 && isUtf8Continuation(static_cast<uint8_t>(text[index + 1]))) {
            return 2;
        }
        if ((first & 0xF0U) == 0xE0U && remaining >= 3 && isUtf8Continuation(static_cast<uint8_t>(text[index + 1]))
            && isUtf8Continuation(static_cast<uint8_t>(text[index + 2]))) {
            return 3;
        }
        if ((first & 0xF8U) == 0xF0U && remaining >= 4 && isUtf8Continuation(static_cast<uint8_t>(text[index + 1]))
            && isUtf8Continuation(static_cast<uint8_t>(text[index + 2]))
            && isUtf8Continuation(static_cast<uint8_t>(text[index + 3]))) {
            return 4;
        }

        return 1;
    }

    String utf8GlyphAt(const String& text, size_t index) {
        const size_t length = utf8CharLength(text, index);
        return length > 0 ? text.substring(index, index + length) : String();
    }

    bool nextCodepoint(const String& text, size_t& index, uint16_t& codepoint) {
        if (index >= text.length()) {
            return false;
        }

        const uint8_t first = static_cast<uint8_t>(text[index++]);
        if (first < 0x80U) {
            codepoint = first;
            return true;
        }

        auto continuation = [&](uint8_t& value) {
            if (index >= text.length()) {
                return false;
            }
            value = static_cast<uint8_t>(text[index]);
            if (!isUtf8Continuation(value)) {
                return false;
            }
            ++index;
            return true;
        };

        if ((first & 0xE0U) == 0xC0U) {
            uint8_t b1 = 0;
            if (!continuation(b1)) {
                codepoint = '?';
                return true;
            }
            const uint16_t value = static_cast<uint16_t>(((first & 0x1FU) << 6U) | (b1 & 0x3FU));
            codepoint = value >= 0x80U ? value : static_cast<uint16_t>('?');
            return true;
        }

        if ((first & 0xF0U) == 0xE0U) {
            uint8_t b1 = 0;
            uint8_t b2 = 0;
            if (!continuation(b1) || !continuation(b2)) {
                codepoint = '?';
                return true;
            }
            const uint16_t value =
                static_cast<uint16_t>(((first & 0x0FU) << 12U) | ((b1 & 0x3FU) << 6U) | (b2 & 0x3FU));
            codepoint = value >= 0x800U ? value : static_cast<uint16_t>('?');
            return true;
        }

        if ((first & 0xF8U) == 0xF0U) {
            for (uint8_t i = 0; i < 3 && index < text.length() && isUtf8Continuation(static_cast<uint8_t>(text[index]));
                 ++i) {
                ++index;
            }
        }

        codepoint = '?';
        return true;
    }

    bool isReaderWordCodepoint(uint16_t codepoint) {
        if (codepoint < 0x80U) {
            return LatinText::isWordCharacter(static_cast<uint8_t>(codepoint));
        }

        return (codepoint >= 0x00C0U && codepoint <= 0x024FU) || (codepoint >= 0x0400U && codepoint <= 0x052FU);
    }

    int orpOrdinalForLength(int length) {
        if (length <= 1) {
            return 0;
        }
        if (length <= 5) {
            return 1;
        }
        if (length <= 9) {
            return 2;
        }
        if (length <= 13) {
            return 3;
        }
        return 4;
    }

    ReaderTextStyle readerTextStyle(uint8_t fontSizeIndex) {
        if (fontSizeIndex >= kReaderTextStyles.size()) {
            fontSizeIndex = 0;
        }
        return kReaderTextStyles[fontSizeIndex];
    }

} // namespace

UiRenderer::UiRenderer() : readerText_(&Board::Display::gfx()) {}

bool UiRenderer::begin() {
    if (!Board::Display::begin()) {
        return false;
    }
    readerText_.begin();
    fill(color(DisplayTheme::ColorRole::Background));
    return true;
}

void UiRenderer::setTheme(const DisplayTheme::Theme& theme) {
    theme_ = theme;
}

void UiRenderer::setFontCatalog(FontCatalog* catalog) {
    fontCatalog_ = catalog;
    setReaderFont(readerTypefaceIndex_, readerFontSizeIndex_);
}

void UiRenderer::setBrightness(uint8_t percent) {
    Board::Display::setBrightness(percent);
}

void UiRenderer::setBatteryStatus(uint8_t percent, float voltage, bool charging, bool showVoltage) {
    batteryPercent_ = std::min<uint8_t>(percent, 100);
    batteryVoltage_ = voltage;
    batteryCharging_ = charging;
    batteryShowVoltage_ = showVoltage;
}

bool UiRenderer::hitBattery(Tap tap) const {
    return contains(batteryRect(), tap);
}

void UiRenderer::setReaderFont(uint8_t typefaceIndex, uint8_t sizeIndex) {
    const uint8_t typefaceCount = readerTypefaceCount();
    readerTypefaceIndex_ = typefaceCount == 0 ? 0 : std::min<uint8_t>(typefaceIndex, typefaceCount - 1);
    readerFontSizeIndex_ = std::min<uint8_t>(sizeIndex, readerFontSizeCount() - 1);
    readerFont_ = fontCatalog_ != nullptr ? fontCatalog_->loadFont(readerTypefaceIndex_, readerFontSizeIndex_)
                                          : FontCatalog::fallbackFont(readerFontSizeIndex_);
}

void UiRenderer::setTypographyConfig(const TypographyConfig& config) {
    typography_ = config;
}

void UiRenderer::sleep() {
    Board::Display::sleep();
}

void UiRenderer::wake() {
    Board::Display::wake();
}

void UiRenderer::clearToBackground() {
    fill(color(DisplayTheme::ColorRole::Background));
    Board::Display::gfx().flush();
}


uint8_t UiRenderer::readerTypefaceCount() const {
    return fontCatalog_ != nullptr ? fontCatalog_->typefaceCount() : uint8_t{1};
}

uint8_t UiRenderer::readerFontSizeCount() {
    return FontCatalog::sizeCount();
}

const char* UiRenderer::readerTypefaceLabel(uint8_t index) const {
    return fontCatalog_ != nullptr ? fontCatalog_->typefaceLabel(index) : "Literata";
}

const char* UiRenderer::readerFontSizeLabel(uint8_t index) {
    return FontCatalog::sizeLabel(index);
}

void UiRenderer::renderReader(const String& beforeText, const String& word, const String& afterText,
                              const String& chapterLabel, uint8_t progressPercent, const String& footerStatusLabel,
                              bool showFooter, const String& overlayText, ReaderChrome chrome) {
    fill(color(DisplayTheme::ColorRole::Background));

    const String displayWord = readerDisplayText(word);
    const String displayBefore = readerDisplayText(beforeText);
    const String displayAfter = readerDisplayText(afterText);
    const int focusIndex = findFocusLetterIndex(word);
    const AlphaFont* font = readerFont();
    const int16_t wordInkTop = font != nullptr ? font->wordInkTop : static_cast<int16_t>(-textHeight(2));
    const int16_t wordInkBottom = font != nullptr ? font->wordInkBottom : 0;
    const int16_t wordInkHeight = static_cast<int16_t>(wordInkBottom - wordInkTop + 1);
    const int16_t wordBaseline = static_cast<int16_t>(((height() - wordInkHeight) / 2) - wordInkTop);
    const int16_t wordX = readerWordStartX(displayWord, focusIndex);
    const int16_t anchorX = static_cast<int16_t>((width() * typography_.anchorPercent) / 100);
    const ReaderTextStyle style = readerTextStyle(readerFontSizeIndex_);
    const uint16_t phantomColor = blendOverBackground(color(DisplayTheme::ColorRole::Foreground), style.phantomAlpha);

    drawReaderGuide(anchorX, wordBaseline, wordInkTop, wordInkBottom);
    if (!displayBefore.isEmpty()) {
        drawReaderPlain(displayBefore, wordX - style.phantomGap - readerTextWidth(displayBefore), wordBaseline,
                        phantomColor);
    }
    drawReaderWord(displayWord, wordX, wordBaseline, focusIndex);
    if (!displayAfter.isEmpty()) {
        drawReaderPlain(displayAfter, wordX + readerTextWidth(displayWord) + style.phantomGap, wordBaseline,
                        phantomColor);
    }

    if (!overlayText.isEmpty()) {
        drawCenteredText(fitText(overlayText, 2, width() - 24), height() - textHeight(2) - 40, 2,
                         color(DisplayTheme::ColorRole::Accent));
    }
    if (chrome.showPreviousSentenceHint) {
        drawText("<<", kPreviousSentenceHintX, (height() - textHeight(2)) / 2, 2,
                 color(DisplayTheme::ColorRole::Muted));
    }
    drawReaderFooter(chapterLabel, footerStatusLabel.isEmpty() ? String(progressPercent) + "%" : footerStatusLabel,
                     chrome);
    if (chrome.showBattery) {
        drawBattery();
    }
    Board::Display::gfx().flush();
}

int UiRenderer::renderMainMenu(const String& currentBookTitle, uint8_t progressPercent, size_t selectedIndex) {
    return renderMainMenu(currentBookTitle, progressPercent, selectedIndex, Tap{false, 0, 0});
}

int UiRenderer::renderMainMenu(const String& currentBookTitle, uint8_t progressPercent, size_t selectedIndex, Tap tap) {
    if (width() < 620 || height() < 150 || height() > 240) {
        std::vector<Button> items = {
            Button{String("Press to Resume"), selectedIndex == 0}, Button{String("Chapters"), selectedIndex == 1},
            Button{String("Library"), selectedIndex == 2},         Button{String("Settings"), selectedIndex == 3},
            Button{String("Device"), selectedIndex == 4},          Button{String("Focus Timer"), selectedIndex == 5},
            Button{String("Power Off"), selectedIndex == 6},
        };
        return renderMenu("Menu", items, tap);
    }

    Arduino_GFX& gfx = Board::Display::gfx();
    const uint16_t background = color(DisplayTheme::ColorRole::Background);
    const uint16_t foreground = color(DisplayTheme::ColorRole::Foreground);
    const uint16_t muted = color(DisplayTheme::ColorRole::Muted);
    const uint16_t accent = color(DisplayTheme::ColorRole::Accent);
    const uint16_t outline = color(DisplayTheme::ColorRole::Outline);
    const uint16_t surface = color(DisplayTheme::ColorRole::Surface);
    const uint16_t surfaceMuted = color(DisplayTheme::ColorRole::SurfaceMuted);
    const uint16_t track = color(DisplayTheme::ColorRole::ProgressTrack);
    const uint16_t activeWash = blendOverBackground(accent, theme_.lowBrightness ? 42 : 22);
    const uint16_t faintInk = blendOverBackground(foreground, theme_.lowBrightness ? 78 : 46);

    constexpr uint8_t kTiny = 1;
    constexpr uint8_t kLabel = 2;
    constexpr int16_t kNavWidth = 82;
    constexpr int16_t kPowerSize = 28;
    constexpr int16_t kPowerXInset = 10;
    constexpr int16_t kPowerY = 7;
    constexpr int16_t kContentX = kNavWidth + 12;
    constexpr int16_t kResumeY = 8;
    constexpr int16_t kResumeHeight = 54;
    constexpr int16_t kActionY = 82;
    constexpr int16_t kActionHeight = 64;
    constexpr int16_t kActionGap = 14;

    auto makeRect = [](int x, int y, int w, int h) {
        return Rect{static_cast<int16_t>(x), static_cast<int16_t>(y), static_cast<int16_t>(w), static_cast<int16_t>(h)};
    };
    auto selected = [&](int id) {
        return selectedIndex == static_cast<size_t>(id);
    };
    auto selectedColor = [&](int id) {
        return selected(id) ? accent : foreground;
    };
    auto drawFocusFrame = [&](Rect rect, int id) {
        if (!selected(id)) {
            return;
        }
        gfx.fillRoundRect(rect.x, rect.y, rect.w, rect.h, 6, activeWash);
        gfx.fillRect(static_cast<int16_t>(rect.x + 8), static_cast<int16_t>(rect.y + rect.h - 4),
                     static_cast<int16_t>(rect.w - 16), 2, accent);
    };
    auto drawPowerIcon = [&](int16_t cx, int16_t cy, uint16_t ink) {
        gfx.drawCircle(cx, cy, 8, ink);
        gfx.fillRect(static_cast<int16_t>(cx - 3), static_cast<int16_t>(cy - 10), 7, 10, background);
        gfx.drawFastVLine(cx, static_cast<int16_t>(cy - 10), 9, ink);
    };
    auto drawContentsIcon = [&](int16_t x, int16_t y, uint16_t ink) {
        gfx.drawRect(x, y, 23, 31, ink);
        gfx.fillRect(static_cast<int16_t>(x + 23), static_cast<int16_t>(y + 5), 6, 7, accent);
        gfx.fillRect(static_cast<int16_t>(x + 23), static_cast<int16_t>(y + 19), 6, 7, faintInk);
        gfx.drawFastHLine(static_cast<int16_t>(x + 5), static_cast<int16_t>(y + 8), 13, accent);
        gfx.drawFastHLine(static_cast<int16_t>(x + 5), static_cast<int16_t>(y + 16), 13, muted);
        gfx.drawFastHLine(static_cast<int16_t>(x + 5), static_cast<int16_t>(y + 24), 12, muted);
    };
    auto drawLibraryIcon = [&](int16_t x, int16_t y, uint16_t ink) {
        gfx.fillRect(x, static_cast<int16_t>(y + 30), 41, 3, outline);
        gfx.fillRect(static_cast<int16_t>(x + 3), static_cast<int16_t>(y + 8), 7, 22, accent);
        gfx.fillRect(static_cast<int16_t>(x + 13), static_cast<int16_t>(y + 12), 7, 18, faintInk);
        gfx.fillRect(static_cast<int16_t>(x + 23), static_cast<int16_t>(y + 4), 7, 26, ink);
        gfx.fillRect(static_cast<int16_t>(x + 33), static_cast<int16_t>(y + 15), 7, 15, muted);
    };
    auto drawRailTab = [&](Rect rect, const char* title, int id, bool readTab) {
        const bool isSelected = id >= 0 && selected(id);
        const uint16_t fillColor = readTab || isSelected ? surface : surfaceMuted;
        gfx.fillRect(rect.x, rect.y, rect.w, rect.h, fillColor);
        gfx.drawRect(rect.x, rect.y, rect.w, rect.h, outline);
        if (readTab || isSelected) {
            gfx.fillRect(rect.x, static_cast<int16_t>(rect.y + 6), 3, static_cast<int16_t>(rect.h - 12), accent);
        }
        const uint16_t titleColor = readTab || isSelected ? foreground : muted;
        const int16_t labelY = static_cast<int16_t>(rect.y + ((rect.h - textHeight(kTiny)) / 2));
        drawText(title, static_cast<int16_t>(rect.x + 10), labelY, kTiny, titleColor);
    };
    auto drawAction = [&](Rect rect, const char* label, int id, bool libraryIcon) {
        drawFocusFrame(rect, id);
        gfx.drawRect(rect.x, rect.y, rect.w, rect.h, outline);
        const uint16_t ink = selectedColor(id);
        if (libraryIcon) {
            drawLibraryIcon(static_cast<int16_t>(rect.x + 8), static_cast<int16_t>(rect.y + 12), ink);
        } else {
            drawContentsIcon(static_cast<int16_t>(rect.x + 10), static_cast<int16_t>(rect.y + 12), ink);
        }
        drawText(label, static_cast<int16_t>(rect.x + 58), static_cast<int16_t>(rect.y + 24), kLabel, ink);
    };

    fill(background);

    const Rect readTab = makeRect(0, 0, kNavWidth, 53);
    const Rect settingsTab = makeRect(0, 53, kNavWidth, 53);
    const Rect deviceTab = makeRect(0, 106, kNavWidth, 34);
    const Rect focusTab = makeRect(0, 140, kNavWidth, height() - 140);
    drawRailTab(readTab, "Read", -1, true);
    drawRailTab(settingsTab, "Settings", 3, false);
    drawRailTab(deviceTab, "Device", 4, false);
    drawRailTab(focusTab, "Focus", 5, false);

    const Rect powerRect = makeRect(width() - kPowerSize - kPowerXInset, kPowerY, kPowerSize, kPowerSize);
    drawFocusFrame(powerRect, 6);
    gfx.drawRoundRect(powerRect.x, powerRect.y, powerRect.w, powerRect.h, 7, outline);
    drawPowerIcon(static_cast<int16_t>(powerRect.x + (powerRect.w / 2)),
                  static_cast<int16_t>(powerRect.y + (powerRect.h / 2)), selectedColor(6));

    const int16_t rightLimit = static_cast<int16_t>(powerRect.x - 10);
    const Rect resumeRect = makeRect(kContentX, kResumeY, rightLimit - kContentX, kResumeHeight);
    drawFocusFrame(resumeRect, 0);
    gfx.fillRect(resumeRect.x, resumeRect.y, resumeRect.w, resumeRect.h, surface);
    gfx.drawRect(resumeRect.x, resumeRect.y, resumeRect.w, resumeRect.h, outline);
    const int16_t bookmarkX = static_cast<int16_t>(resumeRect.x + resumeRect.w - 34);
    const int16_t bookmarkY = resumeRect.y;
    constexpr int16_t kBookmarkW = 13;
    constexpr int16_t kBookmarkH = 30;
    gfx.fillRect(bookmarkX, bookmarkY, kBookmarkW, kBookmarkH, accent);
    for (int16_t row = 0; row <= 6; ++row) {
        gfx.drawFastHLine(static_cast<int16_t>(bookmarkX + (kBookmarkW / 2) - row),
                          static_cast<int16_t>(bookmarkY + kBookmarkH - 7 + row),
                          static_cast<int16_t>((row * 2) + 1), surface);
    }
    drawText("Press to Resume", static_cast<int16_t>(resumeRect.x + 10), static_cast<int16_t>(resumeRect.y + 8), kTiny,
             muted);
    gfx.drawFastHLine(static_cast<int16_t>(resumeRect.x + 10), static_cast<int16_t>(resumeRect.y + 20),
                      static_cast<int16_t>(resumeRect.w - 54), outline);

    const String title = fitText(currentBookTitle, kLabel, static_cast<int16_t>(resumeRect.w - 100));
    drawText(title, static_cast<int16_t>(resumeRect.x + 10), static_cast<int16_t>(resumeRect.y + 29), kLabel,
             selectedColor(0));
    const int clampedProgress = std::clamp<int>(progressPercent, 0, 100);
    const String progressLabel = String(clampedProgress) + "%";
    drawText(progressLabel, static_cast<int16_t>(resumeRect.x + resumeRect.w - 72),
             static_cast<int16_t>(resumeRect.y + 31), kLabel, accent);

    const int16_t progressX = static_cast<int16_t>(resumeRect.x + 10);
    const int16_t progressY = static_cast<int16_t>(resumeRect.y + resumeRect.h - 6);
    const int16_t progressEndX = static_cast<int16_t>(bookmarkX - 10);
    const int16_t progressW = std::max<int16_t>(0, static_cast<int16_t>(progressEndX - progressX));
    gfx.drawFastHLine(progressX, progressY, progressW, track);
    gfx.drawFastHLine(progressX, progressY, static_cast<int16_t>((progressW * clampedProgress) / 100), accent);

    const int16_t actionWidth = static_cast<int16_t>((rightLimit - kContentX - kActionGap) / 2);
    const Rect contentsRect = makeRect(kContentX, kActionY, actionWidth, kActionHeight);
    const Rect libraryRect = makeRect(kContentX + actionWidth + kActionGap, kActionY, actionWidth, kActionHeight);
    drawAction(contentsRect, "Chapters", 1, false);
    drawAction(libraryRect, "Library", 2, true);

    Board::Display::gfx().flush();

    const Element hits[] = {
        {readTab, kMenuNavRead}, {resumeRect, 0}, {contentsRect, 1}, {libraryRect, 2},
        {settingsTab, kMenuNavSettings}, {deviceTab, kMenuNavDevice}, {focusTab, kMenuNavFocus},
        {powerRect, kMenuNavPower},
    };
    return hit(tap, hits);
}

int UiRenderer::renderSettingsHub(size_t selectedIndex) {
    return renderSettingsHub(selectedIndex, Tap{false, 0, 0});
}

int UiRenderer::renderSettingsHub(size_t selectedIndex, Tap tap) {
    if (width() < 620 || height() < 150 || height() > 240) {
        std::vector<Button> items = {
            Button{String("Reading"), selectedIndex == 0},
            Button{String("Display"), selectedIndex == 1},
            Button{String("Pacing"), selectedIndex == 2},
            Button{String("Typography / Aa"), selectedIndex == 3},
        };
        return renderMenu("Settings", items, tap);
    }

    Arduino_GFX& gfx = Board::Display::gfx();
    const uint16_t background = color(DisplayTheme::ColorRole::Background);
    const uint16_t foreground = color(DisplayTheme::ColorRole::Foreground);
    const uint16_t muted = color(DisplayTheme::ColorRole::Muted);
    const uint16_t accent = color(DisplayTheme::ColorRole::Accent);
    const uint16_t outline = color(DisplayTheme::ColorRole::Outline);
    const uint16_t surface = color(DisplayTheme::ColorRole::Surface);
    const uint16_t surfaceMuted = color(DisplayTheme::ColorRole::SurfaceMuted);
    const uint16_t activeWash = blendOverBackground(accent, theme_.lowBrightness ? 42 : 22);

    constexpr uint8_t kTiny = 1;
    constexpr uint8_t kLabel = 2;
    constexpr int16_t kNavWidth = 82;
    constexpr int16_t kPowerSize = 28;
    constexpr int16_t kPowerXInset = 10;
    constexpr int16_t kPowerY = 7;
    constexpr int16_t kContentX = kNavWidth + 12;
    constexpr int16_t kTileGap = 12;
    constexpr int16_t kTileHeight = 58;
    constexpr int16_t kTopRowY = 12;
    constexpr int16_t kBottomRowY = 82;

    auto makeRect = [](int x, int y, int w, int h) {
        return Rect{static_cast<int16_t>(x), static_cast<int16_t>(y), static_cast<int16_t>(w), static_cast<int16_t>(h)};
    };
    auto selected = [&](int id) {
        return selectedIndex == static_cast<size_t>(id);
    };
    auto selectedColor = [&](int id) {
        return selected(id) ? accent : foreground;
    };
    auto drawPowerIcon = [&](int16_t cx, int16_t cy, uint16_t ink) {
        gfx.drawCircle(cx, cy, 8, ink);
        gfx.fillRect(static_cast<int16_t>(cx - 3), static_cast<int16_t>(cy - 10), 7, 10, background);
        gfx.drawFastVLine(cx, static_cast<int16_t>(cy - 10), 9, ink);
    };
    auto drawRailTab = [&](Rect rect, const char* title, bool active) {
        gfx.fillRect(rect.x, rect.y, rect.w, rect.h, active ? surface : surfaceMuted);
        gfx.drawRect(rect.x, rect.y, rect.w, rect.h, outline);
        if (active) {
            gfx.fillRect(rect.x, static_cast<int16_t>(rect.y + 6), 3, static_cast<int16_t>(rect.h - 12), accent);
        }
        const int16_t labelY = static_cast<int16_t>(rect.y + ((rect.h - textHeight(kTiny)) / 2));
        drawText(title, static_cast<int16_t>(rect.x + 10), labelY, kTiny, active ? foreground : muted);
    };
    auto drawTile = [&](Rect rect, const char* label, int id, bool aaMark) {
        if (selected(id)) {
            gfx.fillRoundRect(rect.x, rect.y, rect.w, rect.h, 6, activeWash);
            gfx.fillRect(static_cast<int16_t>(rect.x + 8), static_cast<int16_t>(rect.y + rect.h - 4),
                         static_cast<int16_t>(rect.w - 16), 2, accent);
        }
        gfx.drawRect(rect.x, rect.y, rect.w, rect.h, outline);
        if (aaMark) {
            drawText("Aa", static_cast<int16_t>(rect.x + 10), static_cast<int16_t>(rect.y + 14), kLabel,
                     selectedColor(id));
            drawText(label, static_cast<int16_t>(rect.x + 54), static_cast<int16_t>(rect.y + 16), kLabel,
                     selectedColor(id));
        } else {
            drawText(label, static_cast<int16_t>(rect.x + 14), static_cast<int16_t>(rect.y + 16), kLabel,
                     selectedColor(id));
        }
    };

    fill(background);

    const Rect readTab = makeRect(0, 0, kNavWidth, 53);
    const Rect settingsTab = makeRect(0, 53, kNavWidth, 53);
    const Rect deviceTab = makeRect(0, 106, kNavWidth, 34);
    const Rect focusTab = makeRect(0, 140, kNavWidth, height() - 140);
    drawRailTab(readTab, "Read", false);
    drawRailTab(settingsTab, "Settings", true);
    drawRailTab(deviceTab, "Device", false);
    drawRailTab(focusTab, "Focus", false);

    const Rect powerRect = makeRect(width() - kPowerSize - kPowerXInset, kPowerY, kPowerSize, kPowerSize);
    gfx.drawRoundRect(powerRect.x, powerRect.y, powerRect.w, powerRect.h, 7, outline);
    drawPowerIcon(static_cast<int16_t>(powerRect.x + (powerRect.w / 2)),
                  static_cast<int16_t>(powerRect.y + (powerRect.h / 2)), muted);

    const int16_t rightLimit = static_cast<int16_t>(powerRect.x - 10);
    const int16_t tileWidth = static_cast<int16_t>((rightLimit - kContentX - kTileGap) / 2);
    const Rect readingRect = makeRect(kContentX, kTopRowY, tileWidth, kTileHeight);
    const Rect displayRect = makeRect(kContentX + tileWidth + kTileGap, kTopRowY, tileWidth, kTileHeight);
    const Rect pacingRect = makeRect(kContentX, kBottomRowY, tileWidth, kTileHeight);
    const Rect typeRect = makeRect(kContentX + tileWidth + kTileGap, kBottomRowY, tileWidth, kTileHeight);
    drawTile(readingRect, "Reading", 0, false);
    drawTile(displayRect, "Display", 1, false);
    drawTile(pacingRect, "Pacing", 2, false);
    drawTile(typeRect, "Typography", 3, true);

    Board::Display::gfx().flush();

    const Element hits[] = {
        {readTab, kMenuNavRead}, {settingsTab, kMenuNavSettings}, {deviceTab, kMenuNavDevice},
        {focusTab, kMenuNavFocus}, {powerRect, kMenuNavPower}, {readingRect, 0},
        {displayRect, 1}, {pacingRect, 2}, {typeRect, 3},
    };
    return hit(tap, hits);
}

int UiRenderer::renderDeviceHub(size_t selectedIndex) {
    return renderDeviceHub(selectedIndex, Tap{false, 0, 0});
}

int UiRenderer::renderDeviceHub(size_t selectedIndex, Tap tap) {
    if (width() < 620 || height() < 150 || height() > 240) {
        std::vector<Button> items = {
            Button{String("Storage Status"), selectedIndex == 0},
            Button{String("Sync / Import"), selectedIndex == 1},
            Button{String("OTA Update"), selectedIndex == 2},
        };
        return renderMenu("Device", items, tap);
    }

    Arduino_GFX& gfx = Board::Display::gfx();
    const uint16_t background = color(DisplayTheme::ColorRole::Background);
    const uint16_t foreground = color(DisplayTheme::ColorRole::Foreground);
    const uint16_t muted = color(DisplayTheme::ColorRole::Muted);
    const uint16_t accent = color(DisplayTheme::ColorRole::Accent);
    const uint16_t outline = color(DisplayTheme::ColorRole::Outline);
    const uint16_t surface = color(DisplayTheme::ColorRole::Surface);
    const uint16_t surfaceMuted = color(DisplayTheme::ColorRole::SurfaceMuted);
    const uint16_t activeWash = blendOverBackground(accent, theme_.lowBrightness ? 42 : 22);

    constexpr uint8_t kTiny = 1;
    constexpr uint8_t kLabel = 2;
    constexpr int16_t kNavWidth = 82;
    constexpr int16_t kPowerSize = 28;
    constexpr int16_t kPowerXInset = 10;
    constexpr int16_t kPowerY = 7;
    constexpr int16_t kContentX = kNavWidth + 12;
    constexpr int16_t kTileY = 54;
    constexpr int16_t kTileHeight = 72;

    auto makeRect = [](int x, int y, int w, int h) {
        return Rect{static_cast<int16_t>(x), static_cast<int16_t>(y), static_cast<int16_t>(w), static_cast<int16_t>(h)};
    };
    auto selected = [&](int id) {
        return selectedIndex == static_cast<size_t>(id);
    };
    auto drawPowerIcon = [&](int16_t cx, int16_t cy, uint16_t ink) {
        gfx.drawCircle(cx, cy, 8, ink);
        gfx.fillRect(static_cast<int16_t>(cx - 3), static_cast<int16_t>(cy - 10), 7, 10, background);
        gfx.drawFastVLine(cx, static_cast<int16_t>(cy - 10), 9, ink);
    };
    auto drawRailTab = [&](Rect rect, const char* title, bool active) {
        gfx.fillRect(rect.x, rect.y, rect.w, rect.h, active ? surface : surfaceMuted);
        gfx.drawRect(rect.x, rect.y, rect.w, rect.h, outline);
        if (active) {
            gfx.fillRect(rect.x, static_cast<int16_t>(rect.y + 6), 3, static_cast<int16_t>(rect.h - 12), accent);
        }
        const int16_t labelY = static_cast<int16_t>(rect.y + ((rect.h - textHeight(kTiny)) / 2));
        drawText(title, static_cast<int16_t>(rect.x + 10), labelY, kTiny, active ? foreground : muted);
    };
    auto drawTile = [&](Rect rect, const char* label, int id) {
        if (selected(id)) {
            gfx.fillRoundRect(rect.x, rect.y, rect.w, rect.h, 6, activeWash);
            gfx.fillRect(static_cast<int16_t>(rect.x + 8), static_cast<int16_t>(rect.y + rect.h - 4),
                         static_cast<int16_t>(rect.w - 16), 2, accent);
        }
        gfx.drawRect(rect.x, rect.y, rect.w, rect.h, outline);
        drawText(label, static_cast<int16_t>(rect.x + 14), static_cast<int16_t>(rect.y + 20), kLabel,
                 selected(id) ? accent : foreground);
    };

    fill(background);

    const Rect readTab = makeRect(0, 0, kNavWidth, 53);
    const Rect settingsTab = makeRect(0, 53, kNavWidth, 53);
    const Rect deviceTab = makeRect(0, 106, kNavWidth, 34);
    const Rect focusTab = makeRect(0, 140, kNavWidth, height() - 140);
    drawRailTab(readTab, "Read", false);
    drawRailTab(settingsTab, "Settings", false);
    drawRailTab(deviceTab, "Device", true);
    drawRailTab(focusTab, "Focus", false);

    const Rect powerRect = makeRect(width() - kPowerSize - kPowerXInset, kPowerY, kPowerSize, kPowerSize);
    gfx.drawRoundRect(powerRect.x, powerRect.y, powerRect.w, powerRect.h, 7, outline);
    drawPowerIcon(static_cast<int16_t>(powerRect.x + (powerRect.w / 2)),
                  static_cast<int16_t>(powerRect.y + (powerRect.h / 2)), muted);

    const int16_t rightLimit = static_cast<int16_t>(powerRect.x - 10);
    const int16_t tileWidth = static_cast<int16_t>((rightLimit - kContentX - 24) / 3);
    const Rect storageRect = makeRect(kContentX, kTileY, tileWidth, kTileHeight);
    const Rect syncRect = makeRect(kContentX + tileWidth + 12, kTileY, tileWidth, kTileHeight);
    const Rect otaRect = makeRect(kContentX + (tileWidth + 12) * 2, kTileY, tileWidth, kTileHeight);
    drawTile(storageRect, "Storage", 0);
    drawTile(syncRect, "Sync", 1);
    drawTile(otaRect, "OTA Update", 2);

    Board::Display::gfx().flush();

    const Element hits[] = {
        {readTab, kMenuNavRead}, {settingsTab, kMenuNavSettings}, {deviceTab, kMenuNavDevice},
        {focusTab, kMenuNavFocus}, {powerRect, kMenuNavPower}, {storageRect, 0}, {syncRect, 1}, {otaRect, 2},
    };
    return hit(tap, hits);
}

int UiRenderer::renderFocusHub(size_t selectedIndex) {
    return renderFocusHub(selectedIndex, Tap{false, 0, 0});
}

int UiRenderer::renderFocusHub(size_t selectedIndex, Tap tap) {
    if (width() < 620 || height() < 150 || height() > 240) {
        std::vector<Button> items = {
            Button{String("RSVP Nano"), selectedIndex == 0},
            Button{String("Strength Labs"), selectedIndex == 1},
            Button{String("Self Care"), selectedIndex == 2},
            Button{String("Other"), selectedIndex == 3},
        };
        return renderMenu("Focus Timer", items, tap);
    }

    Arduino_GFX& gfx = Board::Display::gfx();
    const uint16_t background = color(DisplayTheme::ColorRole::Background);
    const uint16_t foreground = color(DisplayTheme::ColorRole::Foreground);
    const uint16_t muted = color(DisplayTheme::ColorRole::Muted);
    const uint16_t accent = color(DisplayTheme::ColorRole::Accent);
    const uint16_t outline = color(DisplayTheme::ColorRole::Outline);
    const uint16_t surface = color(DisplayTheme::ColorRole::Surface);
    const uint16_t surfaceMuted = color(DisplayTheme::ColorRole::SurfaceMuted);
    const uint16_t activeWash = blendOverBackground(accent, theme_.lowBrightness ? 42 : 22);

    constexpr uint8_t kTiny = 1;
    constexpr uint8_t kLabel = 2;
    constexpr int16_t kNavWidth = 82;
    constexpr int16_t kPowerSize = 28;
    constexpr int16_t kPowerXInset = 10;
    constexpr int16_t kPowerY = 7;
    constexpr int16_t kContentX = kNavWidth + 12;

    auto makeRect = [](int x, int y, int w, int h) {
        return Rect{static_cast<int16_t>(x), static_cast<int16_t>(y), static_cast<int16_t>(w), static_cast<int16_t>(h)};
    };
    auto selected = [&](int id) {
        return selectedIndex == static_cast<size_t>(id);
    };
    auto drawPowerIcon = [&](int16_t cx, int16_t cy, uint16_t ink) {
        gfx.drawCircle(cx, cy, 8, ink);
        gfx.fillRect(static_cast<int16_t>(cx - 3), static_cast<int16_t>(cy - 10), 7, 10, background);
        gfx.drawFastVLine(cx, static_cast<int16_t>(cy - 10), 9, ink);
    };
    auto drawRailTab = [&](Rect rect, const char* title, bool active) {
        gfx.fillRect(rect.x, rect.y, rect.w, rect.h, active ? surface : surfaceMuted);
        gfx.drawRect(rect.x, rect.y, rect.w, rect.h, outline);
        if (active) {
            gfx.fillRect(rect.x, static_cast<int16_t>(rect.y + 6), 3, static_cast<int16_t>(rect.h - 12), accent);
        }
        const int16_t labelY = static_cast<int16_t>(rect.y + ((rect.h - textHeight(kTiny)) / 2));
        drawText(title, static_cast<int16_t>(rect.x + 10), labelY, kTiny, active ? foreground : muted);
    };
    auto drawTile = [&](Rect rect, const char* label, int id) {
        if (selected(id)) {
            gfx.fillRoundRect(rect.x, rect.y, rect.w, rect.h, 6, activeWash);
            gfx.fillRect(static_cast<int16_t>(rect.x + 8), static_cast<int16_t>(rect.y + rect.h - 4),
                         static_cast<int16_t>(rect.w - 16), 2, accent);
        }
        gfx.drawRect(rect.x, rect.y, rect.w, rect.h, outline);
        drawText(label, static_cast<int16_t>(rect.x + 14), static_cast<int16_t>(rect.y + 16), kLabel,
                 selected(id) ? accent : foreground);
    };

    fill(background);

    const Rect readTab = makeRect(0, 0, kNavWidth, 53);
    const Rect settingsTab = makeRect(0, 53, kNavWidth, 53);
    const Rect deviceTab = makeRect(0, 106, kNavWidth, 34);
    const Rect focusTab = makeRect(0, 140, kNavWidth, height() - 140);
    drawRailTab(readTab, "Read", false);
    drawRailTab(settingsTab, "Settings", false);
    drawRailTab(deviceTab, "Device", false);
    drawRailTab(focusTab, "Focus", true);

    const Rect powerRect = makeRect(width() - kPowerSize - kPowerXInset, kPowerY, kPowerSize, kPowerSize);
    gfx.drawRoundRect(powerRect.x, powerRect.y, powerRect.w, powerRect.h, 7, outline);
    drawPowerIcon(static_cast<int16_t>(powerRect.x + (powerRect.w / 2)),
                  static_cast<int16_t>(powerRect.y + (powerRect.h / 2)), muted);

    const int16_t rightLimit = static_cast<int16_t>(powerRect.x - 10);
    const int16_t tileWidth = static_cast<int16_t>((rightLimit - kContentX - 12) / 2);
    const Rect nanoRect = makeRect(kContentX, 12, tileWidth, 58);
    const Rect strengthRect = makeRect(kContentX + tileWidth + 12, 12, tileWidth, 58);
    const Rect selfCareRect = makeRect(kContentX, 82, tileWidth, 58);
    const Rect otherRect = makeRect(kContentX + tileWidth + 12, 82, tileWidth, 58);
    drawTile(nanoRect, "RSVP Nano", 0);
    drawTile(strengthRect, "Strength Labs", 1);
    drawTile(selfCareRect, "Self Care", 2);
    drawTile(otherRect, "Other", 3);

    Board::Display::gfx().flush();

    const Element hits[] = {
        {readTab, kMenuNavRead}, {settingsTab, kMenuNavSettings}, {deviceTab, kMenuNavDevice},
        {focusTab, kMenuNavFocus}, {powerRect, kMenuNavPower}, {nanoRect, 0},
        {strengthRect, 1}, {selfCareRect, 2}, {otherRect, 3},
    };
    return hit(tap, hits);
}

int UiRenderer::renderMenu(const String& title, const std::vector<Button>& items) {
    return renderMenu(title, items, Tap{false, 0, 0});
}

int UiRenderer::renderMenu(const String& title, const std::vector<Button>& items, Tap tap) {
    (void) title;
    Arduino_GFX& gfx = Board::Display::gfx();
    fill(color(DisplayTheme::ColorRole::Background));
    if (items.empty()) {
        drawCenteredText("Empty", (height() - textHeight(3)) / 2, 3, color(DisplayTheme::ColorRole::Foreground));
        Board::Display::gfx().flush();
        return -1;
    }

    size_t selectedIndex = 0;
    for (size_t i = 0; i < items.size(); ++i) {
        if (items[i].selected) {
            selectedIndex = i;
            break;
        }
    }

    constexpr int16_t kRowStartY = 4;
    const int16_t rowHeight = kMenuRowHeight;
    const int16_t availableRows = std::max<int16_t>(1, static_cast<int16_t>((height() - kRowStartY - 6) / rowHeight));
    const size_t visibleCount = std::min(items.size(), static_cast<size_t>(availableRows));
    size_t firstVisible = 0;
    if (selectedIndex >= visibleCount / 2) {
        firstVisible = selectedIndex - visibleCount / 2;
    }
    if (firstVisible + visibleCount > items.size()) {
        firstVisible = items.size() - visibleCount;
    }

    const uint16_t foreground = color(DisplayTheme::ColorRole::Foreground);
    const uint16_t muted = color(DisplayTheme::ColorRole::Muted);
    const uint16_t accent = color(DisplayTheme::ColorRole::Accent);
    const uint16_t surface = color(DisplayTheme::ColorRole::Surface);
    const uint16_t active = blendOverBackground(accent, theme_.lowBrightness ? 48 : 24);

    int16_t y = kRowStartY;
    int tapped = -1;
    for (size_t row = 0; row < visibleCount; ++row, y += rowHeight) {
        const size_t itemIndex = firstVisible + row;
        const Rect rowRect{0, y, width(), rowHeight};
        if (items[itemIndex].selected) {
            gfx.fillRect(16, static_cast<int16_t>(y + 2), static_cast<int16_t>(width() - 32),
                         static_cast<int16_t>(rowHeight - 4), active);
            gfx.fillRect(18, static_cast<int16_t>(y + 6), 4, static_cast<int16_t>(rowHeight - 12), accent);
        } else {
            gfx.drawFastHLine(24, static_cast<int16_t>(y + rowHeight - 1), static_cast<int16_t>(width() - 48), surface);
        }
        const uint16_t itemColor = items[itemIndex].selected ? foreground : muted;
        const int16_t textX = items[itemIndex].selected ? 30 : 28;
        drawText(fitText(items[itemIndex].label, 2, static_cast<int16_t>(width() - textX - 18)), textX,
                 static_cast<int16_t>(y + 4), 2, itemColor);
        if (contains(rowRect, tap) && tapped < 0) {
            tapped = static_cast<int>(itemIndex);
        }
    }

    if (firstVisible > 0) {
        drawText("^", static_cast<int16_t>(width() - 24), kRowStartY, 1, accent);
    }
    if (firstVisible + visibleCount < items.size()) {
        drawText("v", static_cast<int16_t>(width() - 24), static_cast<int16_t>(height() - 12), 1, accent);
    }

    Board::Display::gfx().flush();
    return tapped;
}

int UiRenderer::renderLibrary(const std::vector<LibraryItem>& items, size_t selectedIndex, int16_t shelfOffsetPx) {
    return renderLibrary(items, selectedIndex, shelfOffsetPx, Tap{false, 0, 0}, false);
}

int UiRenderer::renderLibrary(const std::vector<LibraryItem>& items, size_t selectedIndex, int16_t shelfOffsetPx,
                              bool fastShelf) {
    return renderLibrary(items, selectedIndex, shelfOffsetPx, Tap{false, 0, 0}, fastShelf);
}

int16_t UiRenderer::libraryShelfOffsetFor(const std::vector<LibraryItem>& items, size_t index) const {
    if (items.empty()) {
        return 0;
    }
    const size_t clampedIndex = std::min(index, items.size() - 1);
    int16_t spineLeft = 0;
    for (size_t i = 0; i < clampedIndex; ++i) {
        spineLeft = static_cast<int16_t>(spineLeft + librarySpineWidthFor(items[i], i) + kLibraryGap);
    }
    const int16_t spineWidth = librarySpineWidthFor(items[clampedIndex], clampedIndex);
    return libraryShelfClampedOffset(
        items, static_cast<int16_t>(kLibraryShelfMarkerX - kLibraryViewportX - spineLeft - (spineWidth / 2)));
}

int16_t UiRenderer::libraryShelfClampedOffset(const std::vector<LibraryItem>& items, int16_t offsetPx) const {
    if (items.empty()) {
        return 0;
    }

    const int16_t firstCenter = static_cast<int16_t>(librarySpineWidthFor(items[0], 0) / 2);
    int16_t left = 0;
    int16_t lastCenter = firstCenter;
    for (size_t i = 0; i < items.size(); ++i) {
        const int16_t spineW = librarySpineWidthFor(items[i], i);
        lastCenter = static_cast<int16_t>(left + (spineW / 2));
        left = static_cast<int16_t>(left + spineW + kLibraryGap);
    }

    const int16_t maxOffset = static_cast<int16_t>(kLibraryShelfMarkerX - kLibraryViewportX - firstCenter);
    const int16_t minOffset = static_cast<int16_t>(kLibraryShelfMarkerX - kLibraryViewportX - lastCenter);
    return std::clamp<int16_t>(offsetPx, minOffset, maxOffset);
}

size_t UiRenderer::libraryShelfNearestIndex(const std::vector<LibraryItem>& items, int16_t offsetPx,
                                            int16_t screenX) const {
    if (items.empty()) {
        return 0;
    }

    const int16_t targetX = static_cast<int16_t>(screenX - kLibraryViewportX);
    size_t bestIndex = 0;
    int16_t bestDistance = INT16_MAX;
    int16_t left = 0;
    for (size_t i = 0; i < items.size(); ++i) {
        const int16_t spineW = librarySpineWidthFor(items[i], i);
        const int16_t center = static_cast<int16_t>(left + (spineW / 2) + offsetPx);
        const int16_t distance = static_cast<int16_t>(std::abs(static_cast<int>(center) - static_cast<int>(targetX)));
        if (distance < bestDistance) {
            bestDistance = distance;
            bestIndex = i;
        }
        left = static_cast<int16_t>(left + spineW + kLibraryGap);
    }
    return bestIndex;
}

bool UiRenderer::hitLibraryShelf(Tap tap) const {
    return contains({kLibraryViewportX, kLibraryViewportY, kLibraryViewportWidth, kLibraryViewportHeight}, tap);
}

void UiRenderer::renderLibraryChrome() {
    if (width() < 620 || height() < 150 || height() > 240) {
        return;
    }

    Arduino_GFX& gfx = Board::Display::gfx();
    const uint16_t background = color(DisplayTheme::ColorRole::Background);
    const uint16_t foreground = color(DisplayTheme::ColorRole::Foreground);
    const uint16_t muted = color(DisplayTheme::ColorRole::Muted);
    const uint16_t accent = color(DisplayTheme::ColorRole::Accent);
    const uint16_t outline = color(DisplayTheme::ColorRole::Outline);
    const uint16_t surface = color(DisplayTheme::ColorRole::Surface);
    const uint16_t surfaceMuted = color(DisplayTheme::ColorRole::SurfaceMuted);

    constexpr uint8_t kTiny = 1;
    constexpr int16_t kPowerSize = 28;
    constexpr int16_t kPowerXInset = 10;
    constexpr int16_t kPowerY = 7;

    auto makeRect = [](int x, int y, int w, int h) {
        return Rect{static_cast<int16_t>(x), static_cast<int16_t>(y), static_cast<int16_t>(w), static_cast<int16_t>(h)};
    };
    auto drawPowerIcon = [&](int16_t cx, int16_t cy, uint16_t ink) {
        gfx.drawCircle(cx, cy, 8, ink);
        gfx.fillRect(static_cast<int16_t>(cx - 3), static_cast<int16_t>(cy - 10), 7, 10, background);
        gfx.drawFastVLine(cx, static_cast<int16_t>(cy - 10), 9, ink);
    };
    auto drawRailTab = [&](Rect rect, const char* title, bool active) {
        gfx.fillRect(rect.x, rect.y, rect.w, rect.h, active ? surface : surfaceMuted);
        gfx.drawRect(rect.x, rect.y, rect.w, rect.h, outline);
        if (active) {
            gfx.fillRect(rect.x, static_cast<int16_t>(rect.y + 6), 3, static_cast<int16_t>(rect.h - 12), accent);
        }
        const int16_t labelY = static_cast<int16_t>(rect.y + ((rect.h - textHeight(kTiny)) / 2));
        drawText(title, static_cast<int16_t>(rect.x + 10), labelY, kTiny, active ? foreground : muted);
    };

    fill(background);

    drawRailTab(makeRect(0, 0, kLibraryNavWidth, 53), "Read", true);
    drawRailTab(makeRect(0, 53, kLibraryNavWidth, 53), "Settings", false);
    drawRailTab(makeRect(0, 106, kLibraryNavWidth, 34), "Device", false);
    drawRailTab(makeRect(0, 140, kLibraryNavWidth, height() - 140), "Focus", false);

    const Rect powerRect = makeRect(width() - kPowerSize - kPowerXInset, kPowerY, kPowerSize, kPowerSize);
    gfx.drawRoundRect(powerRect.x, powerRect.y, powerRect.w, powerRect.h, 7, outline);
    drawPowerIcon(static_cast<int16_t>(powerRect.x + (powerRect.w / 2)),
                  static_cast<int16_t>(powerRect.y + (powerRect.h / 2)), muted);

    Board::Display::flush();
}

int UiRenderer::renderLibrary(const std::vector<LibraryItem>& items, size_t selectedIndex, int16_t shelfOffsetPx, Tap tap,
                              bool fastShelf) {
    if (width() < 620 || height() < 150 || height() > 240) {
        std::vector<Button> rows;
        rows.reserve(items.size());
        for (size_t i = 0; i < items.size(); ++i) {
            rows.push_back(Button{items[i].title, i == selectedIndex});
        }
        return renderMenu("Library", rows, tap);
    }

    renderLibraryChrome();
    return renderLibraryShelfAndDetail(items, selectedIndex, shelfOffsetPx, tap, fastShelf);
}

int UiRenderer::renderLibraryShelfAndDetail(const std::vector<LibraryItem>& items, size_t selectedIndex,
                                            int16_t shelfOffsetPx, Tap tap, bool fastShelf) {
    if (width() < 620 || height() < 150 || height() > 240) {
        return renderLibrary(items, selectedIndex, shelfOffsetPx, tap, fastShelf);
    }

    Arduino_GFX& gfx = Board::Display::gfx();
    const uint16_t background = color(DisplayTheme::ColorRole::Background);
    const uint16_t foreground = color(DisplayTheme::ColorRole::Foreground);
    const uint16_t muted = color(DisplayTheme::ColorRole::Muted);
    const uint16_t accent = color(DisplayTheme::ColorRole::Accent);
    const uint16_t outline = color(DisplayTheme::ColorRole::Outline);
    const uint16_t track = color(DisplayTheme::ColorRole::ProgressTrack);
    const uint16_t paperText = DisplayTheme::rgb565(245, 240, 230);
    const uint16_t ribbon = DisplayTheme::rgb565(216, 90, 48);

    constexpr uint8_t kTiny = 1;
    constexpr uint8_t kLabel = 2;
    constexpr int16_t kPowerSize = 28;
    constexpr int16_t kPowerXInset = 10;
    constexpr int16_t kPowerY = 7;

    auto makeRect = [](int x, int y, int w, int h) {
        return Rect{static_cast<int16_t>(x), static_cast<int16_t>(y), static_cast<int16_t>(w), static_cast<int16_t>(h)};
    };

    const Rect readTab = makeRect(0, 0, kLibraryNavWidth, 53);
    const Rect settingsTab = makeRect(0, 53, kLibraryNavWidth, 53);
    const Rect deviceTab = makeRect(0, 106, kLibraryNavWidth, 34);
    const Rect focusTab = makeRect(0, 140, kLibraryNavWidth, height() - 140);
    const Rect powerRect = makeRect(width() - kPowerSize - kPowerXInset, kPowerY, kPowerSize, kPowerSize);
    const Rect shelfViewport = makeRect(kLibraryViewportX, kLibraryViewportY, kLibraryViewportWidth,
                                        kLibraryViewportHeight);
    const Rect shelfDirty = makeRect(kLibraryViewportX, static_cast<int16_t>(kLibraryViewportY - 2),
                                     kLibraryViewportWidth,
                                     static_cast<int16_t>(kLibraryViewportHeight + 4));
    const Rect detailRect = makeRect(kLibraryViewportX, kLibraryDetailY, kLibraryViewportWidth, kLibraryDetailHeight);
    const int16_t markerX = static_cast<int16_t>(shelfViewport.x + (shelfViewport.w / 2));

    gfx.fillRect(shelfDirty.x, shelfDirty.y, shelfDirty.w, shelfDirty.h, background);
    gfx.drawFastVLine(markerX, shelfViewport.y, shelfViewport.h, track);

    if (items.empty()) {
        drawText("No Library Items", static_cast<int16_t>(shelfViewport.x + 12),
                 static_cast<int16_t>(shelfViewport.y + 42), kLabel, muted);
        gfx.drawFastHLine(kLibraryViewportX, 128, kLibraryViewportWidth, outline);
        gfx.drawFastHLine(kLibraryViewportX, 129, kLibraryViewportWidth, outline);
        if (!fastShelf) {
            gfx.fillRect(detailRect.x, detailRect.y, detailRect.w, detailRect.h, background);
            Board::Display::flushRegion(detailRect.x, detailRect.y, detailRect.w, detailRect.h);
        }
        Board::Display::flushRegion(shelfDirty.x, shelfDirty.y, shelfDirty.w, shelfDirty.h);
        const Element hits[] = {{readTab, kMenuNavRead},     {settingsTab, kMenuNavSettings},
                                {deviceTab, kMenuNavDevice}, {focusTab, kMenuNavFocus},
                                {powerRect, kMenuNavPower}};
        return hit(tap, hits);
    }

    const size_t clampedSelected = std::min(selectedIndex, items.size() - 1);
    const int16_t offset = libraryShelfClampedOffset(items, shelfOffsetPx);
    const size_t displaySelected = fastShelf ? libraryShelfNearestIndex(items, offset, markerX) : clampedSelected;
    int shelfHit = -1;
    int16_t spineLeft = 0;
    for (size_t i = 0; i < items.size(); ++i) {
        const int16_t spineW = librarySpineWidthFor(items[i], i);
        const int16_t spineH = librarySpineHeightFor(items[i], i);
        const int16_t spineX = static_cast<int16_t>(shelfViewport.x + spineLeft + offset);
        spineLeft = static_cast<int16_t>(spineLeft + spineW + kLibraryGap);
        if (spineX + spineW < shelfViewport.x || spineX > shelfViewport.x + shelfViewport.w) {
            continue;
        }

        const int16_t lift = i == displaySelected ? 8 : 0;
        const int16_t spineY = static_cast<int16_t>(shelfViewport.y + shelfViewport.h - spineH - lift);
        if (contains(makeRect(spineX, spineY, spineW, spineH), tap)) {
            shelfHit = static_cast<int>(kLibraryShelfItemBase + i);
        }
        const uint16_t spineColor = librarySpineColor(i, items[i].article, accent, foreground, muted);
        const uint16_t bindingColor = libraryBindingColor(i, items[i].article);
        gfx.fillRect(spineX, spineY, spineW, spineH, spineColor);
        gfx.drawRect(spineX, spineY, spineW, spineH, foreground);

        const int16_t leftBindingX = static_cast<int16_t>(spineX + 3);
        const int16_t rightBindingX = static_cast<int16_t>(spineX + spineW - 4);
        if (spineW >= 10) {
            gfx.drawFastVLine(leftBindingX, static_cast<int16_t>(spineY + 3), static_cast<int16_t>(spineH - 6),
                              bindingColor);
            gfx.drawFastVLine(rightBindingX, static_cast<int16_t>(spineY + 3), static_cast<int16_t>(spineH - 6),
                              bindingColor);
        }

        if (items[i].article && spineH >= 24 && spineW >= 12) {
            const uint16_t bandColor = libraryArticleBandColor(i);
            gfx.fillRect(static_cast<int16_t>(spineX + 2), static_cast<int16_t>(spineY + 2),
                         static_cast<int16_t>(spineW - 4), 2, bandColor);
            gfx.fillRect(static_cast<int16_t>(spineX + 2), static_cast<int16_t>(spineY + spineH - 4),
                         static_cast<int16_t>(spineW - 4), 2, bandColor);
        }

        if (i == displaySelected) {
            gfx.fillRect(spineX, static_cast<int16_t>(spineY - 2), spineW, 2, ribbon);
        }

        if (items[i].progressPercent > 0 && spineW >= 12) {
            const int16_t ribbonW = spineW >= 30 ? 7 : 5;
            const int16_t ribbonX = static_cast<int16_t>(rightBindingX - ribbonW - 2);
            const int16_t rawRibbonH =
                static_cast<int16_t>((static_cast<int32_t>(spineH) * items[i].progressPercent) / 100);
            const int16_t ribbonH = std::min<int16_t>(spineH, std::max<int16_t>(8, rawRibbonH));
            gfx.fillRect(ribbonX, spineY, ribbonW, ribbonH, ribbon);

            if (!fastShelf) {
                constexpr int16_t kBookmarkNotchRows = 6;
                for (int16_t row = 0; row <= kBookmarkNotchRows && row < ribbonH; ++row) {
                    const int16_t notchY = static_cast<int16_t>(spineY + ribbonH - kBookmarkNotchRows - 1 + row);
                    if (notchY < spineY || notchY >= spineY + ribbonH) {
                        continue;
                    }
                    const int16_t halfWidth = std::min<int16_t>(row, static_cast<int16_t>(ribbonW / 2));
                    gfx.drawFastHLine(static_cast<int16_t>(ribbonX + (ribbonW / 2) - halfWidth), notchY,
                                      static_cast<int16_t>((halfWidth * 2) + 1), spineColor);
                }
            }
        }

        if (!fastShelf && !items[i].spineLabel.isEmpty()) {
            int16_t charY = static_cast<int16_t>(spineY + 6);
            const int16_t charX = static_cast<int16_t>(spineX + ((spineW - textWidth("W", kTiny)) / 2));
            gfx.setFont(static_cast<const GFXfont*>(nullptr));
            gfx.setTextSize(kTiny);
            gfx.setTextWrap(false);
            gfx.setTextColor(paperText);
            for (size_t c = 0; c < items[i].spineLabel.length() && charY + textHeight(kTiny) <= spineY + spineH - 4;
                 ++c) {
                gfx.setCursor(charX, charY);
                gfx.write(static_cast<uint8_t>(items[i].spineLabel[c]));
                charY = static_cast<int16_t>(charY + 11);
            }
        }
    }

    gfx.drawFastHLine(kLibraryViewportX, 128, kLibraryViewportWidth, outline);
    gfx.drawFastHLine(kLibraryViewportX, 129, kLibraryViewportWidth, outline);
    Board::Display::flushRegion(shelfDirty.x, shelfDirty.y, shelfDirty.w, shelfDirty.h);

    if (fastShelf) {
        const LibraryItem& selectedItem = items[displaySelected];
        gfx.fillRect(detailRect.x, detailRect.y, detailRect.w, detailRect.h, background);
        drawText(fitText(selectedItem.title, kLabel, detailRect.w), detailRect.x, detailRect.y, kLabel, foreground);
        Board::Display::flushRegion(detailRect.x, detailRect.y, detailRect.w, detailRect.h);
    } else {
        const LibraryItem& selectedItem = items[clampedSelected];
        gfx.fillRect(detailRect.x, detailRect.y, detailRect.w, detailRect.h, background);
        const int16_t progressX = static_cast<int16_t>(detailRect.x + detailRect.w - 62);
        drawText(fitText(selectedItem.title, kLabel, static_cast<int16_t>(progressX - detailRect.x - 12)), detailRect.x,
                 static_cast<int16_t>(detailRect.y + 0), kLabel, foreground);
        drawText(fitText(selectedItem.detailLine, kTiny, static_cast<int16_t>(progressX - detailRect.x - 12)), detailRect.x,
                 static_cast<int16_t>(detailRect.y + 18), kTiny, muted);
        drawText(selectedItem.progressLabel, progressX, static_cast<int16_t>(detailRect.y + 7), kLabel, accent);
        Board::Display::flushRegion(detailRect.x, detailRect.y, detailRect.w, detailRect.h);
    }

    const Element navHits[] = {{readTab, kMenuNavRead},     {settingsTab, kMenuNavSettings},
                               {deviceTab, kMenuNavDevice}, {focusTab, kMenuNavFocus},
                               {powerRect, kMenuNavPower},  {detailRect, kLibraryOpenSelected}};
    const int navHit = hit(tap, navHits);
    if (navHit >= 0) {
        return navHit;
    }
    if (hitLibraryShelf(tap)) {
        return shelfHit >= 0 ? shelfHit : kLibraryOpenSelected;
    }
    return -1;
}

void UiRenderer::renderStatus(const String& title, const String& line1, const String& line2) {
    fill(color(DisplayTheme::ColorRole::Background));
    drawBattery();
    drawCenteredText(fitText(title, 3, width() - 24), height() / 2 - 38, 3, color(DisplayTheme::ColorRole::Foreground));
    if (!line1.isEmpty()) {
        drawCenteredText(fitText(line1, 2, width() - 24), height() / 2 + 2, 2, color(DisplayTheme::ColorRole::Muted));
    }
    if (!line2.isEmpty()) {
        drawCenteredText(fitText(line2, 1, width() - 24), height() / 2 + 34, 1, color(DisplayTheme::ColorRole::Accent));
    }
    Board::Display::gfx().flush();
}

void UiRenderer::renderProgress(const String& title, const String& line1, const String& line2) {
    renderProgress(title, line1, line2, Slider{-1});
}

void UiRenderer::renderProgress(const String& title, const String& line1, const String& line2, Slider slider) {
    renderStatus(title, line1, line2);
    if (slider.progressPercent < 0) {
        return;
    }
    const int16_t barW = std::min<int16_t>(width() - 48, 320);
    const int16_t barX = (width() - barW) / 2;
    const int16_t barY = height() / 2 + 64;
    drawSlider({barX, barY, barW, 8}, slider);
    Board::Display::gfx().flush();
}

void UiRenderer::renderStandby() {
    renderStatus("STANDBY", "tap or press to wake", "");
}

void UiRenderer::renderStandby(const standby::Frame& frame, uint16_t columns, uint16_t rows, uint8_t cellSize) {
    Arduino_GFX& gfx = Board::Display::gfx();
    const uint16_t background = color(DisplayTheme::ColorRole::Background);

    if (frame.fullRedraw) {
        gfx.fillScreen(background);
    }

    if (!frame.cells.valid() || columns == 0 || rows == 0 || cellSize == 0) {
        Board::Display::gfx().flush();
        return;
    }

    const int16_t gridWidth = static_cast<int16_t>(columns * cellSize);
    const int16_t gridHeight = static_cast<int16_t>(rows * cellSize);
    const int16_t originX = static_cast<int16_t>((width() - gridWidth) / 2);
    const int16_t originY = static_cast<int16_t>((height() - gridHeight) / 2);
    const uint16_t dimColor = blendOverBackground(color(DisplayTheme::ColorRole::Foreground), 72);
    const uint16_t liveColor = color(DisplayTheme::ColorRole::Foreground);


    auto drawRuns = [&](standby::PackedGridView cells, uint16_t colorValue, standby::PackedGridView mask) {
        for (uint16_t row = 0; row < rows; ++row) {
            const int16_t y = static_cast<int16_t>(originY + row * cellSize);
            if (y >= height()) {
                break;
            }
            const int16_t drawY = std::max<int16_t>(0, y);
            const int16_t drawH = std::min<int16_t>(height(), y + cellSize) - drawY;
            if (drawH <= 0) {
                continue;
            }

            uint16_t column = 0;
            const size_t rowOffset = static_cast<size_t>(row) * columns;
            while (column < columns) {
                while (column < columns) {
                    const size_t index = rowOffset + column;
                    const bool selected =
                        standby::cellAlive(cells, index) && (!mask.valid() || standby::cellAlive(mask, index));
                    if (selected) {
                        break;
                    }
                    ++column;
                }
                const uint16_t runStart = column;
                while (column < columns) {
                    const size_t index = rowOffset + column;
                    const bool selected =
                        standby::cellAlive(cells, index) && (!mask.valid() || standby::cellAlive(mask, index));
                    if (!selected) {
                        break;
                    }
                    ++column;
                }
                if (runStart == column) {
                    continue;
                }

                const int16_t x = static_cast<int16_t>(originX + runStart * cellSize);
                const int16_t runEndX = static_cast<int16_t>(originX + column * cellSize);
                if (runEndX <= 0 || x >= width()) {
                    continue;
                }
                const int16_t drawX = std::max<int16_t>(0, x);
                const int16_t drawW = std::min<int16_t>(width(), runEndX) - drawX;
                if (drawW > 0) {
                    gfx.fillRect(drawX, drawY, drawW, drawH, colorValue);
                }
            }
        }
    };

    if (!frame.fullRedraw && frame.dirtyCells.valid()) {
        drawRuns(frame.dirtyCells, background, {});
        if (frame.dimCells.valid()) {
            drawRuns(frame.dimCells, dimColor, frame.dirtyCells);
        }
        drawRuns(frame.cells, liveColor, frame.dirtyCells);
        Board::Display::gfx().flush();
        return;
    }

    if (!frame.fullRedraw) {
        gfx.fillScreen(background);
    }
    if (frame.dimCells.valid()) {
        drawRuns(frame.dimCells, dimColor, {});
    }
    drawRuns(frame.cells, liveColor, {});

    Board::Display::gfx().flush();
}

bool UiRenderer::contains(Rect rect, Tap tap) const {
    return tap.active && tap.x >= rect.x && tap.y >= rect.y && tap.x < rect.x + rect.w && tap.y < rect.y + rect.h;
}

bool UiRenderer::drawButton(Rect rect, const Button& button, uint8_t textSize, Tap tap) {
    if (button.selected) {
        Board::Display::gfx().fillRect(rect.x + 8, rect.y + 2, rect.w - 16, rect.h - 2,
                                       color(DisplayTheme::ColorRole::SurfaceActive));
    }
    drawText(fitText(button.label, textSize, rect.w - 28), rect.x + 16, rect.y + 6, textSize,
             button.selected ? color(DisplayTheme::ColorRole::OnAccent) : color(DisplayTheme::ColorRole::Foreground));
    return contains(rect, tap);
}

void UiRenderer::drawSlider(Rect rect, Slider slider) {
    const int progressPercent = std::clamp(slider.progressPercent, 0, 100);
    Board::Display::gfx().drawRect(rect.x, rect.y, rect.w, rect.h, color(DisplayTheme::ColorRole::Muted));
    if (rect.w <= 2 || rect.h <= 2) {
        Board::Display::gfx().fillRect(rect.x, rect.y, static_cast<int16_t>((rect.w * progressPercent) / 100), rect.h,
                                       color(DisplayTheme::ColorRole::Accent));
        return;
    }
    Board::Display::gfx().fillRect(rect.x + 1, rect.y + 1, static_cast<int16_t>(((rect.w - 2) * progressPercent) / 100),
                                   rect.h - 2, color(DisplayTheme::ColorRole::Accent));
}

void UiRenderer::fill(uint16_t colorValue) {
    Board::Display::gfx().fillScreen(colorValue);
}

void UiRenderer::drawText(const String& text, int16_t x, int16_t y, uint8_t size, uint16_t colorValue) {
    Arduino_GFX& gfx = Board::Display::gfx();
    gfx.setFont(static_cast<const GFXfont*>(nullptr));
    gfx.setTextSize(size);
    gfx.setTextWrap(false);
    gfx.setTextColor(colorValue);
    gfx.setCursor(x, y);
    gfx.print(text);
}

void UiRenderer::drawReaderText(const String& text, int16_t x, int16_t baseline, uint16_t colorValue) {
    if (!readerText_.ready()) {
        return;
    }

    applyReaderFont();
    readerText_.setTextColor(colorValue, color(DisplayTheme::ColorRole::Background));
    readerText_.drawString(text, x, baseline);
}

void UiRenderer::drawReaderCodepoint(uint16_t codepoint, int16_t x, int16_t baseline, uint16_t colorValue) {
    if (!readerText_.ready()) {
        return;
    }

    applyReaderFont();
    readerText_.setTextColor(colorValue, color(DisplayTheme::ColorRole::Background));
    readerText_.drawCodepoint(codepoint, x, baseline);
}

void UiRenderer::drawReaderPlain(const String& text, int16_t x, int16_t y, uint16_t colorValue) {
    if (typography_.trackingPx == 0) {
        drawReaderText(text, x, y, colorValue);
        return;
    }

    int16_t cursorX = x;
    uint16_t previousCodepoint = 0;
    bool hasPrevious = false;
    for (size_t i = 0; i < text.length();) {
        uint16_t codepoint = 0;
        if (!nextCodepoint(text, i, codepoint)) {
            break;
        }

        if (hasPrevious) {
            cursorX = static_cast<int16_t>(cursorX + readerKerning(previousCodepoint, codepoint));
        }
        drawReaderCodepoint(codepoint, cursorX, y, colorValue);
        cursorX = static_cast<int16_t>(cursorX + readerCodepointAdvance(codepoint));
        if (i < text.length()) {
            cursorX = static_cast<int16_t>(cursorX + typography_.trackingPx);
        }
        previousCodepoint = codepoint;
        hasPrevious = true;
    }
}

void UiRenderer::drawReaderWord(const String& word, int16_t x, int16_t y, int focusIndex) {
    if (typography_.trackingPx == 0 && (!typography_.focusHighlight || focusIndex < 0)) {
        drawReaderText(word, x, y, color(DisplayTheme::ColorRole::Foreground));
        return;
    }

    int16_t cursorX = x;
    int glyphIndex = 0;
    uint16_t previousCodepoint = 0;
    bool hasPrevious = false;
    for (size_t i = 0; i < word.length();) {
        uint16_t codepoint = 0;
        if (!nextCodepoint(word, i, codepoint)) {
            break;
        }

        if (hasPrevious) {
            cursorX = static_cast<int16_t>(cursorX + readerKerning(previousCodepoint, codepoint));
        }
        const bool focused = typography_.focusHighlight && glyphIndex == focusIndex;
        drawReaderCodepoint(codepoint, cursorX, y,
                            focused ? color(DisplayTheme::ColorRole::Accent)
                                    : color(DisplayTheme::ColorRole::Foreground));
        cursorX = static_cast<int16_t>(cursorX + readerCodepointAdvance(codepoint));
        ++glyphIndex;
        if (i < word.length()) {
            cursorX = static_cast<int16_t>(cursorX + typography_.trackingPx);
        }
        previousCodepoint = codepoint;
        hasPrevious = true;
    }
}

void UiRenderer::drawReaderGuide(int16_t anchorX, int16_t textBaseline, int16_t wordInkTop, int16_t wordInkBottom) {
    const int16_t wordInkHeight = static_cast<int16_t>(wordInkBottom - wordInkTop + 1);
    const int16_t guidePadding = std::max<int16_t>(4, std::min<int16_t>(8, wordInkHeight / 8));
    const int16_t guideTop = std::max<int16_t>(2, static_cast<int16_t>(textBaseline + wordInkTop - guidePadding));
    const int16_t guideBottom =
        std::min<int16_t>(height() - 3, static_cast<int16_t>(textBaseline + wordInkBottom + guidePadding));
    const int16_t halfWidth = typography_.guideHalfWidth;
    const int16_t gap = typography_.guideGap;
    const uint16_t guideColor = blendOverBackground(color(DisplayTheme::ColorRole::Foreground), 96);
    const uint16_t tickColor = typography_.focusHighlight ? color(DisplayTheme::ColorRole::Accent) : guideColor;

    Board::Display::gfx().drawFastHLine(std::max<int16_t>(0, anchorX - halfWidth), guideTop,
                                        std::max<int16_t>(0, halfWidth - gap), guideColor);
    Board::Display::gfx().drawFastHLine(anchorX + gap, guideTop, std::max<int16_t>(0, halfWidth - gap + 1), guideColor);
    Board::Display::gfx().drawFastHLine(std::max<int16_t>(0, anchorX - halfWidth), guideBottom,
                                        std::max<int16_t>(0, halfWidth - gap), guideColor);
    Board::Display::gfx().drawFastHLine(anchorX + gap, guideBottom, std::max<int16_t>(0, halfWidth - gap + 1),
                                        guideColor);
    Board::Display::gfx().drawFastVLine(anchorX, guideTop, 5, tickColor);
    Board::Display::gfx().drawFastVLine(anchorX, guideBottom - 4, 5, tickColor);
}

void UiRenderer::drawReaderFooter(const String& chapterLabel, const String& statusLabel, const ReaderChrome& chrome) {
    if (!chrome.showChapter && !chrome.showProgress) {
        return;
    }

    const uint8_t textSize = kReaderChromeTextSize;
    const int16_t y = height() - textHeight(textSize) - kReaderChromeMarginBottom;
    int16_t chapterMaxWidth = width() - (kReaderChromeMarginX * 2);
    if (chrome.showProgress) {
        const String status = statusLabel.isEmpty() ? "0%" : statusLabel;
        const int16_t statusW = textWidth(status, textSize);
        const int16_t statusX = std::max<int16_t>(kReaderChromeMarginX, width() - kReaderChromeMarginX - statusW);
        chapterMaxWidth = std::max<int16_t>(0, statusX - kReaderChromeMarginX - 24);
        drawText(status, statusX, y, textSize, color(DisplayTheme::ColorRole::Muted));
    }
    if (chrome.showChapter) {
        drawText(fitText(chapterLabel.isEmpty() ? String("START") : chapterLabel, textSize, chapterMaxWidth),
                 kReaderChromeMarginX, y, textSize, color(DisplayTheme::ColorRole::Muted));
    }
}

String UiRenderer::readerDisplayText(const String& text) const {
    return text;
}

int16_t UiRenderer::readerTextWidth(const String& text) const {
    if (text.length() > 1 && typography_.trackingPx != 0) {
        int16_t widthValue = 0;
        uint16_t previousCodepoint = 0;
        bool hasPrevious = false;
        for (size_t i = 0; i < text.length();) {
            uint16_t codepoint = 0;
            if (!nextCodepoint(text, i, codepoint)) {
                break;
            }
            if (hasPrevious) {
                widthValue = static_cast<int16_t>(widthValue + readerKerning(previousCodepoint, codepoint));
            }
            widthValue = static_cast<int16_t>(widthValue + readerCodepointAdvance(codepoint));
            if (i < text.length()) {
                widthValue = static_cast<int16_t>(widthValue + typography_.trackingPx);
            }
            previousCodepoint = codepoint;
            hasPrevious = true;
        }
        return std::max<int16_t>(0, widthValue);
    }

    if (readerText_.ready()) {
        applyReaderFont();
        return readerText_.textAdvance(text);
    }

    return textWidth(text, 2);
}

int16_t UiRenderer::readerCodepointAdvance(uint16_t codepoint) const {
    if (!readerText_.ready()) {
        return 0;
    }

    applyReaderFont();
    return readerText_.glyphAdvance(codepoint);
}

int16_t UiRenderer::readerKerning(uint16_t leftCodepoint, uint16_t rightCodepoint) const {
    if (!readerText_.ready()) {
        return 0;
    }

    applyReaderFont();
    return readerText_.kerningAdjust(leftCodepoint, rightCodepoint);
}

const AlphaFont* UiRenderer::readerFont() const {
    return readerFont_ != nullptr ? readerFont_ : FontCatalog::fallbackFont(readerFontSizeIndex_);
}

void UiRenderer::applyReaderFont() const {
    readerText_.setFont(readerFont());
}

int16_t UiRenderer::readerWordStartX(const String& word, int focusIndex) const {
    const int16_t totalWidth = readerTextWidth(word);
    if (totalWidth <= 0) {
        return width() / 2;
    }
    if (focusIndex < 0) {
        return static_cast<int16_t>((width() - totalWidth) / 2);
    }

    int16_t cursorX = 0;
    int16_t focusCenterX = totalWidth / 2;
    int glyphIndex = 0;
    uint16_t previousCodepoint = 0;
    bool hasPrevious = false;
    for (size_t i = 0; i < word.length();) {
        uint16_t codepoint = 0;
        if (!nextCodepoint(word, i, codepoint)) {
            break;
        }

        if (hasPrevious) {
            cursorX = static_cast<int16_t>(cursorX + readerKerning(previousCodepoint, codepoint));
        }
        const int16_t glyphAdvance = readerCodepointAdvance(codepoint);
        if (glyphIndex == focusIndex) {
            focusCenterX = static_cast<int16_t>(cursorX + (glyphAdvance / 2));
            break;
        }

        cursorX = static_cast<int16_t>(cursorX + glyphAdvance);
        ++glyphIndex;
        if (i < word.length()) {
            cursorX = static_cast<int16_t>(cursorX + typography_.trackingPx);
        }
        previousCodepoint = codepoint;
        hasPrevious = true;
    }

    const int16_t anchorX = static_cast<int16_t>((width() * typography_.anchorPercent) / 100);
    const int16_t x = static_cast<int16_t>(anchorX - focusCenterX);
    const int16_t minX = kReaderSideMargin;
    const int16_t maxX = static_cast<int16_t>(width() - kReaderSideMargin - totalWidth);
    return maxX < minX ? x : std::clamp<int16_t>(x, minX, maxX);
}

int UiRenderer::findFocusLetterIndex(const String& word) const {
    int wordCharacterCount = 0;
    for (size_t i = 0; i < word.length();) {
        uint16_t codepoint = 0;
        if (!nextCodepoint(word, i, codepoint)) {
            break;
        }
        if (isReaderWordCodepoint(codepoint)) {
            ++wordCharacterCount;
        }
    }

    if (wordCharacterCount == 0) {
        return word.length() > 0 ? 0 : -1;
    }

    const int targetOrdinal = std::min(orpOrdinalForLength(wordCharacterCount), wordCharacterCount - 1);
    int currentOrdinal = 0;
    int glyphIndex = 0;
    for (size_t i = 0; i < word.length();) {
        uint16_t codepoint = 0;
        if (!nextCodepoint(word, i, codepoint)) {
            break;
        }
        if (isReaderWordCodepoint(codepoint)) {
            if (currentOrdinal == targetOrdinal) {
                return glyphIndex;
            }
            ++currentOrdinal;
        }
        ++glyphIndex;
    }

    return 0;
}

void UiRenderer::drawCenteredText(const String& text, int16_t y, uint8_t size, uint16_t colorValue) {
    drawText(text, std::max<int16_t>(0, (width() - textWidth(text, size)) / 2), y, size, colorValue);
}

UiRenderer::Rect UiRenderer::batteryRect() const {
    return {static_cast<int16_t>(std::max<int16_t>(0, width() - kBatteryTapWidth)), 0, kBatteryTapWidth,
            kBatteryTapHeight};
}

void UiRenderer::drawBattery() {
    if (batteryPercent_ == 0 && batteryVoltage_ <= 0.0f) {
        return;
    }

    const uint8_t textSize = kReaderChromeTextSize;
    const String label = batteryShowVoltage_ && batteryVoltage_ > 0.0f ? String(batteryVoltage_, 2) + "V"
                                                                       : String(batteryPercent_) + "%";
    const int16_t labelW = textWidth(label, textSize);
    const int16_t totalW = static_cast<int16_t>(kBatteryIconWidth + kBatteryCapWidth + 7 + labelW);
    const int16_t x = std::max<int16_t>(4, width() - totalW - 10);
    const int16_t iconY = 10;
    const int16_t textY = 6;
    const uint16_t outlineColor = color(DisplayTheme::ColorRole::Muted);
    const uint16_t labelColor = color(DisplayTheme::ColorRole::Muted);
    const uint16_t fillColor = batteryFillColor(batteryPercent_, batteryCharging_);

    Arduino_GFX& gfx = Board::Display::gfx();
    gfx.drawRect(x, iconY, kBatteryIconWidth, kBatteryIconHeight, outlineColor);
    gfx.fillRect(static_cast<int16_t>(x + kBatteryIconWidth), static_cast<int16_t>(iconY + 4), kBatteryCapWidth, 5,
                 outlineColor);

    const int16_t innerW = kBatteryIconWidth - 4;
    const int16_t fillW = batteryCharging_ ? innerW : static_cast<int16_t>((innerW * batteryPercent_) / 100);
    if (fillW > 0) {
        gfx.fillRect(static_cast<int16_t>(x + 2), static_cast<int16_t>(iconY + 2), fillW,
                     static_cast<int16_t>(kBatteryIconHeight - 4), fillColor);
    }

    if (batteryCharging_) {
        const int16_t boltX = static_cast<int16_t>(x + 10);
        const int16_t boltY = static_cast<int16_t>(iconY + 2);
        const uint16_t boltColor = color(DisplayTheme::ColorRole::Background);
        gfx.drawLine(boltX + 5, boltY, boltX + 1, boltY + 5, boltColor);
        gfx.drawLine(boltX + 1, boltY + 5, boltX + 6, boltY + 5, boltColor);
        gfx.drawLine(boltX + 6, boltY + 5, boltX + 2, boltY + 10, boltColor);
    }

    drawText(label, static_cast<int16_t>(x + kBatteryIconWidth + kBatteryCapWidth + 7), textY, textSize, labelColor);
}

String UiRenderer::fitText(const String& text, uint8_t size, int16_t maxWidth) const {
    if (textWidth(text, size) <= maxWidth) {
        return text;
    }
    String result = text;
    while (result.length() > 1 && textWidth(result + "...", size) > maxWidth) {
        result.remove(result.length() - 1);
    }
    return result + "...";
}

uint16_t UiRenderer::color(DisplayTheme::ColorRole role) const {
    const size_t index = static_cast<size_t>(role);
    if (index >= theme_.colors.size()) {
        return role == DisplayTheme::ColorRole::Background ? kBlack : kWhite;
    }
    return theme_.colors[index];
}

uint16_t UiRenderer::blendOverBackground(uint16_t colorValue, uint8_t alpha) const {
    if (alpha >= 255) {
        return colorValue;
    }
    const uint16_t bg = color(DisplayTheme::ColorRole::Background);
    const uint8_t fgR = static_cast<uint8_t>((colorValue >> 11) & 0x1F);
    const uint8_t fgG = static_cast<uint8_t>((colorValue >> 5) & 0x3F);
    const uint8_t fgB = static_cast<uint8_t>(colorValue & 0x1F);
    const uint8_t bgR = static_cast<uint8_t>((bg >> 11) & 0x1F);
    const uint8_t bgG = static_cast<uint8_t>((bg >> 5) & 0x3F);
    const uint8_t bgB = static_cast<uint8_t>(bg & 0x1F);
    const uint8_t r = static_cast<uint8_t>((fgR * alpha + bgR * (255 - alpha)) / 255);
    const uint8_t g = static_cast<uint8_t>((fgG * alpha + bgG * (255 - alpha)) / 255);
    const uint8_t b = static_cast<uint8_t>((fgB * alpha + bgB * (255 - alpha)) / 255);
    return static_cast<uint16_t>((r << 11) | (g << 5) | b);
}

int16_t UiRenderer::width() const {
    return Board::Display::gfx().width();
}

int16_t UiRenderer::height() const {
    return Board::Display::gfx().height();
}
