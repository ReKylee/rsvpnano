#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace standby {

    constexpr uint16_t kMaxStandbyColumns = 160;
    constexpr uint16_t kMaxStandbyRows = 64;
    constexpr size_t kMaxStandbyCells = static_cast<size_t>(kMaxStandbyColumns) * kMaxStandbyRows;
    constexpr size_t kPackedBitsPerWord = 32;
    constexpr size_t kMaxStandbyWords = (kMaxStandbyCells + kPackedBitsPerWord - 1U) / kPackedBitsPerWord;

    using PackedGridStorage = std::array<uint32_t, kMaxStandbyWords>;
    using PackedGridView = std::span<const uint32_t>;

    uint32_t advanceRng(uint32_t& rng);
    size_t packedWordCount(size_t cellCount);
    void clearPackedGrid(PackedGridStorage& cells, size_t wordCount = kMaxStandbyWords);
    bool cellAlive(PackedGridView cells, size_t index);
    bool anyCellAlive(PackedGridView cells);
    void setCell(PackedGridStorage& cells, size_t index, bool alive);
    void setCellAt(PackedGridStorage& cells, uint16_t columns, uint16_t rows, int x, int y, bool alive);
    PackedGridView viewOf(const PackedGridStorage& cells, size_t wordCount);

} // namespace standby
