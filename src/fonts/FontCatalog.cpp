#include "fonts/FontCatalog.h"

#include <algorithm>
#include <cstring>

#include "board/BoardStorage.h"
#include "fonts/LiterataFallbackAlpha4.h"
#include "storage/fs/StorageFiles.h"
#include "storage/fs/StoragePaths.h"

namespace {

    constexpr const char* kFallbackId = "literata";
    constexpr const char* kFallbackLabel = "Literata";
    constexpr uint8_t kInvalidIndex = 0xFF;
    constexpr uint32_t kMaxRuntimeFontBytes = 2UL * 1024UL * 1024UL;

    const std::array<const AlphaFont*, RFont4::kSizeCount> kFallbackFonts = {
        &LiterataFallbackAlpha4_52,
        &LiterataFallbackAlpha4_43,
        &LiterataFallbackAlpha4_33,
    };

    String basename(String path) {
        const int separator = path.lastIndexOf('/');
        if (separator >= 0) {
            path = path.substring(static_cast<unsigned int>(separator + 1));
        }
        return path;
    }

    bool checkedEnd(uint32_t offset, uint32_t bytes, uint32_t total, uint32_t& end) {
        if (offset > total || bytes > total - offset) {
            return false;
        }
        end = offset + bytes;
        return true;
    }

    template <typename T>
    bool readArray(File& file, uint32_t offset, std::vector<T>& out, size_t count, String& error) {
        out.clear();
        if (count == 0) {
            return true;
        }
        out.resize(count);
        const size_t bytes = sizeof(T) * count;
        if (!file.seek(offset)) {
            error = "Font section seek failed";
            return false;
        }
        const size_t read = file.read(reinterpret_cast<uint8_t*>(out.data()), bytes);
        if (read != bytes) {
            error = "Font section read failed";
            out.clear();
            return false;
        }
        return true;
    }

    bool validateHeaderLayout(const RFont4::Header& header, size_t fileSize, String& error) {
        if (header.magic != RFont4::kMagic || header.version != RFont4::kVersion) {
            error = "Unsupported font format";
            return false;
        }
        if (header.headerSize < sizeof(RFont4::Header)
            || header.glyphRecordSize != sizeof(RFont4::GlyphRecord)
            || header.rowRecordSize != sizeof(RFont4::RowRecord)
            || header.spanRecordSize != sizeof(RFont4::SpanRecord)
            || header.kerningRecordSize != sizeof(RFont4::KerningRecord)) {
            error = "Font record layout mismatch";
            return false;
        }
        if (header.pageMapSize != RFont4::kPageMapBytes
            || header.pageTableSize != header.pageTableCount * RFont4::kPageTableEntries * sizeof(uint16_t)) {
            error = "Font page table layout mismatch";
            return false;
        }
        if (header.totalSize == 0 || header.totalSize > fileSize || header.totalSize > kMaxRuntimeFontBytes) {
            error = "Invalid font size";
            return false;
        }

        const uint32_t total = header.totalSize;
        uint32_t end = 0;
        if (!checkedEnd(header.nameOffset, header.nameSize, total, end)
            || !checkedEnd(header.bitmapOffset, header.bitmapSize, total, end)
            || !checkedEnd(header.glyphsOffset, header.glyphCount * sizeof(RFont4::GlyphRecord), total, end)
            || !checkedEnd(header.rowsOffset, header.rowCount * sizeof(RFont4::RowRecord), total, end)
            || !checkedEnd(header.spansOffset, header.spanCount * sizeof(RFont4::SpanRecord), total, end)
            || !checkedEnd(header.pageMapOffset, header.pageMapSize, total, end)
            || !checkedEnd(header.pageTablesOffset, header.pageTableSize, total, end)
            || !checkedEnd(header.kerningOffset, header.kerningPairCount * sizeof(RFont4::KerningRecord), total, end)) {
            error = "Font sections are out of bounds";
            return false;
        }
        if (header.nameSize == 0 || header.glyphCount == 0 || header.yAdvance == 0) {
            error = "Font is missing required metadata";
            return false;
        }
        return true;
    }

} // namespace

FontCatalog::FontCatalog() {
    reset();
}

