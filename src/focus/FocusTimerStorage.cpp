#include "focus/FocusTimerStorage.h"

#include <Arduino.h>
#include <FS.h>

#include <string>

#include "storage/fs/StorageFiles.h"
#include "storage/fs/StoragePaths.h"

namespace focus {
    std::expected<Timers, std::error_code> load(fs::FS& filesystem) {
        return StorageFiles::readTextFile(filesystem, StoragePaths::kFocusConfigPath, kMaxConfigBytes)
            .and_then([](const std::string& content) {
                return decodeToml(content);
            });
    }

    std::expected<void, std::error_code> save(fs::FS& filesystem, const Timers& timers) {
        return encodeToml(timers).and_then([&](const std::string& content) {
            return StorageFiles::ensureDirectory(StoragePaths::kConfigPath).and_then([&] {
                return StorageFiles::writeFileAtomic(filesystem, StoragePaths::kFocusConfigPath,
                                                     StoragePaths::kFocusConfigTempPath,
                                                     StoragePaths::kFocusConfigBackupPath, content);
            });
        });
    }

} // namespace focus
