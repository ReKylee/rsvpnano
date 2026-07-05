#if !defined(LITTLE_FOOT_PRINT)

#ifndef RSVP_NANO_ROW_PREFIX_CANVAS_H
#define RSVP_NANO_ROW_PREFIX_CANVAS_H

#include <Arduino_GFX.h>

class RowPrefixCanvas : public Arduino_GFX
{
public:
  RowPrefixCanvas(int16_t w, int16_t h, Arduino_G *output, int16_t output_x = 0, int16_t output_y = 0, uint8_t rotation = 0);
  ~RowPrefixCanvas();

  bool begin(int32_t speed = GFX_NOT_DEFINED) override;
  void writePixelPreclipped(int16_t x, int16_t y, uint16_t color) override;
  void writeFastVLine(int16_t x, int16_t y, int16_t h, uint16_t color) override;
  void writeFastVLineCore(int16_t x, int16_t y, int16_t h, uint16_t color);
  void writeFastHLine(int16_t x, int16_t y, int16_t w, uint16_t color) override;
  void writeFastHLineCore(int16_t x, int16_t y, int16_t w, uint16_t color);
  void writeFillRectPreclipped(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) override;
  void drawIndexedBitmap(int16_t x, int16_t y, uint8_t *bitmap, uint16_t *color_index, int16_t w, int16_t h, int16_t x_skip = 0) override;
  void drawIndexedBitmap(int16_t x, int16_t y, uint8_t *bitmap, uint16_t *color_index, uint8_t chroma_key, int16_t w, int16_t h, int16_t x_skip = 0) override;
  void draw16bitRGBBitmap(int16_t x, int16_t y, uint16_t *bitmap, int16_t w, int16_t h) override;
  void draw16bitRGBBitmapWithTranColor(int16_t x, int16_t y, uint16_t *bitmap, uint16_t transparent_color, int16_t w, int16_t h) override;
  void draw16bitBeRGBBitmap(int16_t x, int16_t y, uint16_t *bitmap, int16_t w, int16_t h) override;
  void flush(bool force_flush = false) override;

  void flushQuad(bool force_flush = false);
  void shade(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t shade_mask);
  uint16_t *getFramebuffer();
  void markDirtyRect(int16_t x, int16_t y, int16_t w, int16_t h);
  bool hasDirty() const;

protected:
  uint16_t *_framebuffer = nullptr;
  Arduino_G *_output = nullptr;
  int16_t _output_x, _output_y;
  int16_t MAX_X, MAX_Y;

  // for flushQuad() only
  uint16_t *_rowBuf = nullptr;

private:
  int16_t dirtyNativeRowEnd() const;
  void clearDirty();

  int16_t _dirty_left = -1;
  int16_t _dirty_top = -1;
  int16_t _dirty_right = -1;
  int16_t _dirty_bottom = -1;
};

#endif // RSVP_NANO_ROW_PREFIX_CANVAS_H

#endif // !defined(LITTLE_FOOT_PRINT)
