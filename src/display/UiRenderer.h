#pragma once

#include <Arduino.h>
#include <vector>

#include "display/DisplayTheme.h"
#include "fonts/AlphaFont.h"
#include "fonts/FontCatalog.h"
#include "standby/ScreensaverTypes.h"

class UiRenderer {
public:
    struct Rect {
        int16_t x = 0;
        int16_t y = 0;
        int16_t w = 0;
        int16_t h = 0;

        constexpr Rect() = default;
        constexpr Rect(int16_t xValue, int16_t yValue, int16_t widthValue, int16_t heightValue) :
                x(xValue),
                y(yValue),
                w(widthValue),
                h(heightValue) {}
    };

    struct Tap {
        bool active = false;
        uint16_t x = 0;
        uint16_t y = 0;

        constexpr Tap() {};
        constexpr Tap(bool isActive, uint16_t xValue, uint16_t yValue) : active(isActive), x(xValue), y(yValue) {}
    };

    struct Element {
        Rect rect;
        int id = -1;

        constexpr Element() = default;
        constexpr Element(Rect area, int elementId) : rect(area), id(elementId) {}
    };

    struct Button {
        String label;
        bool selected = false;
    };

    struct LibraryItem {
        String title;
        String author;
        String chapter;
        String detailLine;
        String progressLabel;
        String spineLabel;
        uint8_t progressPercent = 0;
        bool article = false;
    };

    struct Slider {
        int progressPercent = -1;

        constexpr Slider() = default;
        constexpr Slider(int progress) : progressPercent(progress) {}
    };

    static constexpr int kMenuNavRead = 100;
    static constexpr int kMenuNavSettings = 101;
    static constexpr int kMenuNavDevice = 102;
    static constexpr int kMenuNavFocus = 103;
    static constexpr int kMenuNavPower = 104;
    static constexpr int kLibraryOpenSelected = 107;
    static constexpr int kLibraryShelfItemBase = 1000;

    struct TypographyConfig {
        bool focusHighlight = true;
        int8_t trackingPx = 0;
        uint8_t anchorPercent = 35;
        uint8_t guideHalfWidth = 20;
        uint8_t guideGap = 4;
    };

    struct ReaderChrome {
        bool showBattery = true;
        bool showChapter = true;
        bool showProgress = true;
        bool showPreviousSentenceHint = true;
    };

    UiRenderer();

    bool begin();
    void setTheme(const DisplayTheme::Theme& theme);
    void setFontCatalog(FontCatalog* catalog);
    void setBrightness(uint8_t percent);
    void setBatteryStatus(uint8_t percent, float voltage, bool charging, bool showVoltage);
    bool hitBattery(Tap tap) const;
    void setReaderFont(uint8_t typefaceIndex, uint8_t sizeIndex);
    void setTypographyConfig(const TypographyConfig& config);
    void sleep();
    void wake();
    void clearToBackground();

    uint8_t readerTypefaceCount() const;
    static uint8_t readerFontSizeCount();
    const char* readerTypefaceLabel(uint8_t index) const;
    static const char* readerFontSizeLabel(uint8_t index);

