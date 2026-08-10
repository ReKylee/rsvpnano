#include "benchmark/BenchmarkRunner.h"
#include <esp_log.h>

#include <Arduino.h>
#include <esp_heap_caps.h>
#include <algorithm>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include "board/BoardStorage.h"

#include "board/Board.h"
#include "board/BoardConfig.h"
#include "board/BoardInput.h"
#include "converter/EpubConverter.h"
#include "fonts/FontCatalog.h"
#include "input/Input.h"
#include "storage/fs/SdCard.h"
#include "storage/fs/StorageFiles.h"
#include "storage/fs/StoragePaths.h"
#include "text/BidiText.h"
#include "text/UnicodeText.h"
#include "ui/Theme.h"
#include "ui/Ui.h"
#include "ui/screens/Screens.h"

namespace {

    constexpr const char* kBenchmarkDir = "/benchmark";
    constexpr const char* kSdWritePath = "/benchmark/sd-write.bin";
    constexpr const char* kDraculaEpubPath = "/benchmark/Dracula-epub.epub";
    constexpr const char* kDraculaRsvpPath = "/benchmark/Dracula-epub.rsvp";
    constexpr size_t kSdProbeBytes = 256UL * 1024UL;
    constexpr size_t kSdChunkBytes = 4096;
    constexpr size_t kCpuIterations = 64;
    constexpr size_t kRenderIterations = 8;

    ui::Context gDisplay(Board::Display::gfx());
    ui::themes::Theme gTheme = ui::themes::defaultTheme();
    ui::fonts::AlphaTextRenderer<640> gText(Board::Display::gfx());
    FontCatalog gFonts;
    bool gDisplayReady = false;

    struct TextSample {
        std::string_view id;
        std::string_view locale;
        std::string_view paragraph;
        std::string_view word;
        bool rightToLeft = false;
    };

    constexpr TextSample kLatinSample{
        "latin", "en", "Comfortable reading should remain quick on every page.", "Comfortable", false};
    constexpr TextSample kHebrewSample{
        "hebrew", "he", "קריאה עברית נוחה וברורה עם נִקּוּד מלא.", "נִקּוּד", true};
    constexpr TextSample kArabicSample{
        "arabic", "ar", "القراءة العربية واضحة ومريحة مع التَّشْكِيلِ.", "التَّشْكِيلِ", true};
    constexpr TextSample kCjkSample{
        "cjk", "ja", "日本語と中文の文章を快適に読みます。", "日本語と中文", false};
    constexpr TextSample kMathSample{
        "math", "en", "∀x∈ℝ, x²≥0 and ∫₀¹x²dx=⅓.", "∀x∈ℝ", false};
    constexpr std::string_view kMixedParagraph =
        "English 123 — עברית עם נִקּוּד — العربية مع التَّشْكِيلِ — 日本語と中文 — ∀x∈ℝ.";

    void showStatus(const char* title, const char* line1 = "", const char* line2 = "") {
        ESP_LOGI("bench", "screen title=%s line1=%s line2=%s", title, line1, line2);
        if (gDisplayReady) {
            screens::status(gDisplay, title, line1, line2);
        }
    }

    ui::Rect renderArea() {
        const int16_t top = std::min<int16_t>(48, gDisplay.height() / 3);
        const int16_t bottom = std::min<int16_t>(32, gDisplay.height() / 4);
        return {8, top, static_cast<int16_t>(gDisplay.width() - 16),
                static_cast<int16_t>(gDisplay.height() - top - bottom)};
    }

