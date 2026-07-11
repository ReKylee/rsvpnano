#pragma once

#include <Arduino.h>
#include <Preferences.h>

#include <concepts>
#include <cstdint>

namespace settings::nvs {

    template<typename T>
    concept ScalarValue = std::same_as<T, bool> || std::same_as<T, int8_t> || std::same_as<T, uint8_t>
                       || std::same_as<T, uint16_t> || std::same_as<T, uint32_t>;

    template<typename T> concept Value = ScalarValue<T> || std::same_as<T, String>;

    template<Value T>
    T get(Preferences& prefs, const char* key, const T& fallback) {
        if constexpr (std::same_as<T, String>) {
            return prefs.getString(key, fallback);
        } else if constexpr (std::same_as<T, bool>) {
            return prefs.getBool(key, fallback);
        } else if constexpr (std::same_as<T, int8_t>) {
            return prefs.getChar(key, fallback);
        } else if constexpr (std::same_as<T, uint8_t>) {
            return prefs.getUChar(key, fallback);
        } else if constexpr (std::same_as<T, uint16_t>) {
            return prefs.getUShort(key, fallback);
        } else {
            return prefs.getUInt(key, fallback);
        }
    }

    template<Value T>
    bool put(Preferences& prefs, const char* key, const T& value) {
        if constexpr (std::same_as<T, String>) {
            const size_t written = prefs.putString(key, value);
            return value.isEmpty() || written > 0;
        } else if constexpr (std::same_as<T, bool>) {
            return prefs.putBool(key, value) > 0;
        } else if constexpr (std::same_as<T, int8_t>) {
            return prefs.putChar(key, value) > 0;
        } else if constexpr (std::same_as<T, uint8_t>) {
            return prefs.putUChar(key, value) > 0;
        } else if constexpr (std::same_as<T, uint16_t>) {
            return prefs.putUShort(key, value) > 0;
        } else {
            return prefs.putUInt(key, value) > 0;
        }
    }

} // namespace settings::nvs
