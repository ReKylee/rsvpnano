#include <unity.h>

#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "ui/Theme.h"

#if __has_include(<filesystem>)
#include <filesystem>
namespace fs = std::filesystem;
#else
#error "theme tests require std::filesystem"
#endif

namespace {

String validThemeText() {
  return String(
      "@rtheme\n"
      "name=Valid Theme\n"
      "typeface=atkinson\n"
      "low_brightness=true\n"
      "background=#000000\n"
      "foreground=#ffffff\n"
      "muted=#888888\n"
      "subtle=#666666\n"
      "accent=#ff0000\n"
      "accent_bar=#ff0000\n"
      "break_accent=#00ff00\n"
      "on_accent=#000000\n"
      "surface=#101010\n"
      "surface_muted=#202020\n"
      "surface_active=#303030\n"
      "outline=#404040\n"
      "guide=#505050\n"
      "guide_focus=#ff0000\n"
      "phantom=#606060\n"
      "progress_track=#707070\n");
}

ui::themes::Theme parseValidTheme() {
  ui::themes::Theme theme;
  String error;
  TEST_ASSERT_TRUE_MESSAGE(ui::themes::parseThemeText(validThemeText(), "valid", theme, error), error.c_str());
  return theme;
}

String readFile(const fs::path &path) {
  std::ifstream file(path, std::ios::binary);
  std::ostringstream buffer;
  buffer << file.rdbuf();
  return String(buffer.str());
}

} // namespace

void setUp() {}

void tearDown() {}

void test_parses_valid_theme() {
  const ui::themes::Theme theme = parseValidTheme();
  TEST_ASSERT_EQUAL_STRING("valid", theme.id.c_str());
  TEST_ASSERT_EQUAL_STRING("Valid Theme", theme.name.c_str());
  TEST_ASSERT_EQUAL_UINT16(0x0000, theme.colors[static_cast<size_t>(ui::themes::ColorRole::Background)]);
  TEST_ASSERT_EQUAL_UINT16(0xFFFF, theme.colors[static_cast<size_t>(ui::themes::ColorRole::Foreground)]);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(ui::themes::ReaderTypeface::AtkinsonHyperlegible),
                          static_cast<uint8_t>(theme.typeface));
  TEST_ASSERT_TRUE(theme.lowBrightness);
}

void test_accepts_rgb565_literal() {
  String text = validThemeText();
  text.replace("accent=#ff0000", "accent=0x07E0");
  ui::themes::Theme theme;
  String error;
  TEST_ASSERT_TRUE_MESSAGE(ui::themes::parseThemeText(text, "rgb565", theme, error), error.c_str());
  TEST_ASSERT_EQUAL_UINT16(0x07E0, theme.colors[static_cast<size_t>(ui::themes::ColorRole::Accent)]);
}

void test_rejects_missing_magic() {
  String text = validThemeText();
  text.replace("@rtheme\n", "");
  ui::themes::Theme theme;
  String error;
  TEST_ASSERT_FALSE(ui::themes::parseThemeText(text, "missing", theme, error));
  TEST_ASSERT_EQUAL_STRING("first content line must be @rtheme", error.c_str());
}

void test_rejects_versioned_magic() {
  String text = validThemeText();
  text.replace("@rtheme", "@rtheme 1");
  ui::themes::Theme theme;
  String error;
  TEST_ASSERT_FALSE(ui::themes::parseThemeText(text, "versioned", theme, error));
  TEST_ASSERT_EQUAL_STRING("first content line must be @rtheme", error.c_str());
}

void test_rejects_missing_role() {
  String text = validThemeText();
  text.replace("progress_track=#707070\n", "");
  ui::themes::Theme theme;
  String error;
  TEST_ASSERT_FALSE(ui::themes::parseThemeText(text, "missing-role", theme, error));
  TEST_ASSERT_EQUAL_STRING("missing color progress_track", error.c_str());
}

void test_rejects_missing_typeface() {
  String text = validThemeText();
  text.replace("typeface=atkinson\n", "");
  ui::themes::Theme theme;
  String error;
  TEST_ASSERT_FALSE(ui::themes::parseThemeText(text, "missing-typeface", theme, error));
  TEST_ASSERT_EQUAL_STRING("missing typeface", error.c_str());
}

void test_rejects_invalid_typeface() {
  String text = validThemeText();
  text.replace("typeface=atkinson", "typeface=comic_sans");
  ui::themes::Theme theme;
  String error;
  TEST_ASSERT_FALSE(ui::themes::parseThemeText(text, "invalid-typeface", theme, error));
  TEST_ASSERT_EQUAL_STRING("typeface must be standard, open_dyslexic, or atkinson", error.c_str());
}

