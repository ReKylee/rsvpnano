#pragma once

#include <cstdint>
#include <span>
#include <string_view>

namespace Fnv1a {

    inline constexpr uint32_t kOffsetBasis = 2166136261U;
    inline constexpr uint32_t kPrime = 16777619U;

    constexpr uint32_t append(uint32_t hash, std::span<const uint8_t> bytes) {
        for (const uint8_t byte: bytes) {
            hash ^= byte;
            hash *= kPrime;
        }
        return hash;
    }

    constexpr uint32_t append(uint32_t hash, std::string_view text) {
        for (const char character: text) {
            hash ^= static_cast<uint8_t>(character);
            hash *= kPrime;
        }
        return hash;
    }

    constexpr uint32_t hash(std::span<const uint8_t> bytes) {
        return append(kOffsetBasis, bytes);
    }

    constexpr uint32_t hash(std::string_view text) {
        return append(kOffsetBasis, text);
    }

} // namespace Fnv1a