    void showRenderScreen(std::string_view title, std::string_view detail) {
        const ui::Rect area = renderArea();
        gDisplay.invalidate();
        gDisplay.beginFrame(static_cast<uint8_t>(screens::Screen::Status));
        gDisplay.label({8, 4, static_cast<int16_t>(gDisplay.width() - 16), 20}, "Font benchmark", 2,
                       ui::themes::ColorRole::Accent, ui::TextAlign::Center);
        gDisplay.label({8, 26, static_cast<int16_t>(gDisplay.width() - 16), 18}, title, 2,
                       ui::themes::ColorRole::Foreground, ui::TextAlign::Center);
        const int16_t footerY = static_cast<int16_t>(area.y + area.h + 4);
        gDisplay.label({8, footerY, static_cast<int16_t>(gDisplay.width() - 16),
                        static_cast<int16_t>(gDisplay.height() - footerY)},
                       detail, 1, ui::themes::ColorRole::Muted, ui::TextAlign::Center);
        auto& gfx = Board::Display::gfx();
        gfx.drawFastHLine(area.x, area.y, area.w, gDisplay.color(ui::themes::ColorRole::Outline));
        gfx.drawFastHLine(area.x, static_cast<int16_t>(area.y + area.h - 1), area.w,
                          gDisplay.color(ui::themes::ColorRole::Outline));
        gDisplay.endFrame();
    }

    void clearRenderArea() {
        const ui::Rect area = renderArea();
        auto& gfx = Board::Display::gfx();
        gfx.fillRect(area.x, static_cast<int16_t>(area.y + 1), area.w, static_cast<int16_t>(area.h - 2),
                     gDisplay.color(ui::themes::ColorRole::Background));
        gfx.flush();
    }

    void logMetric(std::string_view name, bool ok, uint32_t elapsedUs, size_t iterations = 1, size_t bytes = 0,
                   uint32_t heapBefore = 0, uint32_t heapAfter = 0, uint32_t minimumHeap = 0) {
        const uint32_t elapsedMs = (elapsedUs + 999U) / 1000U;
        const uint32_t averageUs = iterations > 0 ? elapsedUs / iterations : 0;
        const uint32_t rateKiBPerSecond = elapsedUs > 0 && bytes > 0
                                            ? static_cast<uint32_t>((static_cast<uint64_t>(bytes) * 1000000ULL)
                                                                    / (static_cast<uint64_t>(elapsedUs) * 1024ULL))
                                            : 0;
        ESP_LOGI("bench",
                 "metric=%.*s ok=%u ms=%lu us=%lu iterations=%lu avg_us=%lu bytes=%lu rate_kib_s=%lu "
                 "heap_before=%lu heap_after=%lu heap_min=%lu",
                 static_cast<int>(name.size()), name.data(), ok ? 1 : 0, static_cast<unsigned long>(elapsedMs),
                 static_cast<unsigned long>(elapsedUs), static_cast<unsigned long>(iterations),
                 static_cast<unsigned long>(averageUs), static_cast<unsigned long>(bytes),
                 static_cast<unsigned long>(rateKiBPerSecond), static_cast<unsigned long>(heapBefore),
                 static_cast<unsigned long>(heapAfter), static_cast<unsigned long>(minimumHeap));
    }

    void fillBytes(uint8_t* buffer, size_t bytes, uint32_t offset) {
        for (size_t i = 0; i < bytes; ++i) {
            const uint32_t value = offset + static_cast<uint32_t>(i);
            buffer[i] = static_cast<uint8_t>((value * 33U) ^ (value >> 3) ^ 0xA5U);
        }
    }

    uint32_t checksumBytes(const uint8_t* buffer, size_t bytes) {
        uint32_t checksum = 2166136261UL;
        for (size_t i = 0; i < bytes; ++i) {
            checksum ^= buffer[i];
            checksum *= 16777619UL;
        }
        return checksum;
    }

    bool benchmarkDisplayPush() {
        auto& gfx = Board::Display::gfx();
        gfx.fillScreen(gDisplay.color(ui::themes::ColorRole::Background));
        gfx.flush();
        return true;
    }

