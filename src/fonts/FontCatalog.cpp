#include "fonts/FontCatalog.h"

#include <algorithm>
#include <cctype>
#include <iterator>
#include <limits>
#include <ranges>
#include <span>

#include "board/BoardStorage.h"
#include "fonts/LiterataFallbackAlpha4.h"
#include "storage/fs/StorageFiles.h"
#include "storage/fs/StoragePaths.h"

namespace {

    constexpr const char* kFallbackId = "literata";
    constexpr const char* kFallbackLabel = "Literata";
    constexpr size_t kInvalidIndex = std::numeric_limits<size_t>::max();
    constexpr uint32_t kMaxRuntimeFontBytes = 2UL * 1024UL * 1024UL;

    const std::array<const ui::fonts::AlphaFont*, RFont4::kSizeCount> kFallbackFonts = {
        &ui::fonts::LiterataFallbackAlpha4_52,
        &ui::fonts::LiterataFallbackAlpha4_43,
        &ui::fonts::LiterataFallbackAlpha4_33,
    };

    std::string_view fileName(std::string_view path) {
        const size_t separator = path.find_last_of('/');
        return separator == std::string_view::npos ? path : path.substr(separator + 1);
    }

    bool whitespace(unsigned char character) {
        return std::isspace(character);
    }

    bool fits(uint32_t offset, uint32_t bytes, uint32_t total) {
        return offset <= total && bytes <= total - offset;
    }

    template<typename T, size_t Extent>
    std::expected<void, std::string> readSection(File& file, uint32_t offset, std::span<T, Extent> out) {
        if (out.empty())
            return {};
        if (!file.seek(offset))
            return std::unexpected(std::string{"Font section seek failed"});
        const size_t bytes = out.size_bytes();
        if (file.read(reinterpret_cast<uint8_t*>(out.data()), bytes) != bytes)
            return std::unexpected(std::string{"Font section read failed"});
        return {};
    }

    template<typename FileRecord, typename RuntimeRecord, typename Transform>
    std::expected<void, std::string> readRecords(File& file, uint32_t offset, size_t count,
                                                 std::vector<RuntimeRecord>& out, Transform transform) {
        std::vector<FileRecord> records(count);
        if (auto read = readSection(file, offset, std::span{records}); !read)
            return read;
        out.resize(count);
        std::ranges::transform(records, out.begin(), transform);
        return {};
    }

    std::expected<RFont4::Header, std::string> readHeader(File& file) {
        RFont4::Header header;
        if (!file.seek(0) || file.read(reinterpret_cast<uint8_t*>(&header), sizeof(header)) != sizeof(header))
            return std::unexpected(std::string{"Font header read failed"});
        return header;
    }

    std::expected<void, std::string> validateHeaderLayout(const RFont4::Header& header, size_t fileSize) {
        if (header.magic != RFont4::kMagic || header.version != RFont4::kVersion)
            return std::unexpected(std::string{"Unsupported font format"});
        if (header.headerSize < sizeof(RFont4::Header) || header.glyphRecordSize != sizeof(RFont4::GlyphRecord)
            || header.rowRecordSize != sizeof(RFont4::RowRecord) || header.spanRecordSize != sizeof(RFont4::SpanRecord)
            || header.kerningRecordSize != sizeof(RFont4::KerningRecord)) {
            return std::unexpected(std::string{"Font record layout mismatch"});
        }
        if (header.pageMapSize != RFont4::kPageMapBytes
            || header.pageTableSize != header.pageTableCount * RFont4::kPageTableEntries * sizeof(uint16_t)) {
            return std::unexpected(std::string{"Font page table layout mismatch"});
        }
        if (header.totalSize == 0 || header.totalSize > fileSize || header.totalSize > kMaxRuntimeFontBytes) {
            return std::unexpected(std::string{"Invalid font size"});
        }

        const uint32_t total = header.totalSize;
        if (!fits(header.nameOffset, header.nameSize, total) || !fits(header.bitmapOffset, header.bitmapSize, total)
            || !fits(header.glyphsOffset, header.glyphCount * sizeof(RFont4::GlyphRecord), total)
            || !fits(header.rowsOffset, header.rowCount * sizeof(RFont4::RowRecord), total)
            || !fits(header.spansOffset, header.spanCount * sizeof(RFont4::SpanRecord), total)
            || !fits(header.pageMapOffset, header.pageMapSize, total)
            || !fits(header.pageTablesOffset, header.pageTableSize, total)
            || !fits(header.kerningOffset, header.kerningPairCount * sizeof(RFont4::KerningRecord), total)) {
            return std::unexpected(std::string{"Font sections are out of bounds"});
        }
        if (header.nameSize == 0 || header.glyphCount == 0 || header.yAdvance == 0) {
            return std::unexpected(std::string{"Font is missing required metadata"});
        }
        return {};
    }

} // namespace

