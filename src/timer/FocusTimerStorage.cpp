#include "timer/FocusTimerStorage.h"

#include <Arduino.h>
#include <FS.h>

#include <string>

#include "storage/fs/StoragePaths.h"

namespace focus {
    namespace {

        constexpr size_t kMaxFileBytes = 4096;

        bool ensureConfigDirectory(fs::FS& filesystem) {
            File directory = filesystem.open(StoragePaths::kConfigPath);
            const bool exists = directory && directory.isDirectory();
            if (directory)
                directory.close();
            return exists || filesystem.mkdir(StoragePaths::kConfigPath);
        }

    } // namespace

    LoadResult load(fs::FS& filesystem, Timers& timers) {
        File file = filesystem.open(StoragePaths::kFocusConfigPath, FILE_READ);
        if (!file)
            return LoadResult::Missing;
        if (file.isDirectory() || file.size() == 0 || file.size() > kMaxFileBytes) {
            file.close();
            return LoadResult::Invalid;
        }

        std::string content(file.size(), '\0');
        const size_t read = file.read(reinterpret_cast<uint8_t*>(content.data()), content.size());
        file.close();
        return read == content.size() && decodeToml(content, timers) ? LoadResult::Valid : LoadResult::Invalid;
    }

    bool save(fs::FS& filesystem, const Timers& timers) {
        const std::string content = encodeToml(timers);
        if (content.empty() || !ensureConfigDirectory(filesystem))
            return false;

        filesystem.remove(StoragePaths::kFocusConfigTempPath);
        File file = filesystem.open(StoragePaths::kFocusConfigTempPath, FILE_WRITE);
        if (!file || file.isDirectory()) {
            if (file)
                file.close();
            return false;
        }
        const size_t written = file.write(reinterpret_cast<const uint8_t*>(content.data()), content.size());
        file.flush();
        file.close();
        if (written != content.size()) {
            filesystem.remove(StoragePaths::kFocusConfigTempPath);
            return false;
        }

        filesystem.remove(StoragePaths::kFocusConfigBackupPath);
        File existing = filesystem.open(StoragePaths::kFocusConfigPath, FILE_READ);
        const bool hadExisting = existing && !existing.isDirectory();
        if (existing)
            existing.close();
        if (hadExisting
            && !filesystem.rename(StoragePaths::kFocusConfigPath, StoragePaths::kFocusConfigBackupPath)) {
            filesystem.remove(StoragePaths::kFocusConfigTempPath);
            return false;
        }
        if (!filesystem.rename(StoragePaths::kFocusConfigTempPath, StoragePaths::kFocusConfigPath)) {
            if (hadExisting)
                filesystem.rename(StoragePaths::kFocusConfigBackupPath, StoragePaths::kFocusConfigPath);
            filesystem.remove(StoragePaths::kFocusConfigTempPath);
            return false;
        }
        filesystem.remove(StoragePaths::kFocusConfigBackupPath);
        return true;
    }

} // namespace focus
