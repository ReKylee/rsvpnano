#pragma once

#include <Arduino.h>
#include <Preferences.h>

#include <concepts>
#include <cstdint>
#include <string>
#include <type_traits>

namespace settings::nvs {

    template<typename T>
    concept ScalarValue = std::same_as<T, bool> || std::same_as<T, int8_t> || std::same_as<T, uint8_t>
                       || std::same_as<T, uint16_t> || std::same_as<T, int32_t> || std::same_as<T, uint32_t>;

    template<typename T>
    concept EnumValue = std::is_enum_v<T> && ScalarValue<std::underlying_type_t<T>>;

    template<typename T> concept Value = ScalarValue<T> || EnumValue<T> || std::same_as<T, std::string>;

    inline bool begin(Preferences& prefs, const char* name, bool readOnly = false) {
        return prefs.begin(name, readOnly);
    }

    inline void end(Preferences& prefs) {
        prefs.end();
    }

    inline bool contains(Preferences& prefs, const char* key) {
        return prefs.isKey(key);
    }

    inline bool remove(Preferences& prefs, const char* key) {
        return !contains(prefs, key) || prefs.remove(key);
    }

    template<Value T>
    T get(Preferences& prefs, const char* key, const T& fallback) {
        if constexpr (EnumValue<T>) {
            using Stored = std::underlying_type_t<T>;
            return static_cast<T>(get(prefs, key, static_cast<Stored>(fallback)));
        } else if constexpr (std::same_as<T, std::string>) {
            const String value = prefs.getString(key, fallback.c_str());
            return {value.c_str(), value.length()};
        } else if constexpr (std::same_as<T, bool>) {
            return prefs.getBool(key, fallback);
        } else if constexpr (std::same_as<T, int8_t>) {
            return prefs.getChar(key, fallback);
        } else if constexpr (std::same_as<T, uint8_t>) {
            return prefs.getUChar(key, fallback);
        } else if constexpr (std::same_as<T, uint16_t>) {
            return prefs.getUShort(key, fallback);
        } else if constexpr (std::same_as<T, int32_t>) {
            return prefs.getInt(key, fallback);
        } else {
            return prefs.getUInt(key, fallback);
        }
    }

    template<Value T>
    bool put(Preferences& prefs, const char* key, const T& value) {
        if constexpr (EnumValue<T>) {
            using Stored = std::underlying_type_t<T>;
            return put(prefs, key, static_cast<Stored>(value));
        } else if constexpr (std::same_as<T, std::string>) {
            const size_t written = prefs.putString(key, value.c_str());
            return value.empty() || written > 0;
        } else if constexpr (std::same_as<T, bool>) {
            return prefs.putBool(key, value) > 0;
        } else if constexpr (std::same_as<T, int8_t>) {
            return prefs.putChar(key, value) > 0;
        } else if constexpr (std::same_as<T, uint8_t>) {
            return prefs.putUChar(key, value) > 0;
        } else if constexpr (std::same_as<T, uint16_t>) {
            return prefs.putUShort(key, value) > 0;
        } else if constexpr (std::same_as<T, int32_t>) {
            return prefs.putInt(key, value) > 0;
        } else {
            return prefs.putUInt(key, value) > 0;
        }
    }

} // namespace settings::nvs
