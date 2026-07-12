#pragma once

#include <Preferences.h>

#include <algorithm>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <string>
#include <type_traits>

#include "settings/Nvs.h"

namespace settings {

    template<size_t N>
    struct Key {
        char value[N]{};

        constexpr Key(const char (&text)[N]) {
            std::copy_n(text, N, value);
        }

        constexpr size_t size() const {
            return N - 1;
        }
    };

    template<size_t N>
    Key(const char (&)[N]) -> Key<N>;

    template<Key Name>
    struct PreferenceKey {
        inline static constexpr auto kKey = Name;
        static_assert(Name.size() <= 15, "ESP32 Preferences keys must be 15 characters or shorter");

        static constexpr const char* key() {
            return kKey.value;
        }
    };

    template<typename Spec>
    concept Setting = requires {
        typename Spec::Value;
        requires nvs::Value<typename Spec::Value>;
        {
            Spec::key()
        } -> std::convertible_to<const char*>;
        {
            Spec::defaultValue()
        } -> std::same_as<typename Spec::Value>;
    };

    template<typename Spec>
    concept SanitizedSetting = Setting<Spec> && requires(typename Spec::Value value) {
        {
            Spec::sanitize(value)
        } -> std::same_as<typename Spec::Value>;
    };

    template<typename Spec, typename Context>
    concept ContextSetting = Setting<Spec> && requires(typename Spec::Value value, Context context) {
        {
            Spec::sanitize(value, context)
        } -> std::same_as<typename Spec::Value>;
    };

    template<typename Spec> concept BoolSetting = Setting<Spec> && std::same_as<typename Spec::Value, bool>;

    template<typename Spec>
    concept BoundedSetting = Setting<Spec> && requires {
        {
            Spec::minValue()
        } -> std::same_as<typename Spec::Value>;
        {
            Spec::maxValue()
        } -> std::same_as<typename Spec::Value>;
        {
            Spec::step()
        } -> std::same_as<typename Spec::Value>;
    };

    template<typename Spec>
    concept CountedEnumSetting = Setting<Spec> && std::is_enum_v<typename Spec::Value> && requires {
        {
            Spec::count()
        } -> std::same_as<size_t>;
    };

    template<Key Name, typename T, T Default>
    struct Scalar : PreferenceKey<Name> {
        using Value = T;

        static constexpr Value defaultValue() {
            return Default;
        }
    };

    template<Key Name, typename T, T Default, T Min, T Max, T Step = 1>
    struct Bounded : Scalar<Name, T, Default> {
        static_assert(Step > T{}, "bounded setting step must be positive");

        static constexpr T minValue() {
            return Min;
        }

        static constexpr T maxValue() {
            return Max;
        }

        static constexpr T step() {
            return Step;
        }

        static constexpr T sanitize(T value) {
            return std::clamp(value, Min, Max);
        }
    };

    template<Key Name, typename T, T Default>
    struct CountedIndex : Scalar<Name, T, Default> {
        static T sanitize(T value, size_t count) {
            if (count == 0) {
                return 0;
            }
            if (static_cast<size_t>(value) < count) {
                return value;
            }
            return static_cast<T>(std::min<size_t>(Default, count - 1));
        }
    };

    template<Key Name, typename T, T Default, T Count>
        requires std::is_enum_v<T>
    struct EnumSetting : Scalar<Name, T, Default> {
        using Stored = std::underlying_type_t<T>;
        static_assert(static_cast<Stored>(Count) > 0, "enum setting count must be positive");

        static constexpr size_t count() {
            return static_cast<size_t>(static_cast<Stored>(Count));
        }

        static constexpr T sanitize(T value) {
            const Stored stored = static_cast<Stored>(value);
            if constexpr (std::is_signed_v<Stored>) {
                if (stored < 0)
                    return Default;
            }
            return static_cast<size_t>(stored) < count() ? value : Default;
        }
    };

    template<Key Name>
    struct StringSetting : PreferenceKey<Name> {
        using Value = std::string;

        static Value defaultValue() {
            return "";
        }
    };

    template<Setting Spec>
    typename Spec::Value sanitize(typename Spec::Value value) {
        if constexpr (SanitizedSetting<Spec>) {
            return Spec::sanitize(value);
        } else {
            return value;
        }
    }

    template<Setting Spec, typename Context>
        requires ContextSetting<Spec, Context>
    typename Spec::Value sanitize(typename Spec::Value value, Context context) {
        return Spec::sanitize(value, context);
    }

    template<Setting Spec>
    typename Spec::Value load(Preferences& prefs) {
        using Value = typename Spec::Value;
        return sanitize<Spec>(nvs::get(prefs, Spec::key(), Spec::defaultValue()));
    }

    template<Setting Spec, typename Context>
        requires ContextSetting<Spec, Context>
    typename Spec::Value load(Preferences& prefs, Context context) {
        using Value = typename Spec::Value;
        return sanitize<Spec>(nvs::get(prefs, Spec::key(), Spec::defaultValue()), context);
    }

    template<Setting Spec>
    bool save(Preferences& prefs, typename Spec::Value value) {
        using Value = typename Spec::Value;
        const Value cleaned = sanitize<Spec>(value);
        return load<Spec>(prefs) == cleaned || nvs::put(prefs, Spec::key(), cleaned);
    }

    template<Setting Spec, typename Context>
        requires ContextSetting<Spec, Context>
    bool save(Preferences& prefs, typename Spec::Value value, Context context) {
        using Value = typename Spec::Value;
        const Value cleaned = sanitize<Spec>(value, context);
        return load<Spec>(prefs, context) == cleaned || nvs::put(prefs, Spec::key(), cleaned);
    }

    template<Setting Spec>
    bool contains(Preferences& prefs) {
        return nvs::contains(prefs, Spec::key());
    }

    template<Setting Spec>
    bool reset(Preferences& prefs) {
        return nvs::remove(prefs, Spec::key());
    }

    template<BoolSetting Spec>
    bool toggle(Preferences& prefs) {
        const bool next = !load<Spec>(prefs);
        save<Spec>(prefs, next);
        return next;
    }

    template<BoundedSetting Spec>
    typename Spec::Value cycle(Preferences& prefs) {
        using Value = typename Spec::Value;
        const Value current = load<Spec>(prefs);
        const Value minValue = Spec::minValue();
        const Value maxValue = Spec::maxValue();
        const Value stepValue = Spec::step();
        const Value next =
            current >= maxValue || maxValue - current < stepValue ? minValue : static_cast<Value>(current + stepValue);
        save<Spec>(prefs, next);
        return next;
    }

    template<CountedEnumSetting Spec>
    typename Spec::Value cycle(Preferences& prefs) {
        using Value = typename Spec::Value;
        using Stored = std::underlying_type_t<Value>;
        const auto next = static_cast<Value>((static_cast<size_t>(static_cast<Stored>(load<Spec>(prefs))) + 1U)
                                             % Spec::count());
        save<Spec>(prefs, next);
        return next;
    }

    template<Setting Spec>
        requires ContextSetting<Spec, size_t>
    typename Spec::Value cycle(Preferences& prefs, size_t count, typename Spec::Value step = 1) {
        using Value = typename Spec::Value;
        const Value current = load<Spec>(prefs, count);
        const size_t stepValue = std::max<size_t>(1, static_cast<size_t>(step));
        const Value next =
            count == 0 ? Value{} : static_cast<Value>((static_cast<size_t>(current) + stepValue) % count);
        save<Spec>(prefs, next, count);
        return next;
    }

} // namespace settings
