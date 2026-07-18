#include "settings/SettingsCodec.h"

#include <glaze/json.hpp>
#include <glaze/toml.hpp>

#include <utility>

#include "settings/SettingsGlaze.h"

namespace settings::codec {
    namespace {

        SettingsErrorCategory categoryFor(glz::error_code code) {
            if (code == glz::error_code::unknown_key)
                return SettingsErrorCategory::UnknownKey;
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

        SettingsResult<DeviceSettings> checkSchema(DeviceSettings value, SettingsSource source) {
            if (value.schemaVersion == kSettingsSchemaVersion)
                return value;
            return std::unexpected(SettingsError{.category = SettingsErrorCategory::UnsupportedSchema,
                                                 .source = source,
                                                 .path = "schemaVersion",
                                                 .message = "unsupported settings schema version "
                                                          + std::to_string(value.schemaVersion)});
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
                   return glz::read_toml(value, text);
               }).and_then([source](DeviceSettings value) { return checkSchema(std::move(value), source); });
    }

    SettingsResult<std::string> encodeToml(const DeviceSettings& value, SettingsSource source) {
        return encode(value, source, [](auto& input, std::string& output) { return glz::write_toml(input, output); });
    }

    SettingsResult<DeviceSettings> decodeJson(std::string_view input, SettingsSource source) {
        return decode<DeviceSettings>(input, source, kMaxSettingsBytes, [](auto& value, std::string_view text) {
                   return glz::read_json(value, text);
               }).and_then([source](DeviceSettings value) { return checkSchema(std::move(value), source); });
    }

    SettingsResult<std::string> encodeJson(const DeviceSettings& value, SettingsSource source) {
        return encode(value, source, [](auto& input, std::string& output) { return glz::write_json(input, output); });
    }

    SettingsResult<DeviceSecrets> decodeSecrets(std::string_view input, SettingsSource source) {
        return decode<DeviceSecrets>(input, source, kMaxSecretsBytes, [](auto& value, std::string_view text) {
            return glz::read_toml(value, text);
        });
    }

    SettingsResult<std::string> encodeSecrets(const DeviceSecrets& value, SettingsSource source) {
        return encode(value, source, [](auto& input, std::string& output) { return glz::write_toml(input, output); });
    }

} // namespace settings::codec
