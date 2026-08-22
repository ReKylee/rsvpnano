#pragma once

#include <FS.h>
#include <esp_http_server.h>

#include <cstddef>
#include <expected>
#include <span>
#include <string>
#include <string_view>
#include <system_error>

#include "companion/CompanionHttp.h"

namespace companion {

    using UploadChunkConsumer = std::expected<void, std::error_code> (*)(void* context,
                                                                         std::span<const uint8_t> bytes);

    class TemporaryUpload {
    public:
        TemporaryUpload(const TemporaryUpload&) = delete;
        TemporaryUpload& operator=(const TemporaryUpload&) = delete;

        TemporaryUpload(TemporaryUpload&& other) noexcept;
        TemporaryUpload& operator=(TemporaryUpload&& other) noexcept;
        ~TemporaryUpload();

        [[nodiscard]] const std::string& path() const noexcept;

        [[nodiscard]] static api::Result<TemporaryUpload> receive(httpd_req_t& request, fs::FS& filesystem,
                                                                  std::string temporaryPath,
                                                                  size_t maximumBytes,
                                                                  std::string_view label,
                                                                  UploadChunkConsumer consume = nullptr,
                                                                  void* consumeContext = nullptr);

    private:
        TemporaryUpload(fs::FS& filesystem, std::string temporaryPath);
        void cleanup() noexcept;

        fs::FS* filesystem_ = nullptr;
        std::string path_;
    };

} // namespace companion
