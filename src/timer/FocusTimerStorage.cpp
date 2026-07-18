#include "timer/FocusTimerStorage.h"

#include <Arduino.h>
#include <FS.h>

#include <string>

#include "storage/fs/StorageFiles.h"
#include "storage/fs/StoragePaths.h"

namespace focus {
    namespace {

        constexpr size_t kMaxFileBytes = 4096;

    } // namespace

    std::expected<Timers, std::error_code> load(fs::FS& filesystem) {
        return StorageFiles::readTextFile(filesystem, StoragePaths::kFocusConfigPath, kMaxFileBytes)
            .and_then([](const std::string& content) { return decodeToml(content); });
    }

    std::expected<void, std::error_code> save(fs::FS& filesystem, const Timers& timers) {
        auto content = encodeToml(timers);
        if (!content)
            return std::unexpected(content.error());
        if (auto directory = StorageFiles::ensureDirectory(StoragePaths::kConfigPath); !directory)
            return directory;
        return StorageFiles::writeFileAtomic(filesystem, StoragePaths::kFocusConfigPath,
                                             StoragePaths::kFocusConfigTempPath, StoragePaths::kFocusConfigBackupPath,
                                             *content);
    }

} // namespace focus
