#include "settings/Config.h"

#include <Arduino.h>
#include <FS.h>

#include <charconv>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>

#include "settings/PreferenceSpecs.h"
#include "storage/fs/StoragePaths.h"

namespace settings {
    namespace {

        namespace pref = prefs;

        constexpr char kHashKey[] = "cfg_hash";
        constexpr size_t kMaxConfigBytes = 8192;
        constexpr uint32_t kMirrorDelayMs = 1500;

        bool dirty = false;
        bool mirrorEnabled = false;
        uint32_t dirtyAtMs = 0;

        std::string_view trim(std::string_view text) {
            while (!text.empty() && (text.front() == ' ' || text.front() == '\t' || text.front() == '\r'))
                text.remove_prefix(1);
            while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\r'))
                text.remove_suffix(1);
            return text;
        }

        template<typename T>
        bool parseInteger(std::string_view text, T& value) {
            text = trim(text);
            if (text.empty())
                return false;

            if constexpr (std::is_signed_v<T>) {
                int64_t parsed = 0;
                const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), parsed);
                if (error != std::errc{} || end != text.data() + text.size()
                    || parsed < std::numeric_limits<T>::min() || parsed > std::numeric_limits<T>::max())
                    return false;
                value = static_cast<T>(parsed);
            } else {
                uint64_t parsed = 0;
                const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), parsed);
                if (error != std::errc{} || end != text.data() + text.size()
                    || parsed > std::numeric_limits<T>::max())
                    return false;
                value = static_cast<T>(parsed);
            }
            return true;
        }

        bool parseString(std::string_view text, std::string& value) {
            text = trim(text);
            if (text.empty() || text.front() != '"') {
                value.assign(text);
                return true;
            }
            if (text.size() < 2 || text.back() != '"')
                return false;

            value.clear();
            value.reserve(text.size() - 2);
            for (size_t index = 1; index + 1 < text.size(); ++index) {
                const char character = text[index];
                if (character != '\\') {
                    value.push_back(character);
                    continue;
                }
                if (++index + 1 >= text.size())
                    return false;
                switch (text[index]) {
                case '\\':
                case '"':
                    value.push_back(text[index]);
                    break;
                case 'n':
                    value.push_back('\n');
                    break;
                case 'r':
                    value.push_back('\r');
                    break;
                case 't':
                    value.push_back('\t');
                    break;
                default:
                    return false;
                }
            }
            return true;
        }

        template<Setting Spec>
        bool parseValue(std::string_view text, typename Spec::Value& value) {
            using Value = typename Spec::Value;
            Value parsed{};
            if constexpr (std::same_as<Value, std::string>) {
                if (!parseString(text, parsed))
                    return false;
            } else if constexpr (std::same_as<Value, bool>) {
                text = trim(text);
                if (text == "true" || text == "1")
                    parsed = true;
                else if (text == "false" || text == "0")
                    parsed = false;
                else
                    return false;
            } else if constexpr (std::is_enum_v<Value>) {
                using Stored = std::underlying_type_t<Value>;
                Stored stored{};
                if (!parseInteger(text, stored))
                    return false;
                parsed = static_cast<Value>(stored);
            } else if (!parseInteger(text, parsed)) {
                return false;
            }

            const Value cleaned = sanitize<Spec>(parsed);
            if (cleaned != parsed)
                return false;
            value = std::move(parsed);
            return true;
        }

        void appendQuoted(std::string& output, std::string_view value) {
            output.push_back('"');
            for (const char character: value) {
                switch (character) {
                case '\\':
                case '"':
                    output.push_back('\\');
                    output.push_back(character);
                    break;
                case '\n':
                    output += "\\n";
                    break;
                case '\r':
                    output += "\\r";
                    break;
                case '\t':
                    output += "\\t";
                    break;
                default:
                    output.push_back(character);
                    break;
                }
            }
            output.push_back('"');
        }

        template<typename T>
        void appendValue(std::string& output, const T& value) {
            if constexpr (std::same_as<T, std::string>) {
                appendQuoted(output, value);
            } else if constexpr (std::same_as<T, bool>) {
                output += value ? "true" : "false";
            } else if constexpr (std::is_enum_v<T>) {
                using Stored = std::underlying_type_t<T>;
                output += std::to_string(static_cast<long long>(static_cast<Stored>(value)));
            } else if constexpr (std::is_signed_v<T>) {
                output += std::to_string(static_cast<long long>(value));
            } else {
                output += std::to_string(static_cast<unsigned long long>(value));
            }
        }

        template<typename... Specs>
        struct Schema {
            using SpecTuple = std::tuple<Specs...>;
            using Values = std::tuple<typename Specs::Value...>;
            static constexpr size_t size = sizeof...(Specs);
            static_assert(size <= 64);

            static Values load(Preferences& preferences) {
                return Values{settings::load<Specs>(preferences)...};
            }

            template<size_t... Index>
            static bool apply(Preferences& preferences, const Values& values, std::index_sequence<Index...>) {
                return (settings::save<std::tuple_element_t<Index, SpecTuple>>(preferences, std::get<Index>(values))
                        && ...);
            }

            static bool apply(Preferences& preferences, const Values& values) {
                return apply(preferences, values, std::make_index_sequence<size>{});
            }

            template<size_t Index = 0>
            static bool assign(std::string_view key, std::string_view text, Values& values, uint64_t& seen,
                               bool& known) {
                if constexpr (Index == size) {
                    known = false;
                    return true;
                } else {
                    using Spec = std::tuple_element_t<Index, SpecTuple>;
                    if (key == Spec::key()) {
                        known = true;
                        const uint64_t bit = uint64_t{1} << Index;
                        if ((seen & bit) != 0)
                            return false;
                        if (!parseValue<Spec>(text, std::get<Index>(values)))
                            return false;
                        seen |= bit;
                        return true;
                    }
                    return assign<Index + 1>(key, text, values, seen, known);
                }
            }

            template<size_t... Index>
            static std::string serialize(const Values& values, std::index_sequence<Index...>) {
                std::string output{
                    "# RSVP Nano settings\nversion=1\n# Wi-Fi passwords remain in device storage and are never mirrored.\n"};
                output.reserve(1024);
                ((output += std::tuple_element_t<Index, SpecTuple>::key(), output.push_back('='),
                  appendValue(output, std::get<Index>(values)), output.push_back('\n')),
                 ...);
                return output;
            }

            static std::string serialize(const Values& values) {
                return serialize(values, std::make_index_sequence<size>{});
            }
        };

        using MirroredSettings =
            Schema<pref::Wpm, pref::BrightnessIndex, pref::ThemeId, pref::UiLanguage, pref::Handedness,
                   pref::PhantomWords, pref::ChapterScrollReversed, pref::FooterMetricMode, pref::BatteryLabelMode,
                   pref::ScreensaverMode, pref::ReaderBatteryVisible, pref::ReaderChapterVisible,
                   pref::ReaderProgressVisible, pref::ReaderFontSizeIndex, pref::ReaderTypefaceId,
                   pref::TypographyFocusHighlight, pref::TypographyTracking, pref::TypographyAnchor,
                   pref::TypographyGuideWidth, pref::TypographyGuideGap, pref::PacingLongWordDelay,
                   pref::PacingComplexWordDelay, pref::PacingPunctuationDelay, pref::PauseMode,
                   pref::StandbyTimerIndex, pref::WifiSsid, pref::OtaAuto, pref::OtaOwner, pref::OtaTag>;

        enum class ReadResult : uint8_t {
            Missing,
            Valid,
            Invalid,
        };

        ReadResult readConfig(fs::FS& filesystem, MirroredSettings::Values& values, bool& excludedSecretSeen) {
            File file = filesystem.open(StoragePaths::kSettingsConfigPath, FILE_READ);
            if (!file)
                return ReadResult::Missing;
            if (file.isDirectory() || file.size() == 0 || file.size() > kMaxConfigBytes) {
                file.close();
                return ReadResult::Invalid;
            }

            std::string content(file.size(), '\0');
            const size_t read = file.read(reinterpret_cast<uint8_t*>(content.data()), content.size());
            file.close();
            if (read != content.size())
                return ReadResult::Invalid;

            bool versionSeen = false;
            uint64_t seen = 0;
            size_t offset = 0;
            while (offset <= content.size()) {
                const size_t end = content.find('\n', offset);
                std::string_view line{content.data() + offset,
                                      (end == std::string::npos ? content.size() : end) - offset};
                line = trim(line);
                if (!line.empty() && line.front() != '#') {
                    const size_t separator = line.find('=');
                    if (separator == std::string_view::npos)
                        return ReadResult::Invalid;
                    const std::string_view key = trim(line.substr(0, separator));
                    const std::string_view value = trim(line.substr(separator + 1));
                    if (key == "version") {
                        if (versionSeen || value != "1")
                            return ReadResult::Invalid;
                        versionSeen = true;
                    } else if (key == pref::WifiPassword::key()) {
                        excludedSecretSeen = true;
                    } else {
                        bool known = false;
                        if (!MirroredSettings::assign(key, value, values, seen, known))
                            return ReadResult::Invalid;
                        if (!known)
                            Serial.printf("[settings] ignored unknown config key: %.*s\n", static_cast<int>(key.size()),
                                          key.data());
                    }
                }
                if (end == std::string::npos)
                    break;
                offset = end + 1;
            }
            return versionSeen ? ReadResult::Valid : ReadResult::Invalid;
        }

        bool ensureConfigDirectory(fs::FS& filesystem) {
            File directory = filesystem.open(StoragePaths::kConfigPath);
            const bool exists = directory && directory.isDirectory();
            if (directory)
                directory.close();
            return exists || filesystem.mkdir(StoragePaths::kConfigPath);
        }

        bool writeConfig(fs::FS& filesystem, std::string_view content) {
            if (!ensureConfigDirectory(filesystem))
                return false;

            filesystem.remove(StoragePaths::kSettingsConfigTempPath);
            File file = filesystem.open(StoragePaths::kSettingsConfigTempPath, FILE_WRITE);
            if (!file || file.isDirectory()) {
                if (file)
                    file.close();
                return false;
            }
            const size_t written = file.write(reinterpret_cast<const uint8_t*>(content.data()), content.size());
            file.flush();
            file.close();
            if (written != content.size()) {
                filesystem.remove(StoragePaths::kSettingsConfigTempPath);
                return false;
            }

            filesystem.remove(StoragePaths::kSettingsConfigBackupPath);
            File current = filesystem.open(StoragePaths::kSettingsConfigPath, FILE_READ);
            const bool hadCurrent = current && !current.isDirectory();
            if (current)
                current.close();
            if (hadCurrent
                && !filesystem.rename(StoragePaths::kSettingsConfigPath, StoragePaths::kSettingsConfigBackupPath)) {
                filesystem.remove(StoragePaths::kSettingsConfigTempPath);
                return false;
            }
            if (!filesystem.rename(StoragePaths::kSettingsConfigTempPath, StoragePaths::kSettingsConfigPath)) {
                if (hadCurrent)
                    filesystem.rename(StoragePaths::kSettingsConfigBackupPath, StoragePaths::kSettingsConfigPath);
                filesystem.remove(StoragePaths::kSettingsConfigTempPath);
                return false;
            }
            filesystem.remove(StoragePaths::kSettingsConfigBackupPath);
            return true;
        }

        uint32_t hash(std::string_view content) {
            uint32_t value = 2166136261UL;
            for (const char character: content) {
                value ^= static_cast<uint8_t>(character);
                value *= 16777619UL;
            }
            return value == 0 ? 1 : value;
        }

        void markClean() {
            dirty = false;
            dirtyAtMs = 0;
        }

        bool mirror(Preferences& preferences, fs::FS& filesystem, const MirroredSettings::Values& values) {
            const std::string content = MirroredSettings::serialize(values);
            if (!writeConfig(filesystem, content)) {
                Serial.println("[settings] config mirror failed");
                markDirty();
                return false;
            }
            if (!nvs::put(preferences, kHashKey, hash(content))) {
                Serial.println("[settings] config hash save failed");
                markDirty();
                return false;
            }
            markClean();
            Serial.printf("[settings] mirrored %s\n", StoragePaths::kSettingsConfigPath);
            return true;
        }

    } // namespace

    void markDirty() {
        dirty = true;
        dirtyAtMs = millis();
    }

    bool reconcile(Preferences& preferences, fs::FS& filesystem) {
        MirroredSettings::Values saved = MirroredSettings::load(preferences);
        MirroredSettings::Values fileValues = saved;
        bool excludedSecretSeen = false;
        const ReadResult result = readConfig(filesystem, fileValues, excludedSecretSeen);
        if (result == ReadResult::Invalid) {
            mirrorEnabled = false;
            markClean();
            Serial.printf("[settings] invalid %s; using saved settings\n", StoragePaths::kSettingsConfigPath);
            return false;
        }

        mirrorEnabled = true;
        if (result == ReadResult::Missing)
            return mirror(preferences, filesystem, saved);

        const std::string fileContent = MirroredSettings::serialize(fileValues);
        const uint32_t fileHash = hash(fileContent);
        const uint32_t previousHash = nvs::get(preferences, kHashKey, uint32_t{0});
        if (previousHash == 0 || fileHash != previousHash) {
            if (!MirroredSettings::apply(preferences, fileValues)) {
                MirroredSettings::apply(preferences, saved);
                Serial.println("[settings] config import failed");
                return false;
            }
            if (!mirror(preferences, filesystem, fileValues))
                return false;
            Serial.printf("[settings] imported %s\n", StoragePaths::kSettingsConfigPath);
            return true;
        }

        if (fileValues != saved || excludedSecretSeen)
            return mirror(preferences, filesystem, saved);

        markClean();
        return true;
    }

    void update(Preferences& preferences, fs::FS& filesystem, uint32_t nowMs) {
        if (mirrorEnabled && dirty && nowMs - dirtyAtMs >= kMirrorDelayMs)
            mirror(preferences, filesystem, MirroredSettings::load(preferences));
    }

    bool flush(Preferences& preferences, fs::FS& filesystem) {
        return !mirrorEnabled || !dirty || mirror(preferences, filesystem, MirroredSettings::load(preferences));
    }

} // namespace settings
