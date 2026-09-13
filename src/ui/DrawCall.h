#pragma once

#include <bit>
#include <concepts>
#include <cstdint>
#include <functional>
#include <string_view>
#include <tuple>
#include <type_traits>

#include "hash/Fnv1a.h"
#include "ui/Geometry.h"

namespace ui::detail {
    // Serialize values, never object representations (padding) or string addresses.
    class DrawSignature {
    public:
        template<std::integral T>
        void append(T value) {
            byte(1);
            integer(static_cast<uint64_t>(value));
        }

        template<typename T>
            requires std::is_enum_v<T>
        void append(T value) {
            append(static_cast<std::underlying_type_t<T>>(value));
        }

        template<std::floating_point T>
        void append(T value) {
            static_assert(sizeof(T) == 4 || sizeof(T) == 8);
            byte(2);
            // Equal zeros should not invalidate one another.
            if (value == 0)
                value = 0;
            if constexpr (sizeof(T) == 4)
                integer(std::bit_cast<uint32_t>(value));
            else
                integer(std::bit_cast<uint64_t>(value));
        }

        void append(std::string_view text) {
            byte(3);
            integer(text.size());
            hash_ = Fnv1a::append(hash_, text);
        }

        void append(Rect rect) {
            byte(4);
            append(rect.x);
            append(rect.y);
            append(rect.w);
            append(rect.h);
        }

        uint32_t value() const { return hash_; }

    private:
        void byte(uint8_t value) {
            hash_ = (hash_ ^ value) * Fnv1a::kPrime;
        }

        void integer(uint64_t value) {
            for (unsigned i = 0; i < sizeof(value); ++i) {
                byte(static_cast<uint8_t>(value));
                value >>= 8;
            }
        }

        uint32_t hash_ = Fnv1a::kOffsetBasis;
    };

    template<typename T>
    concept DrawArgument = requires(DrawSignature& signature, const T& value) { signature.append(value); };

    template<typename Draw>
    concept StatelessDraw = std::is_empty_v<Draw> && std::default_initializable<Draw>;

    template<typename Draw>
    inline const char drawIdentity = 0;

    // The same arguments drive both invalidation and drawing. References are valid
    // only during this synchronous call; neither callbacks nor model copies survive it.
    template<StatelessDraw Draw, DrawArgument... Args>
    class DrawCall {
    public:
        explicit DrawCall(const Args&... args) : args_(args...) {}

        uint32_t signature() const {
            DrawSignature signature;
            signature.append(reinterpret_cast<uintptr_t>(&drawIdentity<DrawCall>));
            std::apply([&](const auto&... args) { (signature.append(args), ...); }, args_);
            return signature.value();
        }

        template<typename Context, typename Rect>
        void operator()(Context& context, Rect rect) const {
            std::apply([&](const auto&... args) { std::invoke(Draw{}, context, rect, args...); }, args_);
        }

    private:
        std::tuple<const Args&...> args_;
    };
} // namespace ui::detail