    bool benchmarkSdWriteRead() {
        if (!StorageFiles::ensureDirectory(kBenchmarkDir)) {
            return false;
        }

        uint8_t* buffer = static_cast<uint8_t*>(malloc(kSdChunkBytes));
        if (buffer == nullptr) {
            return false;
        }

        uint32_t expectedChecksum = 2166136261UL;
        File file = Board::Storage::filesystem().open(kSdWritePath, FILE_WRITE);
        if (!file) {
            free(buffer);
            return false;
        }

        for (size_t offset = 0; offset < kSdProbeBytes; offset += kSdChunkBytes) {
            const size_t chunk = min(kSdChunkBytes, kSdProbeBytes - offset);
            fillBytes(buffer, chunk, static_cast<uint32_t>(offset));
            expectedChecksum = checksumBytes(buffer, chunk) ^ (expectedChecksum * 16777619UL);
            if (file.write(buffer, chunk) != chunk) {
                file.close();
                free(buffer);
                return false;
            }
        }
        file.flush();
        file.close();

        uint32_t actualChecksum = 2166136261UL;
        file = Board::Storage::filesystem().open(kSdWritePath, FILE_READ);
        if (!file) {
            free(buffer);
            return false;
        }

        for (size_t offset = 0; offset < kSdProbeBytes; offset += kSdChunkBytes) {
            const size_t chunk = min(kSdChunkBytes, kSdProbeBytes - offset);
            if (file.read(buffer, chunk) != static_cast<int>(chunk)) {
                file.close();
                free(buffer);
                return false;
            }
            actualChecksum = checksumBytes(buffer, chunk) ^ (actualChecksum * 16777619UL);
        }
        file.close();
        Board::Storage::filesystem().remove(kSdWritePath);
        free(buffer);
        return expectedChecksum == actualChecksum;
    }

    void reportEpubProgress(const EpubConverter::Options&, const char* line1, const char* line2, int progressPercent) {
        std::string percentLine = std::to_string(progressPercent) + "%";
        if (line2 != nullptr && line2[0] != '\0') {
            percentLine += " ";
            percentLine += line2;
        }
        showStatus("EPUB", line1 == nullptr ? "" : line1, percentLine.c_str());
    }

    bool benchmarkDraculaConversion() {
        if (!StorageFiles::fileExistsWithBytes(kDraculaEpubPath)) {
            ESP_LOGW("bench", "missing_epub path=%s", kDraculaEpubPath);
            showStatus("EPUB missing", "Copy Dracula-epub.epub", "to /benchmark on SD");
            return false;
        }

        Board::Storage::filesystem().remove(kDraculaRsvpPath);
        Board::Storage::filesystem()
            .remove(StoragePaths::siblingPathWithExtension(kDraculaEpubPath, StoragePaths::kTempExtension).c_str());
        Board::Storage::filesystem()
            .remove(StoragePaths::siblingPathWithExtension(kDraculaEpubPath, StoragePaths::kFailedExtension).c_str());

        EpubConverter::Options options;
        options.progressCallback = reportEpubProgress;
        options.progressTitle = "Benchmark";
        options.progressLabel = "Dracula";
        return EpubConverter::convertIfNeeded(kDraculaEpubPath, kDraculaRsvpPath, options).has_value();
    }

