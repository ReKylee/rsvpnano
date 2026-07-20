#include "board/BoardDisplay.h"

#include "board/BacklightBrightness.h"

#include "platforms/waveshare_c6_touch_lcd_147/WaveshareC6TouchLcd147.h"

namespace {

    Arduino_ESP32SPI gBus(WaveshareC6TouchLcd147::DisplayWiring::kDcPin, WaveshareC6TouchLcd147::DisplayWiring::kCsPin,
                          WaveshareC6TouchLcd147::DisplayWiring::kSclkPin,
                          WaveshareC6TouchLcd147::DisplayWiring::kMosiPin,
                          WaveshareC6TouchLcd147::DisplayWiring::kMisoPin);

    Arduino_ST7789 gPanel(&gBus, WaveshareC6TouchLcd147::DisplayWiring::kResetPin, 0, true,
                          WaveshareC6TouchLcd147::DisplayWiring::kPanelWidth,
                          WaveshareC6TouchLcd147::DisplayWiring::kPanelHeight,
                          WaveshareC6TouchLcd147::DisplayWiring::kColumnOffset,
                          WaveshareC6TouchLcd147::DisplayWiring::kRowOffset);

    constexpr uint8_t kMinimumBacklightDuty = Board::Backlight::kDefaultMinimumDuty;

    uint8_t gBacklightDuty = Board::Backlight::dutyFromPercent(100, kMinimumBacklightDuty);
    bool gBacklight = true;

    void writeBacklight() {
        if constexpr (WaveshareC6TouchLcd147::DisplayWiring::kBacklightPin < 0) {
            return;
        }
        analogWriteResolution(WaveshareC6TouchLcd147::DisplayWiring::kBacklightPin, 8);
        analogWriteFrequency(WaveshareC6TouchLcd147::DisplayWiring::kBacklightPin, 20000);
        analogWrite(WaveshareC6TouchLcd147::DisplayWiring::kBacklightPin, gBacklight ? gBacklightDuty : 0);
    }

} // namespace

namespace Board::Display {

    bool begin() {
        const bool ok = gPanel.begin();
        gPanel.fillScreen(0x0000);
        writeBacklight();
        return ok;
    }
    Arduino_GFX& gfx() {
        return gPanel;
    }

    ui::Orientation defaultUiOrientation() {
        return WaveshareC6TouchLcd147::DisplayWiring::kDefaultUiOrientation;
    }
    ui::Orientation rotatedUiOrientation() {
        return ui::opposite(defaultUiOrientation());
    }
    uint16_t nativeWidth() {
        return WaveshareC6TouchLcd147::DisplayWiring::kPanelWidth;
    }
    uint16_t nativeHeight() {
        return WaveshareC6TouchLcd147::DisplayWiring::kPanelHeight;
    }
    size_t txChunkBytes() {
        return WaveshareC6TouchLcd147::DisplayWiring::kTxChunkBytes;
    }
    void setBacklight(bool on) {
        gBacklight = on;
        writeBacklight();
    }
    void setBrightness(uint8_t percent) {
        gBacklightDuty = Board::Backlight::dutyFromPercent(percent, kMinimumBacklightDuty);
        writeBacklight();
    }
    void sleep() {
        setBacklight(false);
        gPanel.displayOff();
    }
    void wake() {
        gPanel.displayOn();
        setBacklight(true);
    }
    bool pushColors(uint16_t x, uint16_t y, uint16_t width, uint16_t height, const uint16_t* data) {
        gPanel.draw16bitRGBBitmap(x, y, const_cast<uint16_t*>(data), width, height);
        return true;
    }

} // namespace Board::Display
