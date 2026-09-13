#pragma once
#include <cstdint>

class Arduino_GFX {
public:
    explicit Arduino_GFX(int16_t width = 640, int16_t height = 172) : width_(width), height_(height) {}
    int16_t width() const { return width_; }
    int16_t height() const { return height_; }
    void draw16bitRGBBitmap(int16_t, int16_t, uint16_t*, int16_t, int16_t) {}
private:
    int16_t width_, height_;
};
class Arduino_Canvas : public Arduino_GFX {
public:
    void setRotation(uint8_t) {}
    void setTextBound(int16_t, int16_t, int16_t, int16_t) {}
    uint16_t* getFramebuffer() { return nullptr; }
};