void FontCatalog::reset() {
    loaded_.clear();
    loadedTypefaceIndex_ = kInvalidIndex;
    loadedSizeIndex_ = kInvalidIndex;
    families_.clear();
    Family fallback;
    fallback.id = kFallbackId;
    fallback.label = kFallbackLabel;
    fallback.builtIn = true;
    families_.push_back(fallback);
}

void FontCatalog::loadFromSd() {
    const String selectedId = typefaceId(loadedTypefaceIndex_ == kInvalidIndex ? 0 : loadedTypefaceIndex_);
    reset();
    StorageFiles::ensureDirectory(StoragePaths::kFontsPath, "fonts");

    File root = Board::Storage::filesystem().open(StoragePaths::kFontsPath);
    if (!root || !root.isDirectory()) {
        if (root) {
            root.close();
        }
        return;
    }

    std::vector<Family> loaded;
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
        family.label = basename(entry.path());
        family.id = normalizeId(family.label);
        if (family.id.isEmpty() || family.id == kFallbackId) {
            entry.close();
            continue;
        }

        File sizeEntry = entry.openNextFile();
        while (sizeEntry) {
            if (!sizeEntry.isDirectory()) {
                const String filename = basename(sizeEntry.path());
                String lower = filename;
                lower.toLowerCase();
                for (size_t i = 0; i < RFont4::kSizeCount; ++i) {
                    if (lower == RFont4::sizeFilename(static_cast<uint8_t>(i))) {
                        family.paths[i] = sizeEntry.path();
                        break;
                    }
                }
            }
            sizeEntry.close();
            sizeEntry = entry.openNextFile();
        }

        const bool hasAnySize = std::any_of(family.paths.begin(), family.paths.end(), [](const String& path) {
            return !path.isEmpty();
        });
        if (hasAnySize) {
            loaded.push_back(family);
        }
        entry.close();
    }
    root.close();

    std::sort(loaded.begin(), loaded.end(), [](const Family& a, const Family& b) {
        return std::strcmp(a.label.c_str(), b.label.c_str()) < 0;
    });
    for (const Family& family : loaded) {
        uint8_t unused = 0;
        if (!indexForId(family.id, unused)) {
            families_.push_back(family);
        }
    }

    uint8_t restored = 0;
    if (indexForId(selectedId, restored)) {
        loadedTypefaceIndex_ = restored;
    }
}

uint8_t FontCatalog::typefaceCount() const {
    return static_cast<uint8_t>(std::min<size_t>(families_.size(), 255));
}

const char* FontCatalog::typefaceLabel(uint8_t index) const {
    return families_[safeTypefaceIndex(index)].label.c_str();
}

String FontCatalog::typefaceId(uint8_t index) const {
    return families_[safeTypefaceIndex(index)].id;
}

bool FontCatalog::hasSize(uint8_t typefaceIndex, uint8_t sizeIndex) const {
    const Family& family = families_[safeTypefaceIndex(typefaceIndex)];
    return family.builtIn || !family.paths[safeSizeIndex(sizeIndex)].isEmpty();
}

bool FontCatalog::indexForId(const String& id, uint8_t& index) const {
    const String normalized = normalizeId(id);
    if (normalized == "standard") {
        index = 0;
        return true;
    }
    for (size_t i = 0; i < families_.size() && i <= 255; ++i) {
        if (families_[i].id == normalized) {
            index = static_cast<uint8_t>(i);
            return true;
        }
    }
    return false;
}

const AlphaFont* FontCatalog::loadFont(uint8_t typefaceIndex, uint8_t sizeIndex) {
    const uint8_t safeTypeface = static_cast<uint8_t>(safeTypefaceIndex(typefaceIndex));
    const uint8_t safeSize = safeSizeIndex(sizeIndex);
    const Family& family = families_[safeTypeface];

    if (!family.builtIn && !family.paths[safeSize].isEmpty()) {
        if (loadedTypefaceIndex_ == safeTypeface && loadedSizeIndex_ == safeSize && loaded_.font() != nullptr) {
            return loaded_.font();
        }
        if (loadRuntimeFont(family.paths[safeSize])) {
            loadedTypefaceIndex_ = safeTypeface;
            loadedSizeIndex_ = safeSize;
            return loaded_.font();
        }
    }

    loaded_.clear();
    loadedTypefaceIndex_ = safeTypeface;
    loadedSizeIndex_ = safeSize;
    return fallbackFont(safeSize);
}