FontCatalog::FontCatalog() {
    reset();
}

void FontCatalog::reset() {
    clearRuntimeFont();
    loadedFamilyIndex_ = kInvalidIndex;
    loadedSizeIndex_ = kInvalidIndex;
    families_ = {{kFallbackId, kFallbackLabel, {}, true}};
}

void FontCatalog::loadFromSd() {
    reset();
    StorageFiles::ensureDirectory(StoragePaths::kFontsPath);

    File root = Board::Storage::filesystem().open(StoragePaths::kFontsPath);
    if (!root || !root.isDirectory()) {
        if (root) {
            root.close();
        }
        return;
    }

    while (true) {
        File entry = root.openNextFile();
        if (!entry) {
            break;
        }
        if (!entry.isDirectory()) {
            entry.close();
            continue;
        }

        Family family;
        family.label.assign(fileName(entry.path()));
        family.id = normalizeId(family.label);
        if (family.id.empty() || family.id == kFallbackId) {
            entry.close();
            continue;
        }

        File sizeEntry = entry.openNextFile();
        while (sizeEntry) {
            if (!sizeEntry.isDirectory()) {
                std::string_view filename = fileName(sizeEntry.path());
                if (RFont4::hasFontExtension(filename))
                    filename.remove_suffix(std::string_view{RFont4::kExtension}.size());
                const size_t size = RFont4::sizeIndexForId(filename);
                if (size < family.paths.size())
                    family.paths[size] = sizeEntry.path();
            }
            sizeEntry.close();
            sizeEntry = entry.openNextFile();
        }

        if (std::ranges::any_of(family.paths,
                                [](const std::string& path) {
                                    return !path.empty();
                                })
            && find(family.id) == nullptr)
            families_.push_back(std::move(family));
        entry.close();
    }
    root.close();

    std::ranges::sort(families_.begin() + 1, families_.end(), {}, &Family::label);
}

const FontCatalog::Family* FontCatalog::find(std::string_view id) const {
    const auto findId = [this](std::string_view value) {
        return std::ranges::find_if(families_, [value](const Family& family) {
            return family.id == value;
        });
    };
    auto found = findId(id);
    if (found == families_.end())
        found = findId(normalizeId(id));
    return found == families_.end() ? nullptr : &*found;
}

const ui::fonts::AlphaFont* FontCatalog::load(size_t familyIndex, size_t sizeIndex) {
    const size_t safeFamily = std::min(familyIndex, families_.size() - 1);
    const size_t safeSize = std::min(sizeIndex, RFont4::kSizeCount - 1);
    const Family& family = families_[safeFamily];

    if (!family.builtIn && !family.paths[safeSize].empty()) {
        if (loadedFamilyIndex_ == safeFamily && loadedSizeIndex_ == safeSize && runtimeFont_.glyphs != nullptr)
            return &runtimeFont_;
        auto loaded = loadRuntimeFont(family.paths[safeSize]);
        if (loaded) {
            loadedFamilyIndex_ = safeFamily;
            loadedSizeIndex_ = safeSize;
            return &runtimeFont_;
        }
        Serial.printf("[font] load failed %s: %s\n", family.paths[safeSize].c_str(), loaded.error().c_str());
    }

    clearRuntimeFont();
    loadedFamilyIndex_ = safeFamily;
    loadedSizeIndex_ = safeSize;
    return kFallbackFonts[safeSize];
}

std::string FontCatalog::normalizeId(std::string_view value) {
    while (!value.empty() && whitespace(value.front()))
        value.remove_prefix(1);
    while (!value.empty() && whitespace(value.back()))
        value.remove_suffix(1);
    std::string out;
    out.reserve(value.size());
    bool previousDash = false;
    for (const unsigned char character: value) {
        if (std::isalnum(character)) {
            out += std::tolower(character);
            previousDash = false;
        } else if (!previousDash && !out.empty()) {
            out.push_back('-');
            previousDash = true;
        }
    }
    if (!out.empty() && out.back() == '-')
        out.pop_back();
    return out;
}

std::expected<void, std::string> FontCatalog::validateFontFile(const String& path) {
    File file = Board::Storage::filesystem().open(path, FILE_READ);
    if (!file || file.isDirectory()) {
        if (file)
            file.close();
        return std::unexpected(std::string{"Font file unavailable"});
    }
    auto header = readHeader(file);
    auto result = header ? validateHeaderLayout(*header, file.size())
                         : std::expected<void, std::string>{std::unexpected(header.error())};
    file.close();
    return result;
}

