#pragma once

#include <cstddef>
#include <concepts>
#include <type_traits>
#include <utility>

namespace settings {

    template<std::integral T, T Minimum, T Maximum, T Step = T{1}>
    class BoundedValue {
    public:
        using Value = T;

        static_assert(Minimum <= Maximum);
        static_assert(Step > T{});

        static constexpr T min() noexcept { return Minimum; }
        static constexpr T max() noexcept { return Maximum; }
        static constexpr T step() noexcept { return Step; }

        constexpr BoundedValue() noexcept = default;

        template<std::integral U>
        explicit constexpr BoundedValue(U value) noexcept : value_(sanitize(value)) {}

        template<std::integral U>
        constexpr BoundedValue& operator=(U value) noexcept {
            value_ = sanitize(value);
            return *this;
        }

        constexpr operator T() const noexcept { return value_; }

        constexpr void cycle() noexcept {
            value_ = value_ >= Maximum || Maximum - value_ < Step ? Minimum : static_cast<T>(value_ + Step);
        }

        bool operator==(const BoundedValue&) const = default;

    private:
        template<std::integral U>
        static constexpr T sanitize(U value) noexcept {
            if (std::cmp_less(value, Minimum))
                return Minimum;
            if (std::cmp_greater(value, Maximum))
                return Maximum;
            return static_cast<T>(value);
        }

        T value_ = Minimum;
    };

} // namespace settings

namespace settings::rules {

    inline constexpr size_t kFontIdMaxLength = 48;
    inline constexpr size_t kThemeIdMaxLength = 64;
    inline constexpr size_t kWifiSsidMaxLength = 32;
    inline constexpr size_t kWifiPasswordMaxLength = 64;
    inline constexpr size_t kRepositoryOwnerMaxLength = 64;
    inline constexpr size_t kReleaseTagMaxLength = 64;
    inline constexpr size_t kThemeNameMaxLength = 64;

} // namespace settings::rules

namespace settings {

    template<typename E>
    concept CountedEnum = std::is_enum_v<E> && requires { E::Count; };

    template<CountedEnum E>
    constexpr E cycleEnum(E current) noexcept {
        using Underlying = std::underlying_type_t<E>;
        using Unsigned = std::make_unsigned_t<Underlying>;
        constexpr Underlying rawCount = std::to_underlying(E::Count);
        static_assert(rawCount > 0);

        const Underlying rawCurrent = std::to_underlying(current);
        if constexpr (std::is_signed_v<Underlying>) {
            if (rawCurrent < 0)
                return static_cast<E>(0);
        }
        const auto index = static_cast<Unsigned>(rawCurrent);
        const auto count = static_cast<Unsigned>(rawCount);
        return index >= count || index + 1 == count ? static_cast<E>(0) : static_cast<E>(index + 1);
    }

} // namespace settings