const AlphaFont* FontCatalog::currentFont() const {
    if (loaded_.font() != nullptr) {
        return loaded_.font();
    }
    return fallbackFont(loadedSizeIndex_ == kInvalidIndex ? 0 : loadedSizeIndex_);
}

const AlphaFont* FontCatalog::fallbackFont(uint8_t sizeIndex) {
    return kFallbackFonts[std::min<size_t>(sizeIndex, kFallbackFonts.size() - 1)];
}

String FontCatalog::normalizeId(const String& value) {
    String id = value;
    id.trim();
    id.toLowerCase();
    String out;
    bool previousDash = false;
    for (size_t i = 0; i < id.length(); ++i) {
        const char c = id[i];
        const bool word = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
        if (word) {
            out += c;
            previousDash = false;
        } else if (!previousDash && out.length() > 0) {
            out += '-';
            previousDash = true;
        }
    }
    while (out.endsWith("-")) {
        out.remove(out.length() - 1);
    }
    if (out == "opendyslexic" || out == "open-dyslexic") {
        return "opendyslexic";
    }
    if (out == "atkinson" || out == "atkinson-hyperlegible") {
        return "atkinson-hyperlegible";
    }
    if (out == "standard") {
        return "literata";
    }
    return out;
}

bool FontCatalog::validateFontFile(const String& path, String& error) {
    File file = Board::Storage::filesystem().open(path, FILE_READ);
    if (!file || file.isDirectory()) {
        if (file) {
            file.close();
        }
        error = "Font file unavailable";
        return false;
    }
    RFont4::Header header;
    if (file.read(reinterpret_cast<uint8_t*>(&header), sizeof(header)) != sizeof(header)) {
        file.close();
        error = "Font header read failed";
        return false;
    }
    const bool ok = validateHeaderLayout(header, static_cast<size_t>(file.size()), error);
    file.close();
    return ok;
}

size_t FontCatalog::safeTypefaceIndex(uint8_t index) const {
    if (families_.empty()) {
        return 0;
    }
    return std::min<size_t>(index, families_.size() - 1);
}

uint8_t FontCatalog::safeSizeIndex(uint8_t index) const {
    return static_cast<uint8_t>(std::min<size_t>(index, RFont4::kSizeCount - 1));
}

bool FontCatalog::loadRuntimeFont(const String& path) {
    String error;
    if (loaded_.load(path, error)) {
        loadError_ = "";
        return true;
    }
    loadError_ = error;
    Serial.printf("[font] load failed %s: %s\n", path.c_str(), error.c_str());
    return false;
}

bool FontCatalog::RuntimeFont::load(const String& path, String& error) {
    File file = Board::Storage::filesystem().open(path, FILE_READ);
    if (!file || file.isDirectory()) {
        if (file) {
            file.close();
        }
        error = "Font file unavailable";
        return false;
    }

    RFont4::Header header;
    if (!readHeader(file, header, error) || !validateHeaderLayout(header, static_cast<size_t>(file.size()), error)
        || !loadRecords(file, header, error)) {
        file.close();
        clear();
        return false;
    }
    file.close();
    rebuildFont(header);
    valid_ = true;
    return true;
}

void FontCatalog::RuntimeFont::clear() {
    name_ = "";
    bitmap_.clear();
    glyphs_.clear();
    rows_.clear();
    spans_.clear();
    pageMap_.fill(0xFF);
    pageTableData_.clear();
    pageTablePointers_.clear();
    kerningPairs_.clear();
    font_ = AlphaFont{};
    valid_ = false;
}

const AlphaFont* FontCatalog::RuntimeFont::font() const {
    return valid_ ? &font_ : nullptr;
}

bool FontCatalog::RuntimeFont::readHeader(File& file, RFont4::Header& header, String& error) const {
    if (!file.seek(0) || file.read(reinterpret_cast<uint8_t*>(&header), sizeof(header)) != sizeof(header)) {
        error = "Font header read failed";
        return false;
    }
    return true;
}

bool FontCatalog::RuntimeFont::readBytes(File& file, uint32_t offset, uint8_t* target, size_t bytes, String& error) const {
    if (bytes == 0) {
        return true;
    }
    if (!file.seek(offset)) {
        error = "Font section seek failed";
        return false;
    }
    const size_t read = file.read(target, bytes);
    if (read != bytes) {
        error = "Font section read failed";
        return false;
    }
    return true;
}

