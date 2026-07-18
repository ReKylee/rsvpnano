#pragma once

#include <expected>
#include <string>
#include <string_view>
#include <system_error>

namespace fs {
    class FS;
}

namespace StorageFiles {

    void logError(const char* tag, const char* operation, const char* path, int error);
    void logError(const char* tag, const char* operation, const char* sourcePath, const char* targetPath, int error);
    void logError(const char* tag, const char* operation, const char* path, std::error_code error);
    void logError(const char* tag, const char* operation, const char* sourcePath, const char* targetPath,
                  std::error_code error);

    bool directoryExists(const char* path);
    bool fileExists(const char* path);
    bool fileExistsWithBytes(const char* path);
    std::expected<void, std::error_code> ensureDirectory(const char* path);
    std::expected<std::string, std::error_code> readTextFile(fs::FS& filesystem, const char* path, size_t maximum);
    std::expected<void, std::error_code> writeFileAtomic(fs::FS& filesystem, const char* path, const char* tempPath,
                                                        const char* backupPath, std::string_view content);
    std::expected<void, std::error_code> replaceFileAtomic(fs::FS& filesystem, const char* path,
                                                          const char* tempPath, const char* backupPath);

} // namespace StorageFiles