    template<typename Operation>
    bool runTimed(std::string_view name, size_t iterations, Operation&& operation, size_t bytes = 0,
                  bool updateDisplay = true) {
        const std::string ownedName{name};
        if (updateDisplay)
            showStatus("Benchmark", ownedName.c_str(), "Running");
        const uint32_t heapBefore = ESP.getFreeHeap();
        const bool monitorHeap = heap_caps_monitor_local_minimum_free_size_start() == ESP_OK;
        const uint32_t startedUs = micros();
        bool ok = true;
        size_t completed = 0;
        while (completed < iterations && ok) {
            ok = operation();
            ++completed;
        }
        const uint32_t elapsedUs = micros() - startedUs;
        const uint32_t heapAfter = ESP.getFreeHeap();
        const uint32_t minimumHeap = heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT);
        if (monitorHeap)
            heap_caps_monitor_local_minimum_free_size_stop();
        logMetric(name, ok, elapsedUs, completed, bytes, heapBefore, heapAfter, minimumHeap);
        if (updateDisplay) {
            const std::string elapsed = std::to_string(elapsedUs / 1000U) + " ms";
            showStatus(ok ? "Benchmark OK" : "Benchmark failed", ownedName.c_str(), elapsed.c_str());
            delay(250);
        }
        return ok;
    }

    template<typename Operation>
    bool runTimed(std::string_view name, Operation&& operation, size_t bytes = 0) {
        return runTimed(name, 1, std::forward<Operation>(operation), bytes);
    }

    const TextSample& sampleFor(const FontCatalog::Family& family) {
        if ((family.scriptMask & UnicodeText::ScriptArabic) != 0)
            return kArabicSample;
        if ((family.scriptMask & UnicodeText::ScriptHebrew) != 0)
            return kHebrewSample;
        if ((family.scriptMask & (UnicodeText::ScriptHan | UnicodeText::ScriptHiragana
                                  | UnicodeText::ScriptKatakana)) != 0)
            return kCjkSample;
        if ((family.scriptMask & UnicodeText::ScriptMath) != 0)
            return kMathSample;
        return kLatinSample;
    }

    bool shape(const FontCatalog::Face& face, const TextSample& sample,
               std::vector<ui::fonts::PositionedGlyph>& glyphs) {
        if (!face.shaper)
            return false;
        const size_t offset = sample.paragraph.find(sample.word);
        if (offset == std::string_view::npos)
            return false;
        glyphs.clear();
        return face.shaper->get()
            .shape(sample.paragraph, offset, sample.word.size(), sample.rightToLeft, sample.locale, gText, glyphs)
            .has_value();
    }

    bool renderParagraph(const TextSample& sample, const ui::Rect& area, size_t sizeIndex, int16_t& baseline,
                         size_t fixedFamily = SIZE_MAX) {
        const int16_t left = static_cast<int16_t>(area.x + 4);
        const int16_t right = static_cast<int16_t>(area.x + area.w - 4);
        int16_t cursor = sample.rightToLeft ? right : left;
        size_t offset = 0;
        std::vector<ui::fonts::PositionedGlyph> glyphs;
        glyphs.reserve(32);
        while (offset < sample.paragraph.size()) {
            while (offset < sample.paragraph.size() && sample.paragraph[offset] == ' ')
                ++offset;
            if (offset == sample.paragraph.size())
                break;
            const size_t found = sample.paragraph.find(' ', offset);
            const size_t end = found == std::string_view::npos ? sample.paragraph.size() : found;
            const std::string_view word = sample.paragraph.substr(offset, end - offset);
            const uint32_t scripts = UnicodeText::scriptsIn(word);
            const size_t selected = fixedFamily == SIZE_MAX
                                      ? FontCatalog::selectFamily(gFonts.families(), "literata", sample.locale, scripts)
                                      : fixedFamily;
            FontCatalog::Face face = gFonts.loadFace(selected, sizeIndex);
            gText.setFont(face.raster.get());

            int16_t advance = 0;
            bool shaped = false;
            if (face.shaper) {
                glyphs.clear();
                const auto result = face.shaper->get().shape(sample.paragraph, offset, word.size(),
                                                             sample.rightToLeft, sample.locale, gText, glyphs);
                if (!result)
                    return false;
                advance = *result;
                shaped = true;
            } else {
                advance = gText.textAdvance(word);
            }
            const int16_t space = std::max<int16_t>(2, gText.glyphAdvance(' '));
            const bool wrap = sample.rightToLeft ? cursor - advance < left : cursor + advance > right;
            if (wrap) {
                baseline = static_cast<int16_t>(baseline + face.raster.get().yAdvance + 2);
                cursor = sample.rightToLeft ? right : left;
            }
            if (baseline >= area.y + area.h - 4)
                return true;
            const int16_t x = sample.rightToLeft ? static_cast<int16_t>(cursor - advance) : cursor;
            const int16_t drawn = shaped ? gText.drawGlyphs(glyphs, x, baseline)
                                         : gText.drawString(word, x, baseline);
            if (drawn < 0)
                return false;
            cursor = sample.rightToLeft ? static_cast<int16_t>(x - space)
                                        : static_cast<int16_t>(x + drawn + space);
            offset = end;
        }
        baseline = static_cast<int16_t>(baseline + 16);
        return true;
    }

    bool benchmarkBidi(std::string_view id, const TextSample& sample) {
        BidiText::Analysis analysis;
        BidiText::Line line;
        const std::string metric = "bidi_" + std::string{id} + "_paragraph";
        return runTimed(metric, kCpuIterations, [&] {
            if (!analysis.reset(sample.paragraph, sample.rightToLeft ? TextDirection::rtl : TextDirection::ltr))
                return false;
            return analysis.resolve({0, sample.paragraph.size()}, line).has_value() && !line.empty();
        }, 0, false);
    }

    bool benchmarkFamily(size_t familyIndex) {
        const FontCatalog::Family& family = gFonts.families()[familyIndex];
        const TextSample& sample = sampleFor(family);
        const std::string prefix = "font_" + family.id;
        const ui::Rect area = renderArea();
        showRenderScreen(family.label, sample.id);

        FontCatalog::Face face = gFonts.loadFace(familyIndex, 1);
        gText.setFont(face.raster.get());
        gText.setTextColor(gDisplay.color(ui::themes::ColorRole::Foreground),
                           gDisplay.color(ui::themes::ColorRole::Background));
        bool ok = true;
        if (face.shaper) {
            std::vector<ui::fonts::PositionedGlyph> glyphs;
            glyphs.reserve(sample.word.size());
            ok &= runTimed(prefix + "_shape_cold", 1, [&] { return shape(face, sample, glyphs); }, 0, false);
            ok &= runTimed(prefix + "_shape_warm", kCpuIterations,
                           [&] { return shape(face, sample, glyphs); }, 0, false);
            if (!glyphs.empty()) {
                clearRenderArea();
                ok &= runTimed(prefix + "_render_rsvp", kRenderIterations, [&] {
                    return gText.drawGlyphs(glyphs, static_cast<int16_t>(area.x + 12),
                                            static_cast<int16_t>(area.y + area.h / 2)) >= 0;
                }, 0, false);
                Board::Display::gfx().flush();
            }
        } else {
            clearRenderArea();
            ok &= runTimed(prefix + "_render_rsvp", kRenderIterations, [&] {
                return gText.drawString(sample.word, static_cast<int16_t>(area.x + 12),
                                        static_cast<int16_t>(area.y + area.h / 2)) >= 0;
            }, 0, false);
            Board::Display::gfx().flush();
        }

        face = gFonts.loadFace(familyIndex, RFont4::kCompactStrikeIndex);
        gText.setFont(face.raster.get());
        clearRenderArea();
        ok &= runTimed(prefix + "_render_page", kRenderIterations, [&] {
            int16_t baseline = static_cast<int16_t>(area.y + 14);
            return renderParagraph(sample, area, RFont4::kCompactStrikeIndex, baseline, familyIndex);
        }, 0, false);
        Board::Display::gfx().flush();
        gFonts.clearLoaded();
        delay(1);
        return ok;
    }

    bool benchmarkMixedPage() {
        const ui::Rect area = renderArea();
        showRenderScreen("Multilingual page", "Latin / Hebrew / Arabic / CJK / Math");
        clearRenderArea();
        gFonts.clearLoaded();
        const bool ok = runTimed("multilingual_page_pipeline", kRenderIterations, [&] {
            int16_t baseline = static_cast<int16_t>(area.y + 12);
            return renderParagraph(kLatinSample, area, RFont4::kCompactStrikeIndex, baseline)
                && renderParagraph(kHebrewSample, area, RFont4::kCompactStrikeIndex, baseline)
                && renderParagraph(kArabicSample, area, RFont4::kCompactStrikeIndex, baseline)
                && renderParagraph(kCjkSample, area, RFont4::kCompactStrikeIndex, baseline)
                && renderParagraph(kMathSample, area, RFont4::kCompactStrikeIndex, baseline);
        }, 0, false);
        Board::Display::gfx().flush();
        gFonts.clearLoaded();
        return ok;
    }

    bool benchmarkFonts() {
        if (!gText.begin())
            return false;
        const bool catalogOk = runTimed("font_catalog_load", [] {
            gFonts.loadFromSd();
            return !gFonts.families().empty();
        });
        if (!catalogOk)
            return false;

        ESP_LOGI("bench", "font_catalog families=%u", static_cast<unsigned>(gFonts.families().size()));
        bool ok = benchmarkBidi("hebrew", kHebrewSample) && benchmarkBidi("arabic", kArabicSample);
        const TextSample mixedBidi{"mixed", "en", kMixedParagraph, "עברית", false};
        ok = benchmarkBidi("mixed", mixedBidi) && ok;
        for (size_t index = 0; index < gFonts.families().size(); ++index)
            ok = benchmarkFamily(index) && ok;
        return benchmarkMixedPage() && ok;
    }

    bool beginDisplay() {
        gDisplayReady = Board::Display::begin();
        if (gDisplayReady)
            gDisplay.setOrientation(Board::Display::defaultUiOrientation());
        gDisplay.setTheme(gTheme);
        return gDisplayReady;
    }
    bool beginInput() {
        const bool started = Input::begin();
        gDisplay.setTouchSource({.surface = Board::Input::touchSurface(), .poll = &Input::pollTouch});
        return started;
    }
    bool beginAudio() {
        return Board::Audio::begin();
    }
    bool beepAudio() {
        return Board::Audio::beep();
    }

    bool startButtonHeld() {
        const Input::PressActions actions = Board::Input::currentActions();
        return actions.shortPress != Input::ActionNone || actions.longPress != Input::ActionNone;
    }

    void waitForStartInput() {
        showStatus("Benchmark", "Tap or press button", "SD data stays in /benchmark");
        ESP_LOGW("bench", "waiting_for_start_input");

        const uint32_t settleStartMs = millis();
        while (millis() - settleStartMs < 500) {
            delay(10);
        }

        bool inputWasHeld = startButtonHeld();
        uint32_t lastReminderMs = millis();
        while (true) {
            Input::Event event;
            if (gDisplay.pollTouch(millis())) {
                break;
            }
            Input::poll(event);

            const bool held = startButtonHeld();
            if (!inputWasHeld && held) {
                break;
            }
            inputWasHeld = held;

            if (millis() - lastReminderMs > 3000) {
                ESP_LOGW("bench", "still_waiting_for_start_input");
                lastReminderMs = millis();
            }
            delay(20);
        }

        ESP_LOGI("bench", "start_input_received");
        showStatus("Benchmark", "Starting", "");
        delay(300);
    }

} // namespace

