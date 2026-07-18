#include "rss/RssConfigStorage.h"

#include <FS.h>

#include <cerrno>
#include <string>

#include "storage/fs/StorageFiles.h"
#include "storage/fs/StoragePaths.h"

namespace rss {
    std::expected<Config, std::error_code> load(fs::FS& filesystem) {
        File file = filesystem.open(StoragePaths::kRssConfigPath, FILE_READ);
        if (!file)
            return std::unexpected(std::make_error_code(std::errc::no_such_file_or_directory));
        if (file.isDirectory() || file.size() == 0 || file.size() > kMaxConfigBytes) {
            file.close();
            return std::unexpected(std::make_error_code(std::errc::invalid_argument));
        }

        std::string content(file.size(), '\0');
        const size_t bytesRead = file.read(reinterpret_cast<uint8_t*>(content.data()), content.size());
        file.close();
        if (bytesRead != content.size())
            return std::unexpected(std::make_error_code(std::errc::io_error));
        return decodeToml(content);
    }

    std::expected<void, std::error_code> save(fs::FS& filesystem, Config config) {
        auto content = encodeToml(std::move(config));
        if (!content)
            return std::unexpected(content.error());
        if (!StorageFiles::ensureDirectory(StoragePaths::kConfigPath, "rss"))
            return std::unexpected(std::make_error_code(std::errc::io_error));

        filesystem.remove(StoragePaths::kRssConfigTempPath);
        File file = filesystem.open(StoragePaths::kRssConfigTempPath, FILE_WRITE);
        if (!file || file.isDirectory()) {
            if (file)
                file.close();
            return std::unexpected(std::make_error_code(std::errc::io_error));
        }
        const size_t bytesWritten =
            file.write(reinterpret_cast<const uint8_t*>(content->data()), content->size());
        file.flush();
        file.close();
        if (bytesWritten != content->size()) {
            filesystem.remove(StoragePaths::kRssConfigTempPath);
            return std::unexpected(std::make_error_code(std::errc::io_error));
        }

        filesystem.remove(StoragePaths::kRssConfigBackupPath);
        File existing = filesystem.open(StoragePaths::kRssConfigPath, FILE_READ);
        const bool hadExisting = existing && !existing.isDirectory();
        if (existing)
            existing.close();
        if (hadExisting
            && !filesystem.rename(StoragePaths::kRssConfigPath, StoragePaths::kRssConfigBackupPath)) {
            filesystem.remove(StoragePaths::kRssConfigTempPath);
            return std::unexpected(std::make_error_code(std::errc::io_error));
        }
        if (!filesystem.rename(StoragePaths::kRssConfigTempPath, StoragePaths::kRssConfigPath)) {
            if (hadExisting)
                filesystem.rename(StoragePaths::kRssConfigBackupPath, StoragePaths::kRssConfigPath);
            filesystem.remove(StoragePaths::kRssConfigTempPath);
            return std::unexpected(std::make_error_code(std::errc::io_error));
        }
        filesystem.remove(StoragePaths::kRssConfigBackupPath);
        return {};
    }

} // namespace rss
