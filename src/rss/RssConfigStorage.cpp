#include "rss/RssConfigStorage.h"

#include <FS.h>

#include "storage/fs/StorageFiles.h"
#include "storage/fs/StoragePaths.h"

namespace rss {
    std::expected<Config, std::error_code> load(fs::FS& filesystem) {
        return StorageFiles::readTextFile(filesystem, StoragePaths::kRssConfigPath, kMaxConfigBytes)
            .and_then([](const std::string& content) { return decodeToml(content); });
    }

    std::expected<void, std::error_code> save(fs::FS& filesystem, Config config) {
        auto content = encodeToml(std::move(config));
        if (!content)
            return std::unexpected(content.error());
        if (auto directory = StorageFiles::ensureDirectory(StoragePaths::kConfigPath); !directory)
            return directory;
        return StorageFiles::writeFileAtomic(filesystem, StoragePaths::kRssConfigPath,
                                             StoragePaths::kRssConfigTempPath, StoragePaths::kRssConfigBackupPath,
                                             *content);
    }

} // namespace rss
