#pragma once

#include <FS.h>
#include <hb.h>

#include <array>
#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "fonts/AlphaFont.h"
#include "fonts/RFont4Format.h"

namespace TextShaping {

    class Shaper {
    public:
        Shaper() = default;
        ~Shaper();

        Shaper(const Shaper&) = delete;
        Shaper& operator=(const Shaper&) = delete;
        Shaper(Shaper&&) = delete;
        Shaper& operator=(Shaper&&) = delete;

        std::expected<void, std::string> open(File& file, const RFont4::Header& header,
                                              std::span<const RFont4::LayoutTableRecord> tables);
        void close();
        bool ready() const {
            return font_ != nullptr;
        }
        std::expected<int16_t, std::string> shape(std::string_view paragraph, size_t offset, size_t length,
                                                  bool rightToLeft, std::string_view language,
                                                  ui::fonts::AlphaTextRenderer<640>& renderer,
                                                  std::vector<ui::fonts::PositionedGlyph>& output);

    private:
        static hb_blob_t* referenceTable(hb_face_t*, hb_tag_t tag, void* userData);
        static hb_bool_t nominalGlyph(hb_font_t*, void* fontData, hb_codepoint_t codepoint, hb_codepoint_t* glyph,
                                      void*);
        static hb_position_t glyphAdvance(hb_font_t*, void* fontData, hb_codepoint_t glyph, void*);
        static hb_font_funcs_t* fontFunctions();
        hb_blob_t* loadTable(hb_tag_t tag);

        File* file_ = nullptr;
        std::span<const RFont4::LayoutTableRecord> tables_;
        std::array<hb_blob_t*, RFont4::kMaximumLayoutTableCount> tableBlobs_{};
        ui::fonts::AlphaTextRenderer<640>* renderer_ = nullptr;
        hb_face_t* face_ = nullptr;
        hb_font_t* font_ = nullptr;
        hb_buffer_t* buffer_ = nullptr;
        uint8_t pixelsPerEm_ = 0;
    };

} // namespace TextShaping
