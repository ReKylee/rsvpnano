#pragma once

namespace StorageFiles {

    void logError(const char* tag, const char* operation, const char* path, int error);
    void logError(const char* tag, const char* operation, const char* sourcePath, const char* targetPath, int error);

    bool directoryExists(const char* path);
    bool fileExists(const char* path);
    bool fileExistsWithBytes(const char* path);
    bool ensureDirectory(const char* path, const char* tag = "storage");

} // namespace StorageFiles