void test_rejects_inherit_typeface() {
  String text = validThemeText();
  text.replace("typeface=atkinson", "typeface=inherit");
  ui::themes::Theme theme;
  String error;
  TEST_ASSERT_FALSE(ui::themes::parseThemeText(text, "inherit-typeface", theme, error));
  TEST_ASSERT_EQUAL_STRING("typeface must be standard, open_dyslexic, or atkinson", error.c_str());
}

void test_rejects_bad_color() {
  String text = validThemeText();
  text.replace("accent=#ff0000", "accent=#ff00zz");
  ui::themes::Theme theme;
  String error;
  TEST_ASSERT_FALSE(ui::themes::parseThemeText(text, "bad-color", theme, error));
  TEST_ASSERT_EQUAL_STRING("invalid color for accent", error.c_str());
}

void test_unknown_key_is_ignored() {
  String text = validThemeText();
  text += "decorative_orb=#ff00ff\n";
  ui::themes::Theme theme;
  String error;
  TEST_ASSERT_TRUE_MESSAGE(ui::themes::parseThemeText(text, "unknown", theme, error), error.c_str());
}

void test_rejects_bad_low_brightness() {
  String text = validThemeText();
  text.replace("low_brightness=true", "low_brightness=maybe");
  ui::themes::Theme theme;
  String error;
  TEST_ASSERT_FALSE(ui::themes::parseThemeText(text, "bad-low-brightness", theme, error));
  TEST_ASSERT_EQUAL_STRING("low_brightness must be true or false", error.c_str());
}

void test_theme_id_from_path() {
  TEST_ASSERT_EQUAL_STRING("catppuccin-mocha",
                           ui::themes::themeIdFromPath("/themes/catppuccin-mocha.rtheme").c_str());
}

void test_repo_themes_parse() {
  std::vector<fs::path> themeFiles;
  for (const fs::directory_entry &entry : fs::directory_iterator("themes")) {
    if (entry.is_regular_file() && entry.path().extension() == ".rtheme") {
      themeFiles.push_back(entry.path());
    }
  }
  TEST_ASSERT_GREATER_OR_EQUAL_UINT32(10, static_cast<uint32_t>(themeFiles.size()));

  for (const fs::path &path : themeFiles) {
    ui::themes::Theme theme;
    String error;
    const String id = ui::themes::themeIdFromPath(String(path.generic_string()));
    TEST_ASSERT_TRUE_MESSAGE(ui::themes::parseThemeText(readFile(path), id, theme, error), path.string().c_str());
  }
}

void test_theme_catalog_references_existing_files() {
  const std::string json = readFile(fs::path("themes") / "index.json").c_str();
  size_t pos = 0;
  uint32_t fileCount = 0;
  while ((pos = json.find("\"file\"", pos)) != std::string::npos) {
    const size_t colon = json.find(':', pos);
    const size_t open = json.find('"', colon + 1);
    const size_t close = json.find('"', open + 1);
    TEST_ASSERT_NOT_EQUAL(std::string::npos, colon);
    TEST_ASSERT_NOT_EQUAL(std::string::npos, open);
    TEST_ASSERT_NOT_EQUAL(std::string::npos, close);
    const std::string filename = json.substr(open + 1, close - open - 1);
    TEST_ASSERT_TRUE_MESSAGE(filename.find('/') == std::string::npos && filename.find('\\') == std::string::npos,
                             filename.c_str());
    TEST_ASSERT_TRUE_MESSAGE(fs::exists(fs::path("themes") / filename), filename.c_str());
    ++fileCount;
    pos = close + 1;
  }
  TEST_ASSERT_GREATER_OR_EQUAL_UINT32(10, fileCount);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_parses_valid_theme);
  RUN_TEST(test_accepts_rgb565_literal);
  RUN_TEST(test_rejects_missing_magic);
  RUN_TEST(test_rejects_versioned_magic);
  RUN_TEST(test_rejects_missing_typeface);
  RUN_TEST(test_rejects_invalid_typeface);
  RUN_TEST(test_rejects_inherit_typeface);
  RUN_TEST(test_rejects_missing_role);
  RUN_TEST(test_rejects_bad_color);
  RUN_TEST(test_unknown_key_is_ignored);
  RUN_TEST(test_rejects_bad_low_brightness);
  RUN_TEST(test_theme_id_from_path);
  RUN_TEST(test_repo_themes_parse);
  RUN_TEST(test_theme_catalog_references_existing_files);
  return UNITY_END();
}
