#include "settings/SettingsCodec.h"

#include <glaze/json.hpp>
#include <glaze/toml.hpp>

#include <utility>

#include "settings/SettingsGlaze.h"

namespace settings::codec {
    namespace {

        constexpr glz::opts kSettingsTomlReadOptions{
            .format = glz::TOML,
            .error_on_unknown_keys = false,
        };

        constexpr glz::opts kSettingsJsonReadOptions{
            .format = glz::JSON,
            .error_on_unknown_keys = false,
        };

        SettingsErrorCategory categoryFor(glz::error_code code) {
            if (code == glz::error_code::unexpected_enum)
                return SettingsErrorCategory::InvalidEnum;
            if (code == glz::error_code::constraint_violated)
                return SettingsErrorCategory::Constraint;
            return SettingsErrorCategory::Syntax;
        }

        SettingsError errorFrom(glz::error_ctx error, std::string_view input, SettingsSource source) {
            return {.category = categoryFor(error.ec),
                    .source = source,
                    .message = glz::format_error(error, input),
                    .glazeCode = error.ec};
        }

        SettingsError tooLarge(SettingsSource source, size_t maximum) {
            return {.category = SettingsErrorCategory::TooLarge,
                    .source = source,
                    .message = "input exceeds " + std::to_string(maximum) + " bytes"};
        }

        template<typename T, typename Reader>
        SettingsResult<T> decode(std::string_view input, SettingsSource source, size_t maximum, Reader&& reader) {
            if (input.size() > maximum)
                return std::unexpected(tooLarge(source, maximum));
            T candidate{};
            if (const glz::error_ctx error = reader(candidate, input))
                return std::unexpected(errorFrom(error, input, source));
            return candidate;
        }

        template<typename T, typename Writer>
        SettingsResult<std::string> encode(const T& value, SettingsSource source, Writer&& writer) {
            std::string output;
            if (const glz::error_ctx error = writer(value, output))
                return std::unexpected(errorFrom(error, output, source));
            return output;
        }

    } // namespace

    SettingsResult<DeviceSettings> decodeToml(std::string_view input, SettingsSource source) {
        return decode<DeviceSettings>(input, source, kMaxSettingsBytes, [](auto& value, std::string_view text) {
            return glz::read<kSettingsTomlReadOptions>(value, text);
        });
    }

    SettingsResult<std::string> encodeToml(const DeviceSettings& value, SettingsSource source) {
        return encode(value, source, [](auto& input, std::string& output) {
            return glz::write_toml(input, output);
        });
    }

    SettingsResult<DeviceSettings> decodeJson(std::string_view input, SettingsSource source) {
        return decode<DeviceSettings>(input, source, kMaxSettingsBytes, [](auto& value, std::string_view text) {
            return glz::read<kSettingsJsonReadOptions>(value, text);
        });
    }

    SettingsResult<std::string> encodeJson(const DeviceSettings& value, SettingsSource source) {
        return encode(value, source, [](auto& input, std::string& output) {
            return glz::write_json(input, output);
        });
    }

    SettingsResult<DeviceSecrets> decodeSecrets(std::string_view input, SettingsSource source) {
        return decode<DeviceSecrets>(input, source, kMaxSecretsBytes, [](auto& value, std::string_view text) {
            return glz::read<kSettingsTomlReadOptions>(value, text);
        });
    }

    SettingsResult<std::string> encodeSecrets(const DeviceSecrets& value, SettingsSource source) {
        return encode(value, source, [](auto& input, std::string& output) {
            return glz::write_toml(input, output);
        });
    }

} // namespace settings::codec
