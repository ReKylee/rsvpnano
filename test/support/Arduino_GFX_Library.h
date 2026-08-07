#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>

struct GFXfont {};

class Arduino_GFX {
public:
    explicit Arduino_GFX(int16_t width = 320, int16_t height = 172) : width_(width), height_(height) {}
    virtual ~Arduino_GFX() = default;

    virtual int16_t width() const {
        return width_;
    }
    virtual int16_t height() const {
        return height_;
    }
    virtual void setRotation(uint8_t rotation) {
        if (((rotation_ ^ rotation) & 1U) != 0)
            std::swap(width_, height_);
        rotation_ = rotation;
    }
    virtual void fillScreen(uint16_t) {
        ++writes;
    }
    virtual void fillRect(int16_t, int16_t, int16_t, int16_t, uint16_t color) {
        ++writes;
        lastFillColor = color;
    }
    virtual void drawRect(int16_t, int16_t, int16_t, int16_t, uint16_t) {
        ++writes;
    }
    virtual void fillRoundRect(int16_t, int16_t, int16_t, int16_t, int16_t, uint16_t) {
        ++writes;
    }
    virtual void drawRoundRect(int16_t, int16_t, int16_t, int16_t, int16_t, uint16_t) {
        ++writes;
    }
    virtual void drawFastHLine(int16_t, int16_t, int16_t, uint16_t) {
        ++writes;
        ++horizontalLines;
    }
    virtual void drawFastVLine(int16_t x, int16_t, int16_t height, uint16_t) {
        ++writes;
        ++verticalLines;
        maxVerticalX = std::max(maxVerticalX, x);
        if (verticalLines == 1)
            firstVerticalHeight = height;
        else if (verticalLines == 2)
            secondVerticalHeight = height;
        lastVerticalHeight = height;
    }
    virtual void drawCircle(int16_t x, int16_t y, int16_t, uint16_t) {
        ++writes;
        recordCircle(x, y);
    }
    virtual void fillCircle(int16_t x, int16_t y, int16_t, uint16_t) {
        ++writes;
        recordCircle(x, y);
    }
    virtual void drawLine(int16_t, int16_t, int16_t, int16_t, uint16_t) {
        ++writes;
    }
    virtual void setFont(const GFXfont*) {}
    virtual void setFont(const uint8_t*) {}
    virtual void setUTF8Print(bool) {}
    virtual void setTextSize(uint8_t size) {
        lastTextSize = size;
    }
    virtual void setTextWrap(bool) {}
    virtual void setTextColor(uint16_t) {
        ++transparentTextColors;
    }
    virtual void setTextColor(uint16_t, uint16_t) {
        ++opaqueTextColors;
    }
    virtual void setCursor(int16_t x, int16_t y) {
        cursorX = x;
        cursorY = y;
    }
    virtual void draw16bitRGBBitmap(int16_t, int16_t, uint16_t*, int16_t, int16_t) {
        ++writes;
        ++bitmapWrites;
    }
    virtual size_t write(uint8_t) {
        ++writes;
        ++textWrites;
        return 1;
    }
    virtual void flush(bool = false) {
        ++flushes;
    }

    int writes = 0;
    int textWrites = 0;
    int bitmapWrites = 0;
    int transparentTextColors = 0;
    int opaqueTextColors = 0;
    uint8_t lastTextSize = 0;
    int flushes = 0;
    int horizontalLines = 0;
    int verticalLines = 0;
    int16_t maxVerticalX = 0;
    int16_t firstVerticalHeight = 0;
    int16_t secondVerticalHeight = 0;
    int16_t lastVerticalHeight = 0;
    int circleWrites = 0;
    int16_t firstCircleX = 0;
    int16_t firstCircleY = 0;
    int16_t lastCircleX = 0;
    int16_t lastCircleY = 0;
    int16_t cursorX = 0;
    int16_t cursorY = 0;
    uint16_t lastFillColor = 0;
    uint8_t rotation_ = 0;

private:
    void recordCircle(int16_t x, int16_t y) {
        if (circleWrites++ == 0) {
            firstCircleX = x;
            firstCircleY = y;
        }
        lastCircleX = x;
        lastCircleY = y;
    }

    int16_t width_;
    int16_t height_;
};
