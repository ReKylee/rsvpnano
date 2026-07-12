#include "timer/FocusTimers.h"

#include <charconv>
#include <limits>
#include <utility>

namespace focus {
    namespace {

        constexpr size_t kMaxFileBytes = 4096;

        std::string_view trim(std::string_view text) {
            while (!text.empty() && (text.front() == ' ' || text.front() == '\t' || text.front() == '\r'))
                text.remove_prefix(1);
            while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\r'))
                text.remove_suffix(1);
            return text;
        }

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

        bool parseQuoted(std::string_view text, std::string& value) {
            text = trim(text);
            if (text.size() < 2 || text.front() != '"' || text.back() != '"')
                return false;
            value.clear();
            value.reserve(text.size() - 2);
            for (size_t index = 1; index + 1 < text.size(); ++index) {
                const char character = text[index];
                if (character != '\\') {
                    value.push_back(character);
                    continue;
                }
                if (++index + 1 >= text.size() || (text[index] != '\\' && text[index] != '"'))
                    return false;
                value.push_back(text[index]);
            }
            return true;
        }

        template<typename T>
        bool parseInteger(std::string_view text, T& value) {
            text = trim(text);
            unsigned int parsed = 0;
            const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), parsed);
            if (text.empty() || error != std::errc{} || end != text.data() + text.size()
                || parsed > static_cast<unsigned int>(std::numeric_limits<T>::max()))
                return false;
            value = static_cast<T>(parsed);
            return true;
        }

        void appendQuoted(std::string& output, std::string_view value) {
            output.push_back('"');
            for (const char character: value) {
                if (character == '\\' || character == '"')
                    output.push_back('\\');
                output.push_back(character);
            }
            output.push_back('"');
        }

    } // namespace

    bool valid(const Timer& timer) {
        return !timer.name.empty() && timer.name.size() <= 32 && validUtf8(timer.name)
            && timer.focusMinutes >= 1 && timer.focusMinutes <= 180
            && timer.breakMinutes >= 1 && timer.breakMinutes <= 60 && timer.rounds >= 1 && timer.rounds <= 12;
    }

    bool parse(std::string_view content, Timers& timers) {
        if (content.empty() || content.size() > kMaxFileBytes)
            return false;

        Timers parsed;
        Timer current;
        uint8_t fields = 0;
        bool inTimer = false;
        bool versionSeen = false;

        const auto finishTimer = [&] {
            if (!inTimer)
                return true;
            if (fields != 0x0FU || parsed.count >= kMaxTimers || !valid(current))
                return false;
            parsed.items[parsed.count++] = std::move(current);
            current = {};
            fields = 0;
            return true;
        };

        size_t offset = 0;
        while (offset <= content.size()) {
            const size_t end = content.find('\n', offset);
            std::string_view line{content.data() + offset,
                                  (end == std::string_view::npos ? content.size() : end) - offset};
            line = trim(line);
            if (!line.empty() && line.front() != '#') {
                if (line == "[[timer]]") {
                    if (!versionSeen || !finishTimer())
                        return false;
                    inTimer = true;
                } else {
                    const size_t separator = line.find('=');
                    if (separator == std::string_view::npos)
                        return false;
                    const std::string_view key = trim(line.substr(0, separator));
                    const std::string_view value = trim(line.substr(separator + 1));
                    if (!inTimer) {
                        if (key != "version" || versionSeen || value != "1")
                            return false;
                        versionSeen = true;
                    } else if (key == "name") {
                        if ((fields & 0x01U) != 0 || !parseQuoted(value, current.name))
                            return false;
                        fields |= 0x01U;
                    } else if (key == "focus_minutes") {
                        if ((fields & 0x02U) != 0 || !parseInteger(value, current.focusMinutes))
                            return false;
                        fields |= 0x02U;
                    } else if (key == "break_minutes") {
                        if ((fields & 0x04U) != 0 || !parseInteger(value, current.breakMinutes))
                            return false;
                        fields |= 0x04U;
                    } else if (key == "rounds") {
                        if ((fields & 0x08U) != 0 || !parseInteger(value, current.rounds))
                            return false;
                        fields |= 0x08U;
                    } else {
                        return false;
                    }
                }
            }
            if (end == std::string_view::npos)
                break;
            offset = end + 1;
        }

        if (!versionSeen || !finishTimer() || parsed.count == 0)
            return false;
        timers = std::move(parsed);
        return true;
    }

    std::string serialize(const Timers& timers) {
        if (timers.count == 0 || timers.count > kMaxTimers)
            return {};
        std::string output{"# RSVP Nano focus timers\nversion=1\n"};
        output.reserve(128 * timers.count);
        for (size_t index = 0; index < timers.count; ++index) {
            const Timer& timer = timers.items[index];
            if (!valid(timer))
                return {};
            output += "\n[[timer]]\nname=";
            appendQuoted(output, timer.name);
            output += "\nfocus_minutes=" + std::to_string(timer.focusMinutes);
            output += "\nbreak_minutes=" + std::to_string(timer.breakMinutes);
            output += "\nrounds=" + std::to_string(timer.rounds) + "\n";
        }
        return output;
    }

} // namespace focus
