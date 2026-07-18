#include <unity.h>

#include <glaze/toml.hpp>

#include <optional>
#include <string>

#include "settings/SettingsCodec.h"
#include "settings/SettingsGlaze.h"
#include "sync/CompanionSyncJson.h"

namespace {

    struct BookStateProbe {
        uint32_t wordIndex = 0;
        std::optional<settings::TypographySettings> bookTypographyOverride;
    };

} // namespace

void setUp() {}
void tearDown() {}

void test_defaults_round_trip_through_toml_and_json() {
    const settings::DeviceSettings defaults;
    auto toml = settings::codec::encodeToml(defaults, settings::SettingsSource::Programmatic);
    TEST_ASSERT_TRUE_MESSAGE(toml.has_value(), toml ? "" : toml.error().message.c_str());
    TEST_ASSERT_EQUAL(std::string::npos, toml->find("schemaVersion"));
    TEST_ASSERT_NOT_EQUAL(std::string::npos, toml->find("batteryIconVisible = true"));
    auto fromToml = settings::codec::decodeToml(*toml, settings::SettingsSource::Sd);
    TEST_ASSERT_TRUE_MESSAGE(fromToml.has_value(), fromToml ? "" : fromToml.error().message.c_str());
    TEST_ASSERT_TRUE(defaults == *fromToml);

    auto json = settings::codec::encodeJson(defaults, settings::SettingsSource::Programmatic);
    TEST_ASSERT_TRUE_MESSAGE(json.has_value(), json ? "" : json.error().message.c_str());
    auto fromJson = settings::codec::decodeJson(*json, settings::SettingsSource::Companion);
    TEST_ASSERT_TRUE_MESSAGE(fromJson.has_value(), fromJson ? "" : fromJson.error().message.c_str());
    TEST_ASSERT_TRUE(defaults == *fromJson);
}

void test_enum_names_are_human_readable() {
    settings::DeviceSettings value;
    value.reading.pauseMode = settings::PauseMode::instant;
    value.interface.screensaver = standby::Kind::reaction;
    auto toml = settings::codec::encodeToml(value, settings::SettingsSource::Programmatic);
    TEST_ASSERT_TRUE(toml.has_value());
    TEST_ASSERT_NOT_EQUAL(std::string::npos, toml->find("pauseMode = \"instant\""));
    TEST_ASSERT_NOT_EQUAL(std::string::npos, toml->find("screensaver = \"reaction\""));
}

void test_missing_fields_retain_defaults() {
    auto value = settings::codec::decodeToml("obsolete = true\n[reading]\nwpm = 300\n", settings::SettingsSource::Sd);
    TEST_ASSERT_TRUE_MESSAGE(value.has_value(), value ? "" : value.error().message.c_str());
    TEST_ASSERT_EQUAL_UINT16(300, value->reading.wpm);
    TEST_ASSERT_TRUE(value->reading.batteryIconVisible);
    TEST_ASSERT_EQUAL_STRING("literata", value->reading.typography.fontId.c_str());
    auto canonical = settings::codec::encodeToml(*value, settings::SettingsSource::Programmatic);
    TEST_ASSERT_TRUE(canonical.has_value());
    TEST_ASSERT_EQUAL(std::string::npos, canonical->find("obsolete"));
    TEST_ASSERT_NOT_EQUAL(std::string::npos, canonical->find("batteryIconVisible = true"));
}

void test_bounded_values_clamp_during_deserialization() {
    auto value = settings::codec::decodeToml("[reading]\nwpm = 9\n", settings::SettingsSource::Sd);
    TEST_ASSERT_TRUE_MESSAGE(value.has_value(), value ? "" : value.error().message.c_str());
    TEST_ASSERT_EQUAL_UINT16(10, value->reading.wpm);
}

void test_bounded_values_clamp_on_every_assignment() {
    settings::BoundedValue<uint8_t, 5, 100, 5> brightness{-1};
    TEST_ASSERT_EQUAL_UINT8(5, brightness);
    brightness = 101;
    TEST_ASSERT_EQUAL_UINT8(100, brightness);
    brightness.cycle();
    TEST_ASSERT_EQUAL_UINT8(5, brightness);
}

void test_invalid_input_cannot_mutate_a_live_value() {
    settings::DeviceSettings live;
    live.reading.wpm = 450;
    auto candidate = settings::codec::decodeToml("[reading]\nwpm = 600\n[interface]\nbrightnessPercent = 101\n",
                                                 settings::SettingsSource::Sd);
    TEST_ASSERT_TRUE(candidate.has_value());
    TEST_ASSERT_EQUAL_UINT16(600, candidate->reading.wpm);
    TEST_ASSERT_EQUAL_UINT8(100, candidate->interface.brightnessPercent);
    TEST_ASSERT_EQUAL_UINT16(450, live.reading.wpm);
}

