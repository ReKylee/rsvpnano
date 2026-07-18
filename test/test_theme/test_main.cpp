#include <unity.h>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "ui/Theme.h"

namespace fs = std::filesystem;

namespace {

    std::string validThemeText() {
        return R"(schemaVersion = 1
name = "Valid Theme"

[typography]
fontId = "atkinson"
fontSizeIndex = 1
focusHighlight = true
tracking = 1
anchor = 33
guideWidth = 24
guideGap = 4

[colors]
background = "#000000"
foreground = "#FFFFFF"
muted = "#888888"
subtle = "#666666"
accent = "#FF0000"
accentBar = "#FF0000"
breakAccent = "#00FF00"
onAccent = "#000000"
surface = "#101010"
surfaceMuted = "#202020"
surfaceActive = "#303030"
outline = "#404040"
guide = "#505050"
guideFocus = "#FF0000"
phantom = "#606060"
progressTrack = "#707070"
)";
    }

    ui::themes::Theme parseValidTheme() {
        auto theme = ui::themes::decodeToml(validThemeText(), "valid");
        TEST_ASSERT_TRUE_MESSAGE(theme.has_value(), theme ? "" : theme.error().message.c_str());
        return std::move(*theme);
    }

    std::string readFile(const fs::path& path) {
        std::ifstream file(path, std::ios::binary);
        std::ostringstream buffer;
        buffer << file.rdbuf();
        return buffer.str();
    }

    void replaceText(std::string& text, std::string_view from, std::string_view to) {
        const size_t position = text.find(from);
        TEST_ASSERT_NOT_EQUAL(std::string::npos, position);
        text.replace(position, from.size(), to);
    }

} // namespace

void setUp() {}
void tearDown() {}

void test_parses_named_colors_and_typography() {
    const auto theme = parseValidTheme();
    TEST_ASSERT_EQUAL_STRING("valid", theme.id.c_str());
    TEST_ASSERT_EQUAL_STRING("Valid Theme", theme.definition.name.c_str());
    TEST_ASSERT_EQUAL_STRING("atkinson", theme.definition.typography.fontId.c_str());
    TEST_ASSERT_EQUAL_UINT16(0x0000, theme.definition.colors.background.value);
    TEST_ASSERT_EQUAL_UINT16(0xFFFF, theme.definition.colors.foreground.value);
    TEST_ASSERT_EQUAL_UINT16(0xF800, theme.definition.colors.accent.value);
}

void test_round_trip_is_canonical_toml() {
    const auto original = parseValidTheme();
    auto encoded = ui::themes::encodeToml(original.definition);
    TEST_ASSERT_TRUE_MESSAGE(encoded.has_value(), encoded ? "" : encoded.error().message.c_str());
    TEST_ASSERT_NOT_EQUAL(std::string::npos, encoded->find("[typography]"));
    TEST_ASSERT_NOT_EQUAL(std::string::npos, encoded->find("[colors]"));
    TEST_ASSERT_NOT_EQUAL(std::string::npos, encoded->find("\"#FF0000\""));
    auto decoded = ui::themes::decodeToml(*encoded, original.id);
    TEST_ASSERT_TRUE_MESSAGE(decoded.has_value(), decoded ? "" : decoded.error().message.c_str());
    TEST_ASSERT_TRUE(original.definition == decoded->definition);
}

void test_missing_fields_retain_application_defaults() {
    settings::TypographySettings defaults;
    defaults.fontId = "application-font";
    defaults.tracking = 2;
    auto theme = ui::themes::decodeToml("schemaVersion = 1\nname = \"Sparse\"\n", "sparse", defaults);
    TEST_ASSERT_TRUE_MESSAGE(theme.has_value(), theme ? "" : theme.error().message.c_str());
    TEST_ASSERT_EQUAL_STRING("application-font", theme->definition.typography.fontId.c_str());
    TEST_ASSERT_EQUAL_INT(2, theme->definition.typography.tracking);
    TEST_ASSERT_EQUAL_UINT16(0xFFFF, theme->definition.colors.foreground.value);
}

void test_rejects_bad_color_transactionally() {
    std::string text = validThemeText();
    replaceText(text, "accent = \"#FF0000\"", "accent = \"#FF00ZZ\"");
    auto theme = ui::themes::decodeToml(text, "bad-color");
    TEST_ASSERT_FALSE(theme.has_value());
    TEST_ASSERT_EQUAL(settings::SettingsErrorCategory::Constraint, theme.error().category);
}

void test_rejects_unknown_key() {
    std::string text = validThemeText();
    text += "\n[extra]\ndecorativeOrb = \"#FF00FF\"\n";
    auto theme = ui::themes::decodeToml(text, "unknown");
    TEST_ASSERT_FALSE(theme.has_value());
    TEST_ASSERT_EQUAL(settings::SettingsErrorCategory::UnknownKey, theme.error().category);
}

void test_theme_id_from_path() {
    TEST_ASSERT_EQUAL_STRING("catppuccin-mocha",
                             ui::themes::themeIdFromPath("/themes/Catppuccin-Mocha.toml").c_str());
}

void test_repo_themes_parse() {
    std::vector<fs::path> themeFiles;
    for (const fs::directory_entry& entry: fs::directory_iterator("themes")) {
        if (entry.is_regular_file() && entry.path().extension() == ".toml")
            themeFiles.push_back(entry.path());
    }
    TEST_ASSERT_GREATER_OR_EQUAL_UINT32(10, static_cast<uint32_t>(themeFiles.size()));

    for (const fs::path& path: themeFiles) {
        const std::string id = ui::themes::themeIdFromPath(path.generic_string());
        auto theme = ui::themes::decodeToml(readFile(path), id);
        TEST_ASSERT_TRUE_MESSAGE(theme.has_value(), path.string().c_str());
    }
}

void test_theme_catalog_references_existing_files() {
    const std::string json = readFile(fs::path("themes") / "index.json");
    size_t pos = 0;
    uint32_t fileCount = 0;
    while ((pos = json.find("\"file\"", pos)) != std::string::npos) {
        const size_t colon = json.find(':', pos);
        const size_t open = json.find('"', colon + 1);
        const size_t close = json.find('"', open + 1);
        const std::string filename = json.substr(open + 1, close - open - 1);
        TEST_ASSERT_TRUE_MESSAGE(fs::exists(fs::path("themes") / filename), filename.c_str());
        ++fileCount;
        pos = close + 1;
    }
    TEST_ASSERT_GREATER_OR_EQUAL_UINT32(10, fileCount);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_parses_named_colors_and_typography);
    RUN_TEST(test_round_trip_is_canonical_toml);
    RUN_TEST(test_missing_fields_retain_application_defaults);
    RUN_TEST(test_rejects_bad_color_transactionally);
    RUN_TEST(test_rejects_unknown_key);
    RUN_TEST(test_theme_id_from_path);
    RUN_TEST(test_repo_themes_parse);
    RUN_TEST(test_theme_catalog_references_existing_files);
    return UNITY_END();
}
