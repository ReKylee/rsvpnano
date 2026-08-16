#pragma once

#include <FS.h>
#include <esp_http_server.h>

#include <cstddef>
#include <string>
#include <string_view>

#include "companion/CompanionHttp.h"

namespace companion {

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
                                                                  std::string_view label);

    private:
        TemporaryUpload(fs::FS& filesystem, std::string temporaryPath);
        void cleanup() noexcept;

        fs::FS* filesystem_ = nullptr;
        std::string path_;
    };

} // namespace companion