    void renderReader(const String& beforeText, const String& word, const String& afterText, const String& chapterLabel,
                      uint8_t progressPercent, const String& footerStatusLabel, bool showFooter,
                      const String& overlayText, ReaderChrome chrome);
    int renderMenu(const String& title, const std::vector<Button>& items);
    int renderMenu(const String& title, const std::vector<Button>& items, Tap tap);
    int renderMainMenu(const String& currentBookTitle, uint8_t progressPercent, size_t selectedIndex);
    int renderMainMenu(const String& currentBookTitle, uint8_t progressPercent, size_t selectedIndex, Tap tap);
    int renderSettingsHub(size_t selectedIndex);
    int renderSettingsHub(size_t selectedIndex, Tap tap);
    int renderDeviceHub(size_t selectedIndex);
    int renderDeviceHub(size_t selectedIndex, Tap tap);
    int renderFocusHub(size_t selectedIndex);
    int renderFocusHub(size_t selectedIndex, Tap tap);
    int renderLibrary(const std::vector<LibraryItem>& items, size_t selectedIndex, int16_t shelfOffsetPx);
    int renderLibrary(const std::vector<LibraryItem>& items, size_t selectedIndex, int16_t shelfOffsetPx,
                      bool fastShelf);
    int renderLibrary(const std::vector<LibraryItem>& items, size_t selectedIndex, int16_t shelfOffsetPx, Tap tap,
                      bool fastShelf = false);
    void renderLibraryChrome();
    int renderLibraryShelfAndDetail(const std::vector<LibraryItem>& items, size_t selectedIndex, int16_t shelfOffsetPx,
                                    Tap tap = Tap(), bool fastShelf = false);
    int16_t libraryShelfOffsetFor(const std::vector<LibraryItem>& items, size_t index) const;
    int16_t libraryShelfClampedOffset(const std::vector<LibraryItem>& items, int16_t offsetPx) const;
    size_t libraryShelfNearestIndex(const std::vector<LibraryItem>& items, int16_t offsetPx, int16_t screenX) const;
    bool hitLibraryShelf(Tap tap) const;
    void renderStatus(const String& title, const String& line1 = "", const String& line2 = "");
    void renderProgress(const String& title, const String& line1, const String& line2);
    void renderProgress(const String& title, const String& line1, const String& line2, Slider slider);
    void renderStandby();
    void renderStandby(const standby::Frame& frame, uint16_t columns, uint16_t rows, uint8_t cellSize);

    template<size_t N>
    int hit(Tap tap, const Element (&elements)[N]) const {
        for (const Element& element: elements) {
            if (contains(element.rect, tap)) {
                return element.id;
            }
        }
        return -1;
    }

    int16_t width() const;
    int16_t height() const;

private:
    bool contains(Rect rect, Tap tap) const;
    bool drawButton(Rect rect, const Button& button, uint8_t textSize, Tap tap);
    void drawSlider(Rect rect, Slider slider);
    void fill(uint16_t color);
    void drawText(const String& text, int16_t x, int16_t y, uint8_t size, uint16_t color);
    void drawReaderText(const String& text, int16_t x, int16_t baseline, uint16_t color);
    void drawReaderCodepoint(uint16_t codepoint, int16_t x, int16_t baseline, uint16_t color);
    void drawReaderPlain(const String& text, int16_t x, int16_t y, uint16_t color);
    void drawReaderWord(const String& word, int16_t x, int16_t y, int focusIndex);
    void drawReaderGuide(int16_t anchorX, int16_t textBaseline, int16_t wordInkTop, int16_t wordInkBottom);
    void drawReaderFooter(const String& chapterLabel, const String& statusLabel, const ReaderChrome& chrome);
    String readerDisplayText(const String& text) const;
    int16_t readerTextWidth(const String& text) const;
    int16_t readerCodepointAdvance(uint16_t codepoint) const;
    int16_t readerKerning(uint16_t leftCodepoint, uint16_t rightCodepoint) const;
    const AlphaFont* readerFont() const;
    void applyReaderFont() const;
    int16_t readerWordStartX(const String& word, int focusIndex) const;
    int findFocusLetterIndex(const String& word) const;
    void drawCenteredText(const String& text, int16_t y, uint8_t size, uint16_t color);
    Rect batteryRect() const;
    void drawBattery();
    String fitText(const String& text, uint8_t size, int16_t maxWidth) const;
    uint16_t color(DisplayTheme::ColorRole role) const;
    uint16_t blendOverBackground(uint16_t colorValue, uint8_t alpha) const;

    DisplayTheme::Theme theme_ = DisplayTheme::defaultTheme();
    TypographyConfig typography_;
    uint8_t batteryPercent_ = 0;
    float batteryVoltage_ = 0.0f;
    bool batteryCharging_ = false;
    bool batteryShowVoltage_ = false;
    mutable AlphaTextRenderer<640> readerText_;
    FontCatalog* fontCatalog_ = nullptr;
    const AlphaFont* readerFont_ = nullptr;
    uint8_t readerTypefaceIndex_ = 0;
    uint8_t readerFontSizeIndex_ = 0;
};