std::expected<void, std::string> FontCatalog::loadRuntimeFont(const std::string& path) {
    File file = Board::Storage::filesystem().open(path.c_str(), FILE_READ);
    if (!file || file.isDirectory()) {
        if (file)
            file.close();
        return std::unexpected(std::string{"Font file unavailable"});
    }

    auto header = readHeader(file);
    if (!header) {
        file.close();
        clearRuntimeFont();
        return std::unexpected(header.error());
    }
    if (auto valid = validateHeaderLayout(*header, file.size()); !valid) {
        file.close();
        clearRuntimeFont();
        return valid;
    }
    if (auto loaded = loadRecords(file, *header); !loaded) {
        file.close();
        clearRuntimeFont();
        return loaded;
    }
    file.close();
    runtimeFont_ = {
        .name = runtimeName_,
        .bitmap = bitmap_.data(),
        .glyphs = glyphs_.data(),
        .glyphCount = header->glyphCount,
        .yAdvance = header->yAdvance,
        .ascent = header->ascent,
        .descent = header->descent,
        .rows = rows_.data(),
        .spans = spans_.data(),
        .pageMap = pageMap_.data(),
        .pageTables = pageTablePointers_.data(),
        .pageTableCount = header->pageTableCount,
        .kerningPairs = kerningPairs_.data(),
        .kerningPairCount = header->kerningPairCount,
        .wordInkTop = header->wordInkTop,
        .wordInkBottom = header->wordInkBottom,
    };
    return {};
}

void FontCatalog::clearRuntimeFont() {
    runtimeName_ = "";
    bitmap_.clear();
    glyphs_.clear();
    rows_.clear();
    spans_.clear();
    pageMap_.fill(0xFF);
    pageTableData_.clear();
    pageTablePointers_.clear();
    kerningPairs_.clear();
    runtimeFont_ = {};
}

std::expected<void, std::string> FontCatalog::loadRecords(File& file, const RFont4::Header& header) {
    clearRuntimeFont();

    std::vector<uint8_t> nameBytes(header.nameSize);
    if (auto read = readSection(file, header.nameOffset, std::span{nameBytes}); !read)
        return read;
    runtimeName_.assign(reinterpret_cast<const char*>(nameBytes.data()), nameBytes.size());

    bitmap_.resize(header.bitmapSize);
    if (auto read = readSection(file, header.bitmapOffset, std::span{bitmap_}); !read)
        return read;

    if (auto read = readRecords<RFont4::GlyphRecord>(
            file, header.glyphsOffset, header.glyphCount, glyphs_, [](const RFont4::GlyphRecord& item) {
                return ui::fonts::AlphaGlyph{item.codepoint, item.bitmapOffset, item.rowOffset, item.kernOffset,
                                             item.width,     item.height,       item.rowStride, item.xAdvance,
                                             item.xOffset,   item.yOffset,      item.kernCount};
            });
        !read)
        return read;
    if (auto read = readRecords<RFont4::RowRecord>(
            file, header.rowsOffset, header.rowCount, rows_, [](const RFont4::RowRecord& item) {
                return ui::fonts::AlphaRow{item.spanOffset, item.spanCount};
            });
        !read)
        return read;
    if (auto read = readRecords<RFont4::SpanRecord>(
            file, header.spansOffset, header.spanCount, spans_, [](const RFont4::SpanRecord& item) {
                return ui::fonts::AlphaSpan{item.x, item.width};
            });
        !read)
        return read;
    if (auto read = readRecords<RFont4::KerningRecord>(
            file, header.kerningOffset, header.kerningPairCount, kerningPairs_, [](const RFont4::KerningRecord& item) {
                return ui::fonts::AlphaKerningPair{item.rightCodepoint, item.xAdjust};
            });
        !read)
        return read;

    if (auto read = readSection(file, header.pageMapOffset, std::span{pageMap_}); !read)
        return read;

    const size_t pageEntryCount = header.pageTableCount * RFont4::kPageTableEntries;
    pageTableData_.resize(pageEntryCount);
    if (auto read = readSection(file, header.pageTablesOffset, std::span{pageTableData_}); !read)
        return read;
    pageTablePointers_.resize(header.pageTableCount);
    const uint16_t* page = pageTableData_.data();
    std::ranges::generate(pageTablePointers_, [&page] {
        const uint16_t* current = page;
        page += RFont4::kPageTableEntries;
        return current;
    });
    return {};
}
