#include <esp_log.h>
#include "app/App.h"

#include <FS.h>
#include <glaze/toml.hpp>
#include <nvs.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <cstdio>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "board/BoardStorage.h"
#include "rss/RssConfig.h"
#include "rss/RssConfigStorage.h"
#include "settings/NvsSecurity.h"
#include "settings/SettingsGlaze.h"
#include "storage/fs/StorageFiles.h"
#include "storage/fs/StoragePaths.h"
#include "storage/index/ReadingProgress.h"
#include "text/AsciiText.h"
#include "timer/FocusTimerStorage.h"
#include "ui/Theme.h"

namespace {

    struct LegacyLocale {
        std::string_view name;
        std::string_view tag;
    };

    constexpr std::array kLegacyUiLocales{
        LegacyLocale{"english", "en"}, LegacyLocale{"spanish", "es"},  LegacyLocale{"french", "fr"},
        LegacyLocale{"german", "de"},  LegacyLocale{"romanian", "ro"}, LegacyLocale{"polish", "pl"},
        LegacyLocale{"russian", "ru"},
    };

    std::string_view legacyUiLocale(uint64_t ordinal) {
        return (ordinal < kLegacyUiLocales.size() ? kLegacyUiLocales[ordinal] : kLegacyUiLocales.front()).tag;
    }

    std::string_view legacyUiLocale(std::string_view name) {
        const auto found = std::ranges::find(kLegacyUiLocales, name, &LegacyLocale::name);
        return found == kLegacyUiLocales.end() ? std::string_view{} : found->tag;
    }

    struct LegacyLocaleInterface {
        std::string language;
        std::string locale;
    };

    struct LegacyLocaleSettings {
        LegacyLocaleInterface interface;
    };

    std::optional<std::string> migrateLocaleDocument(std::string_view content, settings::SettingsSource source) {
        constexpr glz::opts options{.format = glz::TOML, .error_on_unknown_keys = false};
        LegacyLocaleSettings legacy;
        if (glz::read<options>(legacy, content) || legacy.interface.language.empty()
            || !legacy.interface.locale.empty())
            return std::nullopt;
        const std::string_view locale = legacyUiLocale(legacy.interface.language);
        if (locale.empty())
            return std::nullopt;
        auto decoded = settings::codec::decodeToml(content, source);
        if (!decoded)
            return std::nullopt;
        decoded->interface.locale = locale;
        auto encoded = settings::codec::encodeToml(*decoded, source);
        return encoded ? std::optional<std::string>{std::move(*encoded)} : std::nullopt;
    }

    struct LegacyFocusTimer {
        std::string name;
        uint16_t focus_minutes = 25;
        uint16_t break_minutes = 5;
        uint8_t rounds = 4;
    };

    struct LegacyFocusConfig {
        uint32_t version = 0;
        std::vector<LegacyFocusTimer> timer;
    };

    struct LegacyBookState {
        uint32_t sourceSize = 0;
        uint32_t sourceFingerprint = 0;
        uint32_t wordCount = 0;
        uint32_t wordIndex = 0;
        std::optional<settings::TypographySettings> bookTypographyOverride;
    };

    bool migrateBookStateOverrides(fs::FS& filesystem, const std::string& path) {
        auto content = StorageFiles::readTextFile(filesystem, path.c_str(), 2048);
        if (!content)
            return false;
        if (!content->contains("bookTypographyOverride"))
            return true;

        LegacyBookState legacy;
        constexpr glz::opts options{.format = glz::TOML, .error_on_unknown_keys = false};
        if (glz::read<options>(legacy, *content) || legacy.sourceSize == 0 || legacy.wordCount == 0)
            return false;

        ReadingSession::BookState migrated{
            .sourceSize = legacy.sourceSize,
            .sourceFingerprint = legacy.sourceFingerprint,
            .wordCount = legacy.wordCount,
            .wordIndex = legacy.wordIndex,
        };
        std::string output;
        if (glz::write_toml(migrated, output))
            return false;

        const std::string temporary = path + ".tmp";
        const std::string backup = path + ".bak";
        return StorageFiles::writeFileAtomic(filesystem, path.c_str(), temporary.c_str(), backup.c_str(), output)
            .has_value();
    }

} // namespace

void App::migrateSettingsLocale() {
    Preferences settingsPreferences;
    if (settingsPreferences.begin(settings::kSettingsNvsNamespace, false)) {
        const size_t size = settingsPreferences.getBytesLength(settings::kSettingsNvsKey);
        if (size > 0 && size <= settings::kMaxSettingsBytes) {
            std::string content(size, '\0');
            if (settingsPreferences.getBytes(settings::kSettingsNvsKey, content.data(), content.size())
                == content.size()) {
                if (auto migrated = migrateLocaleDocument(content, settings::SettingsSource::Nvs);
                    migrated
                    && settingsPreferences.putBytes(settings::kSettingsNvsKey, migrated->data(), migrated->size())
                           != migrated->size())
                    ESP_LOGE("migration", "could not persist migrated NVS locale setting");
            }
        }
        settingsPreferences.end();
    }
}

