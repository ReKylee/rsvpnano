#include "text/TextShaping.h"

#include <algorithm>
#include <cstdlib>
#if defined(ESP32) && defined(BOARD_HAS_PSRAM)
#include <esp_heap_caps.h>
#endif
#include <limits>

namespace TextShaping {
    namespace {

        constexpr int16_t fromFixed26_6(hb_position_t value) {
            const int64_t rounded =
                value < 0 ? -(-static_cast<int64_t>(value) + 32) / 64 : (static_cast<int64_t>(value) + 32) / 64;
            return static_cast<int16_t>(std::clamp<int64_t>(rounded, INT16_MIN, INT16_MAX));
        }

    } // namespace

    Shaper::~Shaper() {
        close();
    }

    std::expected<void, std::string> Shaper::open(File& file, const RFont4::Header& header,
                                                  std::span<const RFont4::LayoutTableRecord> tables) {
        close();
        if (!file || file.isDirectory() || tables.empty() || tables.size() > RFont4::kMaximumLayoutTableCount)
            return std::unexpected("Font shaping data unavailable");

        file_ = &file;
        tables_ = tables;
        face_ = hb_face_create_for_tables(referenceTable, this, nullptr);
        if (face_ == hb_face_get_empty()) {
            close();
            return std::unexpected("Could not create font layout face");
        }
        hb_face_set_upem(face_, header.unitsPerEm);
        hb_face_set_glyph_count(face_, header.sourceGlyphCount);
        font_ = hb_font_create(face_);
        buffer_ = hb_buffer_create();
        if (font_ == hb_font_get_empty() || buffer_ == hb_buffer_get_empty()) {
            close();
            return std::unexpected("Could not allocate font shaping state");
        }
        return {};
    }

    void Shaper::close() {
        if (buffer_ != nullptr)
            hb_buffer_destroy(buffer_);
        if (font_ != nullptr)
            hb_font_destroy(font_);
        if (face_ != nullptr)
            hb_face_destroy(face_);
        for (hb_blob_t* blob: tableBlobs_) {
            if (blob != nullptr)
                hb_blob_destroy(blob);
        }
        file_ = nullptr;
        renderer_ = nullptr;
        buffer_ = nullptr;
        font_ = nullptr;
        face_ = nullptr;
        pixelsPerEm_ = 0;
        tables_ = {};
        tableBlobs_ = {};
    }

    std::expected<int16_t, std::string> Shaper::shape(std::string_view paragraph, size_t offset, size_t length,
                                                      bool rightToLeft, std::string_view language,
                                                      ui::fonts::AlphaTextRenderer<640>& renderer,
                                                      std::vector<ui::fonts::PositionedGlyph>& output) {
        const uint8_t pixelsPerEm = renderer.pixelsPerEm();
        if (!ready() || pixelsPerEm == 0)
            return std::unexpected("Font shaping is unavailable");
        if (offset > paragraph.size() || length > paragraph.size() - offset
            || paragraph.size() > static_cast<size_t>(std::numeric_limits<int>::max())
            || offset > static_cast<size_t>(std::numeric_limits<unsigned>::max())
            || length > static_cast<size_t>(std::numeric_limits<int>::max()))
            return std::unexpected("Text run is too large to shape");

        if (renderer_ != &renderer) {
            hb_font_set_funcs(font_, fontFunctions(), &renderer, nullptr);
            renderer_ = &renderer;
        }
        if (pixelsPerEm_ != pixelsPerEm) {
            hb_font_set_scale(font_, static_cast<int>(pixelsPerEm) * 64, static_cast<int>(pixelsPerEm) * 64);
            pixelsPerEm_ = pixelsPerEm;
        }
        hb_buffer_clear_contents(buffer_);
        hb_buffer_set_direction(buffer_, rightToLeft ? HB_DIRECTION_RTL : HB_DIRECTION_LTR);
        if (!language.empty())
            hb_buffer_set_language(buffer_,
                                   hb_language_from_string(language.data(), static_cast<int>(language.size())));
        hb_buffer_add_utf8(buffer_, paragraph.data(), static_cast<int>(paragraph.size()), static_cast<unsigned>(offset),
                           static_cast<int>(length));
        hb_buffer_guess_segment_properties(buffer_);
        if (!hb_buffer_allocation_successful(buffer_))
            return std::unexpected("Could not allocate shaping buffer");
        hb_shape(font_, buffer_, nullptr, 0);

        unsigned count = 0;
        const hb_glyph_info_t* info = hb_buffer_get_glyph_infos(buffer_, &count);
        const hb_glyph_position_t* positions = hb_buffer_get_glyph_positions(buffer_, &count);
        const size_t firstGlyph = output.size();
        output.reserve(firstGlyph + count);
        int32_t advance = 0;
        for (unsigned index = 0; index < count; ++index) {
            uint16_t glyphIndex = 0;
            if (!renderer.resolveGlyphId(info[index].codepoint, glyphIndex)) {
                output.resize(firstGlyph);
                return std::unexpected("Shaped glyph is absent from RFont4");
            }
            const int16_t xAdvance = fromFixed26_6(positions[index].x_advance);
            output.push_back({
                .cluster = info[index].cluster,
                .glyphIndex = glyphIndex,
                .xAdvance = xAdvance,
                .xOffset = fromFixed26_6(positions[index].x_offset),
                .yOffset = fromFixed26_6(positions[index].y_offset),
            });
            advance += xAdvance;
        }
        return static_cast<int16_t>(std::clamp<int32_t>(advance, 0, INT16_MAX));
    }