namespace Benchmark {

    void run() {
        ESP_LOGI("bench", "start board=%s id=%s", Board::Config::BOARD_LABEL, Board::Config::BOARD_ID);

        bool mounted = false;
        int mountedFrequencyKhz = 0;
        runTimed("display_begin", beginDisplay);
        runTimed("input_begin", beginInput);
        waitForStartInput();
        runTimed("display_push_full", benchmarkDisplayPush,
                 static_cast<size_t>(gDisplay.width()) * static_cast<size_t>(gDisplay.height()) * sizeof(uint16_t));
        if (Board::Audio::available()) {
            runTimed("audio_begin", beginAudio);
            runTimed("audio_beep", beepAudio);
        } else {
            logMetric("audio_begin", false, 0);
            logMetric("audio_beep", false, 0);
        }

        const uint32_t mountStartedUs = micros();
        mounted = SdCard::mount(mounted, &mountedFrequencyKhz);
        logMetric("sd_mount", mounted, micros() - mountStartedUs);
        ESP_LOGI("bench", "sd_frequency_khz=%d", mountedFrequencyKhz);
        if (mounted) {
            runTimed("sd_write_read", benchmarkSdWriteRead, kSdProbeBytes * 2);
            benchmarkFonts();
            runTimed("epub_dracula_convert", benchmarkDraculaConversion);
        } else {
            logMetric("sd_write_read", false, 0, kSdProbeBytes * 2);
            logMetric("epub_dracula_convert", false, 0);
        }

        ESP_LOGI("bench", "done");
        showStatus("Benchmark", "Done", "Check serial log");
    }

} // namespace Benchmark