void test_unknown_keys_are_ignored_and_invalid_enums_still_fail() {
    auto unknown =
        settings::codec::decodeJson(R"({"surprise":true})", settings::SettingsSource::Companion);
    TEST_ASSERT_TRUE(unknown.has_value());
    TEST_ASSERT_TRUE(unknown->reading.batteryIconVisible);

    auto invalidEnum = settings::codec::decodeJson(R"({"reading":{"pauseMode":"later"}})",
                                                   settings::SettingsSource::Companion);
    TEST_ASSERT_FALSE(invalidEnum.has_value());
    TEST_ASSERT_EQUAL(settings::SettingsErrorCategory::InvalidEnum, invalidEnum.error().category);
}

void test_companion_envelope_encodes_lvalue_without_owning_it() {
    const companion::api::NetworkResponse response{true};
    std::string json;
    TEST_ASSERT_TRUE(companion::api::encodeData(response, json).has_value());
    TEST_ASSERT_EQUAL_STRING("{\"data\":{\"passwordSet\":true}}", json.c_str());
}

void test_companion_theme_list_uses_ids_and_names() {
    const companion::api::ThemesResponse response{{{"default", "Default"}, {"night", "Night"}}};
    std::string json;
    TEST_ASSERT_TRUE(companion::api::encodeData(response, json).has_value());
    TEST_ASSERT_EQUAL_STRING(
        R"({"data":{"themes":[{"id":"default","name":"Default"},{"id":"night","name":"Night"}]}})",
        json.c_str());
}

void test_companion_font_list_uses_ids_and_names() {
    const companion::api::FontsResponse response{{{"literata", "Literata"}, {"atkinson", "Atkinson Hyperlegible"}}};
    std::string json;
    TEST_ASSERT_TRUE(companion::api::encodeData(response, json).has_value());
    TEST_ASSERT_EQUAL_STRING(
        R"({"data":{"fonts":[{"id":"literata","name":"Literata"},{"id":"atkinson","name":"Atkinson Hyperlegible"}]}})",
        json.c_str());
}

void test_secrets_are_not_part_of_public_documents() {
    settings::DeviceSettings publicSettings;
    publicSettings.network.wifiSsid = "reader";
    auto toml = settings::codec::encodeToml(publicSettings, settings::SettingsSource::Programmatic);
    auto json = settings::codec::encodeJson(publicSettings, settings::SettingsSource::Programmatic);
    TEST_ASSERT_TRUE(toml.has_value());
    TEST_ASSERT_TRUE(json.has_value());
    TEST_ASSERT_EQUAL(std::string::npos, toml->find("wifiPassword"));
    TEST_ASSERT_EQUAL(std::string::npos, json->find("wifiPassword"));
}

void test_book_typography_override_precedence_and_reset() {
    settings::TypographySettings theme;
    theme.fontId = "theme-font";
    std::optional<settings::TypographySettings> book;

    TEST_ASSERT_EQUAL_STRING("theme-font", settings::effectiveTypography(book, theme).fontId.c_str());

    book = theme;
    book->fontId = "book-font";
    TEST_ASSERT_EQUAL_STRING("book-font", settings::effectiveTypography(book, theme).fontId.c_str());

    book.reset();
    TEST_ASSERT_EQUAL_STRING("theme-font", settings::effectiveTypography(book, theme).fontId.c_str());
}

void test_optional_book_typography_round_trips_through_toml() {
    BookStateProbe state;
    state.wordIndex = 42;
    state.bookTypographyOverride.emplace();
    state.bookTypographyOverride->fontId = "book-font";

    std::string toml;
    TEST_ASSERT_FALSE(glz::write_toml(state, toml));
    BookStateProbe decoded;
    TEST_ASSERT_FALSE(glz::read_toml(decoded, toml));
    TEST_ASSERT_EQUAL_UINT32(42, decoded.wordIndex);
    TEST_ASSERT_TRUE(decoded.bookTypographyOverride.has_value());
    TEST_ASSERT_EQUAL_STRING("book-font", decoded.bookTypographyOverride->fontId.c_str());
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_defaults_round_trip_through_toml_and_json);
    RUN_TEST(test_enum_names_are_human_readable);
    RUN_TEST(test_missing_fields_retain_defaults);
    RUN_TEST(test_bounded_values_clamp_during_deserialization);
    RUN_TEST(test_bounded_values_clamp_on_every_assignment);
    RUN_TEST(test_invalid_input_cannot_mutate_a_live_value);
    RUN_TEST(test_unknown_keys_are_ignored_and_invalid_enums_still_fail);
    RUN_TEST(test_companion_envelope_encodes_lvalue_without_owning_it);
    RUN_TEST(test_companion_theme_list_uses_ids_and_names);
    RUN_TEST(test_companion_font_list_uses_ids_and_names);
    RUN_TEST(test_secrets_are_not_part_of_public_documents);
    RUN_TEST(test_book_typography_override_precedence_and_reset);
    RUN_TEST(test_optional_book_typography_round_trips_through_toml);
    return UNITY_END();
}