bool FontCatalog::RuntimeFont::loadRecords(File& file, const RFont4::Header& header, String& error) {
    clear();

    std::vector<uint8_t> nameBytes;
    if (!readArray(file, header.nameOffset, nameBytes, header.nameSize, error)) {
        return false;
    }
    nameBytes.push_back(0);
    name_ = reinterpret_cast<const char*>(nameBytes.data());

    bitmap_.resize(header.bitmapSize);
    if (!readBytes(file, header.bitmapOffset, bitmap_.data(), bitmap_.size(), error)) {
        return false;
    }

    std::vector<RFont4::GlyphRecord> fileGlyphs;
    std::vector<RFont4::RowRecord> fileRows;
    std::vector<RFont4::SpanRecord> fileSpans;
    std::vector<RFont4::KerningRecord> fileKerning;
    if (!readArray(file, header.glyphsOffset, fileGlyphs, header.glyphCount, error)
        || !readArray(file, header.rowsOffset, fileRows, header.rowCount, error)
        || !readArray(file, header.spansOffset, fileSpans, header.spanCount, error)
        || !readArray(file, header.kerningOffset, fileKerning, header.kerningPairCount, error)) {
        return false;
    }

    glyphs_.reserve(fileGlyphs.size());
    for (const RFont4::GlyphRecord& item : fileGlyphs) {
        AlphaGlyph glyph;
        glyph.codepoint = item.codepoint;
        glyph.bitmapOffset = item.bitmapOffset;
        glyph.rowOffset = item.rowOffset;
        glyph.kernOffset = item.kernOffset;
        glyph.width = item.width;
        glyph.height = item.height;
        glyph.rowStride = item.rowStride;
        glyph.xAdvance = item.xAdvance;
        glyph.xOffset = item.xOffset;
        glyph.yOffset = item.yOffset;
        glyph.kernCount = item.kernCount;
        glyphs_.push_back(glyph);
    }

    rows_.reserve(fileRows.size());
    for (const RFont4::RowRecord& item : fileRows) {
        rows_.push_back(AlphaRow{item.spanOffset, item.spanCount});
    }

    spans_.reserve(fileSpans.size());
    for (const RFont4::SpanRecord& item : fileSpans) {
        spans_.push_back(AlphaSpan{item.x, item.width});
    }

    kerningPairs_.reserve(fileKerning.size());
    for (const RFont4::KerningRecord& item : fileKerning) {
        kerningPairs_.push_back(AlphaKerningPair{item.rightCodepoint, item.xAdjust});
    }

    if (!readBytes(file, header.pageMapOffset, pageMap_.data(), pageMap_.size(), error)) {
        return false;
    }

    const size_t pageEntryCount = static_cast<size_t>(header.pageTableCount) * RFont4::kPageTableEntries;
    pageTableData_.resize(pageEntryCount);
    if (!readBytes(file, header.pageTablesOffset, reinterpret_cast<uint8_t*>(pageTableData_.data()),
                   pageTableData_.size() * sizeof(uint16_t), error)) {
        return false;
    }
    pageTablePointers_.reserve(header.pageTableCount);
    for (size_t i = 0; i < header.pageTableCount; ++i) {
        pageTablePointers_.push_back(pageTableData_.data() + i * RFont4::kPageTableEntries);
    }
    return true;
}

void FontCatalog::RuntimeFont::rebuildFont(const RFont4::Header& header) {
    font_.name = name_.c_str();
    font_.bitmap = bitmap_.data();
    font_.glyphs = glyphs_.data();
    font_.glyphCount = header.glyphCount;
    font_.yAdvance = header.yAdvance;
    font_.ascent = header.ascent;
    font_.descent = header.descent;
    font_.rows = rows_.data();
    font_.spans = spans_.data();
    font_.pageMap = pageMap_.data();
    font_.pageTables = pageTablePointers_.data();
    font_.pageTableCount = static_cast<uint8_t>(std::min<uint16_t>(header.pageTableCount, 255));
    font_.kerningPairs = kerningPairs_.data();
    font_.kerningPairCount = header.kerningPairCount;
    font_.wordInkTop = header.wordInkTop;
    font_.wordInkBottom = header.wordInkBottom;
}
