#include "timer/FocusTimers.h"

#include <glaze/toml.hpp>

#include <algorithm>
#include <utility>

#include "settings/SettingsGlaze.h"

namespace focus {
    namespace {

        constexpr size_t kMaxFileBytes = 4096;

        bool validUtf8(std::string_view text) {
            for (size_t index = 0; index < text.size();) {
                const uint8_t first = static_cast<uint8_t>(text[index]);
                if (first < 0x20 || first == 0x7F)
                    return false;
                if (first < 0x80) {
                    ++index;
                    continue;
                }

                size_t continuation = 0;
                uint32_t codepoint = 0;
                if ((first & 0xE0U) == 0xC0U) {
                    continuation = 1;
                    codepoint = first & 0x1FU;
                } else if ((first & 0xF0U) == 0xE0U) {
                    continuation = 2;
                    codepoint = first & 0x0FU;
                } else if ((first & 0xF8U) == 0xF0U) {
                    continuation = 3;
                    codepoint = first & 0x07U;
                } else {
                    return false;
                }
                if (index + continuation >= text.size())
                    return false;
                for (size_t offset = 1; offset <= continuation; ++offset) {
                    const uint8_t next = static_cast<uint8_t>(text[index + offset]);
                    if ((next & 0xC0U) != 0x80U)
                        return false;
                    codepoint = (codepoint << 6) | (next & 0x3FU);
                }
                const uint32_t minimum = continuation == 1 ? 0x80U : continuation == 2 ? 0x800U : 0x10000U;
                if (codepoint < minimum || codepoint > 0x10FFFFU || (codepoint >= 0x80U && codepoint <= 0x9FU)
                    || (codepoint >= 0xD800U && codepoint <= 0xDFFFU))
                    return false;
                index += continuation + 1;
            }
            return true;
        }

    } // namespace

    Timer defaultTimer() {
        Timer timer;
        timer.name = "Pomodoro";
        return timer;
    }

    Timers defaultTimers() {
        Timers timers;
        timers.timers.push_back(defaultTimer());
        return timers;
    }

    bool valid(const Timer& timer) {
        return !timer.name.empty() && timer.name.size() <= kMaxTimerNameBytes && validUtf8(timer.name);
    }

    bool valid(const Timers& timers) {
        return timers.schemaVersion == kSchemaVersion && !timers.timers.empty() && timers.timers.size() <= kMaxTimers
            && std::ranges::all_of(timers.timers, [](const Timer& timer) { return valid(timer); });
    }

    std::expected<Timers, std::error_code> decodeToml(std::string_view content) {
        if (content.empty() || content.size() > kMaxFileBytes)
            return std::unexpected(std::make_error_code(content.empty() ? std::errc::invalid_argument
                                                                        : std::errc::value_too_large));

        Timers parsed;
        if (glz::read_toml(parsed, content) || parsed.schemaVersion != kSchemaVersion)
            return std::unexpected(std::make_error_code(std::errc::invalid_argument));
        if (parsed.timers.empty())
            parsed.timers.push_back(defaultTimer());
        if (parsed.timers.size() > kMaxTimers)
            parsed.timers.resize(kMaxTimers);
        if (!valid(parsed))
            return std::unexpected(std::make_error_code(std::errc::invalid_argument));
        return parsed;
    }

    std::expected<std::string, std::error_code> encodeToml(const Timers& timers) {
        if (!valid(timers))
            return std::unexpected(std::make_error_code(std::errc::invalid_argument));
        std::string output;
        if (glz::write_toml(timers, output))
            return std::unexpected(std::make_error_code(std::errc::io_error));
        return output;
    }

} // namespace focus