void App::migrateSettingsLocale(fs::FS& filesystem) {
    auto content =
        StorageFiles::readTextFile(filesystem, StoragePaths::kSettingsConfigPath, settings::kMaxSettingsBytes);
    if (!content)
        return;
    if (auto migrated = migrateLocaleDocument(*content, settings::SettingsSource::Sd)) {
        if (auto written = StorageFiles::writeFileAtomic(filesystem, StoragePaths::kSettingsConfigPath,
                                                         StoragePaths::kSettingsConfigTempPath,
                                                         StoragePaths::kSettingsConfigBackupPath, *migrated);
            !written)
            ESP_LOGE("migration", "locale settings migration failed: %s", written.error().message().c_str());
    }
}

void App::migrateLegacyStorage() {
    // This function is for migrating pre-Glaze settings, secrets, themes, RSS, focus timers, and book progress.
    constexpr char kMigrationMarker[] = "cfg5_migrated";
    constexpr std::array kPreviousMigrationMarkers{"cfg2_migrated", "cfg3_migrated", "cfg4_migrated"};
    constexpr char kLegacySettingsPath[] = "/config/settings.conf";
    constexpr char kLegacySettingsBackupPath[] = "/config/settings.conf.bak";
    constexpr char kLegacySettingsTempPath[] = "/config/settings.conf.tmp";
    constexpr char kLegacyRssPath[] = "/config/rss.conf";
    constexpr char kLegacyRssBackupPath[] = "/config/rss.conf.bak";
    constexpr char kLegacyRssTempPath[] = "/config/rss.conf.tmp";
    constexpr char kLegacyFocusPath[] = "/config/focus.conf";
    constexpr char kLegacyFocusBackupPath[] = "/config/focus.conf.bak";
    constexpr char kLegacyFocusTempPath[] = "/config/focus.conf.tmp";
    constexpr char kLegacyThemeExtension[] = ".rtheme";
    constexpr char kLegacyProgressExtension[] = ".rpos";
    constexpr size_t kMaxLegacyThemeBytes = 4096;
    constexpr size_t kMaxLegacyRssBytes = 4096;
    constexpr size_t kMaxLegacyFocusBytes = 4096;

    if (prefs_.getBool(kMigrationMarker, false))
        return;

    auto nextLine = [](std::string_view& text) {
        const size_t end = text.find('\n');
        if (end == std::string_view::npos) {
            const std::string_view line = text;
            text = {};
            return line;
        }
        const std::string_view line = text.substr(0, end);
        text.remove_prefix(end + 1);
        return line;
    };
    auto parseUnsigned = [&](std::string_view text, uint64_t& value) {
        text = AsciiText::trim(text);
        value = 0;
        const auto [end, error] = std::from_chars(text.begin(), text.end(), value);
        return !text.empty() && error == std::errc{} && end == text.end();
    };
    auto parseSigned = [&](std::string_view text, int64_t& value) {
        text = AsciiText::trim(text);
        value = 0;
        const auto [end, error] = std::from_chars(text.begin(), text.end(), value);
        return !text.empty() && error == std::errc{} && end == text.end();
    };
    auto parseBool = [&](std::string_view text, bool& value) {
        text = AsciiText::trim(text);
        if (text == "true" || text == "1") {
            value = true;
            return true;
        }
        if (text == "false" || text == "0") {
            value = false;
            return true;
        }
        return false;
    };
    auto parseString = [&](std::string_view text, std::string& value) {
        text = AsciiText::trim(text);
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
    };
    auto readFile = [](fs::FS& filesystem, const char* path, size_t maximum, std::string& content) {
        File file = filesystem.open(path, FILE_READ);
        if (!file || file.isDirectory()) {
            if (file)
                file.close();
            return false;
        }
        const size_t size = file.size();
        if (size == 0 || size > maximum) {
            file.close();
            return false;
        }
        content.assign(size, '\0');
        const size_t count = file.read(reinterpret_cast<uint8_t*>(content.data()), content.size());
        file.close();
        return count == content.size();
    };

    settings::DeviceSettings candidate = settingsStore_.settings();
    settings::DeviceSecrets secrets = settingsStore_.secrets();
    bool legacySettingsSeen = false;
    bool legacySecretsSeen = false;
    const auto migrateBrightnessIndex = [](uint64_t value) {
        return static_cast<uint8_t>((std::min<uint64_t>(value, 19) + 1) * 5);
    };

    auto readU8 = [&](const char* key, auto&& assign) {
        if (!prefs_.isKey(key))
            return;
        assign(prefs_.getUChar(key));
        legacySettingsSeen = true;
    };
    auto readU16 = [&](const char* key, auto&& assign) {
        if (!prefs_.isKey(key))
            return;
        assign(prefs_.getUShort(key));
        legacySettingsSeen = true;
    };
    auto readI8 = [&](const char* key, auto&& assign) {
        if (!prefs_.isKey(key))
            return;
        assign(prefs_.getChar(key));
        legacySettingsSeen = true;
    };
    auto readBoolKey = [&](const char* key, bool& value) {
        if (!prefs_.isKey(key))
            return;
        value = prefs_.getBool(key, value);
        legacySettingsSeen = true;
    };
    auto readStringKey = [&](const char* key, std::string& value) {
        if (!prefs_.isKey(key))
            return;
        value = prefs_.getString(key, value.c_str()).c_str();
        legacySettingsSeen = true;
    };

    readU16("wpm", [&](uint16_t value) {
        candidate.reading.wpm = value;
    });
    readU8("bright", [&](uint8_t value) {
        candidate.interface.brightnessPercent = migrateBrightnessIndex(value);
    });
    readStringKey("theme_id", candidate.interface.selectedThemeId);
    readU8("ui_lang", [&](uint8_t value) {
        candidate.interface.locale = legacyUiLocale(value);
    });
    readBoolKey("handed", candidate.reading.leftHanded);
    readBoolKey("phantom_on", candidate.reading.phantomWords);
    readBoolKey("ch_scroll_rev", candidate.reading.chapterScrollReversed);
    readU8("prog_md", [&](uint8_t value) {
        candidate.reading.footerMetric = value < std::to_underlying(settings::FooterMetric::Count)
                                           ? static_cast<settings::FooterMetric>(value)
                                           : settings::FooterMetric::percentage;
    });
    readU8("bat_md", [&](uint8_t value) {
        candidate.reading.batteryLabel = value < std::to_underlying(settings::BatteryLabel::Count)
                                           ? static_cast<settings::BatteryLabel>(value)
                                           : settings::BatteryLabel::percentage;
    });
    readU8("scrn_sv", [&](uint8_t value) {
        candidate.interface.screensaver =
            value < std::to_underlying(standby::Kind::Count) ? static_cast<standby::Kind>(value) : standby::Kind::life;
    });
    readBoolKey("read_bat", candidate.reading.batteryVisibleWhileReading);
    readBoolKey("read_ch", candidate.reading.chapterVisibleWhileReading);
    readBoolKey("read_pct", candidate.reading.progressVisibleWhileReading);
    readU8("font_size", [&](uint8_t value) {
        candidate.reading.typography.fontSizeIndex = value;
    });
    readBoolKey("type_hlt", candidate.reading.typography.focusHighlight);
    readI8("type_trk", [&](int8_t value) {
        candidate.reading.typography.tracking = value;
    });
    readU8("type_anc", [&](uint8_t value) {
        candidate.reading.typography.anchor = value;
    });
    readU8("type_wid", [&](uint8_t value) {
        candidate.reading.typography.guideWidth = value;
    });
    readU8("type_gap", [&](uint8_t value) {
        candidate.reading.typography.guideGap = value;
    });
    readU16("pace_lms", [&](uint16_t value) {
        candidate.reading.pacing.longWordDelayMs = value;
    });
    readU16("pace_cms", [&](uint16_t value) {
        candidate.reading.pacing.complexWordDelayMs = value;
    });
    readU16("pace_pms", [&](uint16_t value) {
        candidate.reading.pacing.punctuationDelayMs = value;
    });
    readU8("pause_md", [&](uint8_t value) {
        candidate.reading.pauseMode = value < std::to_underlying(settings::PauseMode::Count)
                                        ? static_cast<settings::PauseMode>(value)
                                        : settings::PauseMode::sentenceEnd;
    });
    readU8("stby_tmr", [&](uint8_t value) {
        candidate.interface.standbyTimerIndex = value;
    });
    readStringKey("wifi_ssid", candidate.network.wifiSsid);
    readBoolKey("ota_auto", candidate.updates.checkOnStartup);
    readStringKey("ota_owner", candidate.updates.repositoryOwner);
    readStringKey("ota_tag", candidate.updates.releaseTag);
    if (prefs_.isKey("wifi_pass")) {
        secrets.wifiPassword = prefs_.getString("wifi_pass", secrets.wifiPassword.c_str()).c_str();
        legacySecretsSeen = true;
    }

    fs::FS* filesystem = storage_.mounted() ? &Board::Storage::filesystem() : nullptr;
    bool configComplete = filesystem != nullptr;
    const char* legacyConfigPath = nullptr;
    if (filesystem != nullptr) {
        if (StorageFiles::fileExists(kLegacySettingsPath))
            legacyConfigPath = kLegacySettingsPath;
        else if (StorageFiles::fileExists(kLegacySettingsBackupPath))
            legacyConfigPath = kLegacySettingsBackupPath;
        else if (StorageFiles::fileExists(kLegacySettingsTempPath))
            legacyConfigPath = kLegacySettingsTempPath;
    }

    if (legacyConfigPath != nullptr) {
        enum class AssignResult : uint8_t {
            applied,
            unknown,
            invalid,
        };
        std::string content;
        settings::DeviceSettings parsed = candidate;
        bool versionSeen = false;
        bool valid = readFile(*filesystem, legacyConfigPath, settings::kMaxSettingsBytes, content);
        std::vector<std::string> seenKeys;
        seenKeys.reserve(32);

        auto assignUnsigned = [&](std::string_view value, auto& field) {
            uint64_t parsedValue = 0;
            if (!parseUnsigned(value, parsedValue))
                return false;
            field = parsedValue;
            return true;
        };
        auto assigned = [](bool valid) {
            return valid ? AssignResult::applied : AssignResult::invalid;
        };
        auto assignLegacySetting = [&](std::string_view key, std::string_view value) {
            if (key == "wpm")
                return assigned(assignUnsigned(value, parsed.reading.wpm));
            if (key == "bright") {
                uint64_t legacyValue = 0;
                if (!parseUnsigned(value, legacyValue))
                    return AssignResult::invalid;
                parsed.interface.brightnessPercent = migrateBrightnessIndex(legacyValue);
                return AssignResult::applied;
            }
            if (key == "theme_id")
                return assigned(parseString(value, parsed.interface.selectedThemeId));
            if (key == "handed")
                return assigned(parseBool(value, parsed.reading.leftHanded));
            if (key == "phantom_on")
                return assigned(parseBool(value, parsed.reading.phantomWords));
            if (key == "ch_scroll_rev")
                return assigned(parseBool(value, parsed.reading.chapterScrollReversed));
            if (key == "read_bat")
                return assigned(parseBool(value, parsed.reading.batteryVisibleWhileReading));
            if (key == "read_ch")
                return assigned(parseBool(value, parsed.reading.chapterVisibleWhileReading));
            if (key == "read_pct")
                return assigned(parseBool(value, parsed.reading.progressVisibleWhileReading));
            if (key == "font_size")
                return assigned(assignUnsigned(value, parsed.reading.typography.fontSizeIndex));
            if (key == "type_hlt")
                return assigned(parseBool(value, parsed.reading.typography.focusHighlight));
            if (key == "type_trk") {
                int64_t parsedValue = 0;
                if (!parseSigned(value, parsedValue))
                    return AssignResult::invalid;
                parsed.reading.typography.tracking = parsedValue;
                return AssignResult::applied;
            }
            if (key == "type_anc")
                return assigned(assignUnsigned(value, parsed.reading.typography.anchor));
            if (key == "type_wid")
                return assigned(assignUnsigned(value, parsed.reading.typography.guideWidth));
            if (key == "type_gap")
                return assigned(assignUnsigned(value, parsed.reading.typography.guideGap));
            if (key == "pace_lms")
                return assigned(assignUnsigned(value, parsed.reading.pacing.longWordDelayMs));
            if (key == "pace_cms")
                return assigned(assignUnsigned(value, parsed.reading.pacing.complexWordDelayMs));
            if (key == "pace_pms")
                return assigned(assignUnsigned(value, parsed.reading.pacing.punctuationDelayMs));
            if (key == "stby_tmr")
                return assigned(assignUnsigned(value, parsed.interface.standbyTimerIndex));
            if (key == "wifi_ssid")
                return assigned(parseString(value, parsed.network.wifiSsid));
            if (key == "ota_auto")
                return assigned(parseBool(value, parsed.updates.checkOnStartup));
            if (key == "ota_owner")
                return assigned(parseString(value, parsed.updates.repositoryOwner));
            if (key == "ota_tag")
                return assigned(parseString(value, parsed.updates.releaseTag));

            uint64_t enumValue = 0;
            if (key == "ui_lang") {
                if (!parseUnsigned(value, enumValue))
                    return AssignResult::invalid;
                parsed.interface.locale = legacyUiLocale(enumValue);
                return AssignResult::applied;
            }
            if (key == "prog_md") {
                if (!parseUnsigned(value, enumValue))
                    return AssignResult::invalid;
                parsed.reading.footerMetric = enumValue < std::to_underlying(settings::FooterMetric::Count)
                                                ? static_cast<settings::FooterMetric>(enumValue)
                                                : settings::FooterMetric::percentage;
                return AssignResult::applied;
            }
            if (key == "bat_md") {
                if (!parseUnsigned(value, enumValue))
                    return AssignResult::invalid;
                parsed.reading.batteryLabel = enumValue < std::to_underlying(settings::BatteryLabel::Count)
                                                ? static_cast<settings::BatteryLabel>(enumValue)
                                                : settings::BatteryLabel::percentage;
                return AssignResult::applied;
            }
            if (key == "scrn_sv") {
                if (!parseUnsigned(value, enumValue))
                    return AssignResult::invalid;
                parsed.interface.screensaver = enumValue < std::to_underlying(standby::Kind::Count)
                                                 ? static_cast<standby::Kind>(enumValue)
                                                 : standby::Kind::life;
                return AssignResult::applied;
            }
            if (key == "pause_md") {
                if (!parseUnsigned(value, enumValue))
                    return AssignResult::invalid;
                parsed.reading.pauseMode = enumValue < std::to_underlying(settings::PauseMode::Count)
                                             ? static_cast<settings::PauseMode>(enumValue)
                                             : settings::PauseMode::sentenceEnd;
                return AssignResult::applied;
            }
            return key == "wifi_pass" ? AssignResult::applied : AssignResult::unknown;
        };

        std::string_view remaining = content;
        while (valid && !remaining.empty()) {
            std::string_view line = AsciiText::trim(nextLine(remaining));
            if (line.empty() || line.front() == '#')
                continue;
            const size_t separator = line.find('=');
            if (separator == std::string_view::npos) {
                valid = false;
                break;
            }
            const std::string_view key = AsciiText::trim(line.substr(0, separator));
            const std::string_view value = AsciiText::trim(line.substr(separator + 1));
            if (std::ranges::find(seenKeys, key) != seenKeys.end()) {
                valid = false;
                break;
            }
            seenKeys.emplace_back(key);
            if (key == "version") {
                valid = !versionSeen && value == "1";
                versionSeen = true;
            } else if (assignLegacySetting(key, value) == AssignResult::invalid)
                valid = false;
        }
        valid = valid && versionSeen;
        if (valid) {
            candidate = std::move(parsed);
            legacySettingsSeen = true;
        } else {
            configComplete = false;
            ESP_LOGW("migration", "preserved invalid %s", legacyConfigPath);
        }
    }

    bool settingsPersisted = true;
    if (legacySettingsSeen) {
        if (auto result = settingsStore_.replace(std::move(candidate), settings::SettingsSource::Programmatic);
            !result) {
            settingsPersisted = false;
            ESP_LOGE("migration", "settings import failed: %s", result.error().message.c_str());
        }
    }
    if (settingsPersisted && legacySecretsSeen) {
        settingsStore_.secrets() = std::move(secrets);
        if (auto result = settingsStore_.acceptSecretChanges(); !result) {
            settingsPersisted = false;
            ESP_LOGE("migration", "secret import failed: %s", result.error().message.c_str());
        }
    }
    if (settingsPersisted && (legacySettingsSeen || legacySecretsSeen)) {
        if (auto result = settingsStore_.flush(); !result) {
            settingsPersisted = false;
            ESP_LOGE("migration", "settings persistence failed: %s", result.error().message.c_str());
        }
    }

    constexpr std::array legacySettingKeys = {
        "wpm",       "bright",    "theme_id", "ui_lang",   "handed",   "phantom_on", "ch_scroll_rev", "prog_md",
        "bat_md",    "scrn_sv",   "read_bat", "read_ch",   "read_pct", "font_size",  "type_hlt",      "type_trk",
        "type_anc",  "type_wid",  "type_gap", "pace_lms",  "pace_cms", "pace_pms",   "pause_md",      "stby_tmr",
        "wifi_ssid", "wifi_pass", "ota_auto", "ota_owner", "ota_tag",  "cfg_hash",
    };
    if (settingsPersisted) {
        for (const char* key: legacySettingKeys) {
            if (prefs_.isKey(key) && !prefs_.remove(key)) {
                settingsPersisted = false;
                ESP_LOGE("migration", "could not remove legacy NVS key %s", key);
            }
        }
        if (legacyConfigPath != nullptr && configComplete && !settingsStore_.sdMirrorEnabled()) {
            configComplete = false;
            ESP_LOGW("migration", "preserved legacy settings because the TOML mirror is unavailable");
        }
        if (legacyConfigPath != nullptr && configComplete) {
            for (const char* path: {kLegacySettingsPath, kLegacySettingsBackupPath, kLegacySettingsTempPath}) {
                if (StorageFiles::fileExists(path) && !filesystem->remove(path)) {
                    configComplete = false;
                    ESP_LOGE("migration", "could not remove %s", path);
                }
            }
        }
    }

    bool themesComplete = filesystem != nullptr;
    if (filesystem != nullptr) {
        File directory = filesystem->open(StoragePaths::kThemesPath);
        if (directory && directory.isDirectory()) {
            std::vector<std::string> legacyThemePaths;
            while (File entry = directory.openNextFile()) {
                if (!entry.isDirectory()) {
                    std::string path = entry.path();
                    if (path.size() >= sizeof(kLegacyThemeExtension) - 1) {
                        const std::string_view suffix{path.data() + path.size() - (sizeof(kLegacyThemeExtension) - 1),
                                                      sizeof(kLegacyThemeExtension) - 1};
                        if (std::ranges::equal(suffix, std::string_view{kLegacyThemeExtension},
                                               [](char left, char right) {
                                                   return std::tolower(static_cast<unsigned char>(left))
                                                       == std::tolower(static_cast<unsigned char>(right));
                                               }))
                            legacyThemePaths.push_back(std::move(path));
                    }
                }
                entry.close();
            }
            directory.close();

            for (const std::string& legacyPath: legacyThemePaths) {
                std::string newPath = legacyPath.substr(0, legacyPath.size() - (sizeof(kLegacyThemeExtension) - 1));
                newPath += ui::themes::kThemeExtension;
                bool converted = false;
                if (StorageFiles::fileExists(newPath.c_str())) {
                    std::string current;
                    converted = readFile(*filesystem, newPath.c_str(), kMaxLegacyThemeBytes, current)
                             && ui::themes::decodeToml(current, ui::themes::themeIdFromPath(newPath)).has_value();
                    if (!converted) {
                        themesComplete = false;
                        ESP_LOGW("migration", "preserved %s beside invalid %s", legacyPath.c_str(), newPath.c_str());
                        continue;
                    }
                }
                if (!converted) {
                    std::string content;
                    ui::themes::ThemeFile theme;
                    theme.name.clear();
                    std::array<bool, 16> colorsSeen{};
                    bool magicSeen = false;
                    bool valid = readFile(*filesystem, legacyPath.c_str(), kMaxLegacyThemeBytes, content);
                    std::string_view remaining = content;
                    while (valid && !remaining.empty()) {
                        std::string_view line = AsciiText::trim(nextLine(remaining));
                        if (line.starts_with("\xEF\xBB\xBF"))
                            line.remove_prefix(3);
                        if (line.empty() || line.front() == '#')
                            continue;
                        if (!magicSeen) {
                            magicSeen = line == "@rtheme";
                            valid = magicSeen;
                            continue;
                        }
                        const size_t separator = line.find('=');
                        if (separator == std::string_view::npos)
                            continue;
                        const std::string_view key = AsciiText::trim(line.substr(0, separator));
                        std::string_view value = AsciiText::trim(line.substr(separator + 1));
                        if (key == "name") {
                            theme.name.assign(value);
                            continue;
                        }

                        const bool rgb = value.size() == 7 && value.front() == '#';
                        const bool encoded =
                            value.size() == 6 && value[0] == '0' && (value[1] == 'x' || value[1] == 'X');
                        if (!rgb && !encoded) {
                            valid = false;
                            break;
                        }
                        value.remove_prefix(rgb ? 1 : 2);
                        const auto parsedColor = AsciiText::parseUnsigned<uint32_t>(value, 16);
                        if (!parsedColor || (!rgb && *parsedColor > UINT16_MAX)) {
                            valid = false;
                            break;
                        }
                        const ui::themes::Rgb565 color =
                            rgb ? ui::themes::rgb565(*parsedColor >> 16U, *parsedColor >> 8U, *parsedColor)
                                : static_cast<uint16_t>(*parsedColor);
                        auto setColor = [&](std::string_view expected, auto& field, size_t index) {
                            if (key != expected)
                                return false;
                            field = color;
                            colorsSeen[index] = true;
                            return true;
                        };
                        const bool known = setColor("background", theme.colors.background, 0)
                                        || setColor("foreground", theme.colors.foreground, 1)
                                        || setColor("muted", theme.colors.muted, 2)
                                        || setColor("subtle", theme.colors.subtle, 3)
                                        || setColor("accent", theme.colors.accent, 4)
                                        || setColor("accent_bar", theme.colors.accentBar, 5)
                                        || setColor("break_accent", theme.colors.breakAccent, 6)
                                        || setColor("on_accent", theme.colors.onAccent, 7)
                                        || setColor("surface", theme.colors.surface, 8)
                                        || setColor("surface_muted", theme.colors.surfaceMuted, 9)
                                        || setColor("surface_active", theme.colors.surfaceActive, 10)
                                        || setColor("outline", theme.colors.outline, 11)
                                        || setColor("guide", theme.colors.guide, 12)
                                        || setColor("guide_focus", theme.colors.guideFocus, 13)
                                        || setColor("phantom", theme.colors.phantom, 14)
                                        || setColor("progress_track", theme.colors.progressTrack, 15);
                        if (!known)
                            continue;
                    }
                    valid = valid && magicSeen && !theme.name.empty() && std::ranges::all_of(colorsSeen, [](bool seen) {
                                return seen;
                            });
                    if (valid) {
                        auto encodedTheme = ui::themes::encodeToml(theme);
                        if (encodedTheme) {
                            const std::string temporaryPath = newPath + ".tmp";
                            filesystem->remove(temporaryPath.c_str());
                            File output = filesystem->open(temporaryPath.c_str(), FILE_WRITE);
                            if (output && !output.isDirectory()) {
                                const size_t count =
                                    output.write(reinterpret_cast<const uint8_t*>(encodedTheme->data()),
                                                 encodedTheme->size());
                                output.flush();
                                output.close();
                                converted = count == encodedTheme->size()
                                         && filesystem->rename(temporaryPath.c_str(), newPath.c_str());
                            } else if (output) {
                                output.close();
                            }
                            if (!converted)
                                filesystem->remove(temporaryPath.c_str());
                        }
                    }
                }
                if (!converted) {
                    themesComplete = false;
                    ESP_LOGW("migration", "preserved invalid theme %s", legacyPath.c_str());
                } else if (!filesystem->remove(legacyPath.c_str())) {
                    themesComplete = false;
                    ESP_LOGE("migration", "could not remove %s", legacyPath.c_str());
                } else {
                    ESP_LOGI("migration", "converted theme %s", legacyPath.c_str());
                }
            }
        } else if (directory) {
            directory.close();
        }
    }

    bool rssComplete = filesystem != nullptr;
    if (filesystem != nullptr) {
        const char* legacyRssPath = nullptr;
        for (const char* path: {kLegacyRssPath, kLegacyRssBackupPath, kLegacyRssTempPath}) {
            if (StorageFiles::fileExists(path)) {
                legacyRssPath = path;
                break;
            }
        }

        if (legacyRssPath != nullptr) {
            bool converted = false;
            if (StorageFiles::fileExists(StoragePaths::kRssConfigPath)) {
                converted = rss::load(*filesystem).has_value();
            } else {
                std::string content;
                if (readFile(*filesystem, legacyRssPath, kMaxLegacyRssBytes, content)) {
                    rss::Config migrated;
                    std::string_view remaining = content;
                    while (!remaining.empty()) {
                        std::string_view line = AsciiText::trim(nextLine(remaining));
                        if (line.empty() || line.front() == '#')
                            continue;
                        if (line.starts_with("feed="))
                            line = AsciiText::trim(line.substr(5));
                        migrated.feeds.emplace_back(line);
                    }
                    converted = rss::save(*filesystem, std::move(migrated)).has_value();
                }
            }

            if (!converted) {
                rssComplete = false;
                ESP_LOGW("migration", "preserved invalid RSS config %s", legacyRssPath);
            } else {
                for (const char* path: {kLegacyRssPath, kLegacyRssBackupPath, kLegacyRssTempPath}) {
                    if (StorageFiles::fileExists(path) && !filesystem->remove(path)) {
                        rssComplete = false;
                        ESP_LOGE("migration", "could not remove %s", path);
                    }
                }
                if (rssComplete)
                    ESP_LOGI("migration", "converted RSS feeds to %s", StoragePaths::kRssConfigPath);
            }
        }
    }

    bool focusComplete = filesystem != nullptr;
    if (filesystem != nullptr) {
        const char* legacyFocusPath = nullptr;
        for (const char* path: {kLegacyFocusPath, kLegacyFocusBackupPath, kLegacyFocusTempPath}) {
            if (StorageFiles::fileExists(path)) {
                legacyFocusPath = path;
                break;
            }
        }

        if (legacyFocusPath != nullptr) {
            bool converted = false;
            if (StorageFiles::fileExists(StoragePaths::kFocusConfigPath)) {
                converted = focus::load(*filesystem).has_value();
            } else {
                std::string content;
                LegacyFocusConfig legacy;
                if (readFile(*filesystem, legacyFocusPath, kMaxLegacyFocusBytes, content)
                    && !glz::read<glz::opts{.format = glz::TOML, .error_on_unknown_keys = false}>(legacy, content)
                    && legacy.version == 1 && !legacy.timer.empty()) {
                    focus::Timers migrated;
                    migrated.timers.reserve(std::min(legacy.timer.size(), focus::kMaxTimers));
                    for (const LegacyFocusTimer& source: legacy.timer) {
                        if (migrated.timers.size() == focus::kMaxTimers)
                            break;
                        focus::Timer timer;
                        timer.name = source.name;
                        timer.focusMinutes = source.focus_minutes;
                        timer.breakMinutes = source.break_minutes;
                        timer.rounds = source.rounds;
                        migrated.timers.push_back(std::move(timer));
                    }
                    converted = focus::valid(migrated) && focus::save(*filesystem, migrated).has_value();
                }
            }

            if (!converted) {
                focusComplete = false;
                ESP_LOGW("migration", "preserved invalid focus config %s", legacyFocusPath);
            } else {
                for (const char* path: {kLegacyFocusPath, kLegacyFocusBackupPath, kLegacyFocusTempPath}) {
                    if (StorageFiles::fileExists(path) && !filesystem->remove(path)) {
                        focusComplete = false;
                        ESP_LOGE("migration", "could not remove %s", path);
                    }
                }
                if (focusComplete)
                    ESP_LOGI("migration", "converted focus timers to %s", StoragePaths::kFocusConfigPath);
            }
        }
    }

    bool booksComplete = filesystem != nullptr;
    if (filesystem != nullptr) {
        storage_.refreshBooks(false);
        for (size_t index = 0; index < storage_.bookCount(); ++index) {
            const std::string bookPath = storage_.bookPath(index);
            const std::string legacyPath = StoragePaths::siblingPathWithExtension(bookPath, kLegacyProgressExtension);
            const std::string newPath = StoragePaths::bookStatePathFor(bookPath);
            const bool legacyFileSeen = StorageFiles::fileExists(legacyPath.c_str());

            if (StorageFiles::fileExists(newPath.c_str()) && !migrateBookStateOverrides(*filesystem, newPath)) {
                booksComplete = false;
                ESP_LOGW("migration", "preserved invalid book state %s", newPath.c_str());
                continue;
            }

            uint32_t pathHash = 2166136261UL;
            for (const unsigned char character: bookPath) {
                pathHash ^= character;
                pathHash *= 16777619UL;
            }
            auto progressKey = [&](char prefix) {
                std::array<char, 11> key{};
                std::snprintf(key.data(), key.size(), "%c%08lx", prefix, static_cast<unsigned long>(pathHash));
                return key;
            };
            const auto positionKey = progressKey('p');
            const auto countKey = progressKey('c');
            const auto sizeKey = progressKey('s');
            const auto fingerprintKey = progressKey('f');
            const bool legacyNvsSeen = prefs_.isKey(positionKey.data()) || prefs_.isKey(countKey.data())
                                    || prefs_.isKey(sizeKey.data()) || prefs_.isKey(fingerprintKey.data());
            if (!legacyFileSeen && !legacyNvsSeen)
                continue;

            ReadingProgress::BookIdentity identity;
            uint32_t wordIndex = 0;
            bool recoverable = false;
            if (legacyFileSeen) {
                std::string line;
                if (readFile(*filesystem, legacyPath.c_str(), 256, line)) {
                    char magic[8]{};
                    unsigned long version = 0;
                    unsigned long sourceSize = 0;
                    unsigned long sourceFingerprint = 0;
                    unsigned long wordCount = 0;
                    unsigned long savedWordIndex = 0;
                    if (std::sscanf(line.c_str(), "%7s %lu %lu %lu %lu %lu", magic, &version, &sourceSize,
                                    &sourceFingerprint, &wordCount, &savedWordIndex)
                            == 6
                        && std::string_view{magic} == "rpos" && version == 1 && sourceSize > 0 && wordCount > 0) {
                        identity = {static_cast<uint32_t>(sourceSize), static_cast<uint32_t>(sourceFingerprint),
                                    static_cast<uint32_t>(wordCount)};
                        wordIndex = static_cast<uint32_t>(savedWordIndex);
                        recoverable = true;
                    }
                }
            }
            if (!recoverable && prefs_.isKey(positionKey.data()) && prefs_.isKey(countKey.data())
                && prefs_.isKey(sizeKey.data()) && prefs_.isKey(fingerprintKey.data())) {
                identity = {prefs_.getUInt(sizeKey.data()), prefs_.getUInt(fingerprintKey.data()),
                            prefs_.getUInt(countKey.data())};
                wordIndex = prefs_.getUInt(positionKey.data());
                recoverable = identity.sourceSize > 0 && identity.wordCount > 0;
            }

            bool converted = false;
            if (StorageFiles::fileExists(newPath.c_str())) {
                std::string current;
                ReadingSession::BookState currentState;
                converted =
                    readFile(*filesystem, newPath.c_str(), 2048, current)
                    && !glz::read<glz::opts{.format = glz::TOML, .error_on_unknown_keys = false}>(currentState, current)
                    && currentState.sourceSize > 0 && currentState.wordCount > 0;
            }
            if (!converted && recoverable)
                converted = ReadingProgress::writeBookStatePosition(bookPath, identity, wordIndex).has_value();
            if (!converted && legacyFileSeen) {
                booksComplete = false;
                ESP_LOGW("migration", "preserved invalid progress %s", legacyPath.c_str());
                continue;
            }
            if (legacyFileSeen && !filesystem->remove(legacyPath.c_str())) {
                booksComplete = false;
                ESP_LOGE("migration", "could not remove %s", legacyPath.c_str());
            }
            if (converted || !recoverable) {
                for (const char* key: {positionKey.data(), countKey.data(), sizeKey.data(), fingerprintKey.data()}) {
                    if (prefs_.isKey(key) && !prefs_.remove(key)) {
                        booksComplete = false;
                        ESP_LOGE("migration", "could not remove legacy progress key %s", key);
                    }
                }
            }
        }

        if (booksComplete) {
            std::vector<std::string> orphanedProgressKeys;
            nvs_iterator_t iterator = nullptr;
            esp_err_t result = nvs_entry_find("nvs", settings::kStateNvsNamespace, NVS_TYPE_ANY, &iterator);
            while (result == ESP_OK) {
                nvs_entry_info_t info{};
                if (nvs_entry_info(iterator, &info) == ESP_OK) {
                    const std::string_view key{info.key};
                    const bool legacyProgressKey =
                        key.size() == 9
                        && (key.front() == 'p' || key.front() == 'c' || key.front() == 's' || key.front() == 'f')
                        && std::ranges::all_of(key.substr(1), [](unsigned char character) {
                               return std::isxdigit(character) != 0;
                           });
                    if (legacyProgressKey)
                        orphanedProgressKeys.emplace_back(key);
                }
                result = nvs_entry_next(&iterator);
            }
            nvs_release_iterator(iterator);
            if (result != ESP_ERR_NVS_NOT_FOUND)
                booksComplete = false;
            for (const std::string& key: orphanedProgressKeys) {
                if (!prefs_.remove(key.c_str())) {
                    booksComplete = false;
                    ESP_LOGE("migration", "could not remove orphaned progress key %s", key.c_str());
                }
            }
        }
    }

    if (settingsPersisted && configComplete && themesComplete && rssComplete && focusComplete && booksComplete) {
        if (prefs_.putBool(kMigrationMarker, true) > 0) {
            for (const char* marker: kPreviousMigrationMarkers) {
                if (prefs_.isKey(marker))
                    prefs_.remove(marker);
            }
            ESP_LOGD("migration", "legacy storage migration complete");
        } else {
            ESP_LOGE("migration", "could not save completion marker; migration will retry");
        }
    } else if (filesystem == nullptr) {
        ESP_LOGD("migration", "SD migration deferred until storage is available");
    }
}