    hb_blob_t* Shaper::referenceTable(hb_face_t*, hb_tag_t tag, void* userData) {
        return static_cast<Shaper*>(userData)->loadTable(tag);
    }

    hb_blob_t* Shaper::loadTable(hb_tag_t tag) {
        const auto table = std::ranges::find_if(tables_, [tag](const RFont4::LayoutTableRecord& candidate) {
            return candidate.tag == tag;
        });
        if (table == tables_.end() || !file_)
            return nullptr;
        const size_t index = static_cast<size_t>(table - tables_.begin());
        if (tableBlobs_[index] != nullptr)
            return hb_blob_reference(tableBlobs_[index]);
#if defined(ESP32) && defined(BOARD_HAS_PSRAM)
        auto* bytes = static_cast<char*>(
            heap_caps_malloc(table->size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
#else
        auto* bytes = static_cast<char*>(std::malloc(table->size));
#endif
        File& file = *file_;
        if (bytes == nullptr || !file.seek(table->offset)
            || file.read(reinterpret_cast<uint8_t*>(bytes), table->size) != table->size) {
            std::free(bytes);
            return nullptr;
        }
        tableBlobs_[index] = hb_blob_create_or_fail(bytes, table->size, HB_MEMORY_MODE_WRITABLE, bytes, std::free);
        return tableBlobs_[index] == nullptr ? nullptr : hb_blob_reference(tableBlobs_[index]);
    }

    hb_bool_t Shaper::nominalGlyph(hb_font_t*, void* fontData, hb_codepoint_t codepoint, hb_codepoint_t* glyph, void*) {
        return static_cast<ui::fonts::AlphaTextRenderer<640>*>(fontData)->nominalGlyph(codepoint, *glyph);
    }

    hb_position_t Shaper::glyphAdvance(hb_font_t*, void* fontData, hb_codepoint_t glyph, void*) {
        return static_cast<ui::fonts::AlphaTextRenderer<640>*>(fontData)->glyphIdAdvance(glyph) * 64;
    }

    hb_font_funcs_t* Shaper::fontFunctions() {
        static hb_font_funcs_t* functions = [] {
            hb_font_funcs_t* created = hb_font_funcs_create();
            hb_font_funcs_set_nominal_glyph_func(created, nominalGlyph, nullptr, nullptr);
            hb_font_funcs_set_glyph_h_advance_func(created, glyphAdvance, nullptr, nullptr);
            hb_font_funcs_make_immutable(created);
            return created;
        }();
        return functions;
    }

} // namespace TextShaping
