#include "screensavers/PackedGrid.h"

#include <algorithm>
namespace standby {

    uint32_t advanceRng(uint32_t& rng) {
        rng ^= rng << 13U;
        rng ^= rng >> 17U;
        rng ^= rng << 5U;
        if (rng == 0) {
            rng = 0xA5A5A5A5UL;
        }
        return rng;
    }

    size_t packedWordCount(size_t cellCount) {
        return (cellCount + kPackedBitsPerWord - 1U) / kPackedBitsPerWord;
    }

    void clearPackedGrid(PackedGridStorage& cells, size_t wordCount) {
        std::ranges::fill_n(cells.begin(), std::min(wordCount, cells.size()), 0U);
    }

    bool cellAlive(PackedGridView cells, size_t index) {
        const size_t word = index / kPackedBitsPerWord;
        if (word >= cells.size()) {
            return false;
        }
        return (cells[word] & (1UL << (index % kPackedBitsPerWord))) != 0;
    }

    bool anyCellAlive(PackedGridView cells) {
        return std::ranges::any_of(cells, [](uint32_t word) {
            return word != 0;
        });
    }

    void setCell(PackedGridStorage& cells, size_t index, bool alive) {
        const size_t word = index / kPackedBitsPerWord;
        if (word >= cells.size()) {
            return;
        }
        const uint32_t mask = 1UL << (index % kPackedBitsPerWord);
        if (alive) {
            cells[word] |= mask;
        } else {
            cells[word] &= ~mask;
        }
    }

    void setCellAt(PackedGridStorage& cells, uint16_t columns, uint16_t rows, int x, int y, bool alive) {
        if (x < 0 || y < 0 || x >= static_cast<int>(columns) || y >= static_cast<int>(rows)) {
            return;
        }
        setCell(cells, static_cast<size_t>(y) * columns + static_cast<size_t>(x), alive);
    }

    PackedGridView viewOf(const PackedGridStorage& cells, size_t wordCount) {
        return std::span{cells}.first(std::min(wordCount, cells.size()));
    }

} // namespace standby
