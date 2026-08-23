#include "storage/fs/StorageFiles.h"

#include <cerrno>
#include <string>
#include "board/BoardStorage.h"

namespace StorageFiles {
    namespace {
        std::error_code ioError() {
            return errno == 0 ? std::make_error_code(std::errc::io_error)
                              : std::error_code{errno, std::generic_category()};
        }

    } // namespace

    bool directoryExists(const char* path) {
        File dir = Board::Storage::filesystem().open(path);
        const bool exists = dir && dir.isDirectory();
        if (dir) {
            dir.close();
        }
        return exists;
    }

    bool fileExists(const char* path) {
        File file = Board::Storage::filesystem().open(path);
        const bool exists = file && !file.isDirectory();
        if (file) {
            file.close();
        }
        return exists;
    }

    bool fileExistsWithBytes(const char* path) {
        File file = Board::Storage::filesystem().open(path);
        const bool exists = file && !file.isDirectory() && file.size() > 0;
        if (file) {
            file.close();
        }
        return exists;
    }

    std::expected<void, std::error_code> ensureDirectory(const char* path) {
        if (directoryExists(path))
            return {};
        if (fileExists(path))
            return std::unexpected(std::make_error_code(std::errc::not_a_directory));
        errno = 0;
        if (!Board::Storage::filesystem().mkdir(path) && !directoryExists(path))
            return std::unexpected(ioError());
        return {};
    }

    std::expected<std::string, std::error_code> readTextFile(fs::FS& filesystem, const char* path, size_t maximum) {
        errno = 0;
        File file = filesystem.open(path, FILE_READ);
        if (!file)
            return std::unexpected(errno == 0 ? std::make_error_code(std::errc::no_such_file_or_directory) : ioError());
        const size_t size = file.size();
        if (file.isDirectory() || size == 0 || size > maximum) {
            const auto error =
                file.isDirectory() || size == 0 ? std::errc::invalid_argument : std::errc::value_too_large;
            file.close();
            return std::unexpected(std::make_error_code(error));
        }
        std::string content(size, '\0');
        const size_t read = file.read(reinterpret_cast<uint8_t*>(content.data()), content.size());
        file.close();
        if (read != content.size())
            return std::unexpected(std::make_error_code(std::errc::io_error));
        return content;
    }

    std::expected<void, std::error_code> writeFileAtomic(fs::FS& filesystem, const char* path, const char* tempPath,
                                                         const char* backupPath, std::string_view content) {
        filesystem.remove(tempPath);
        errno = 0;
        File file = filesystem.open(tempPath, FILE_WRITE);
        if (!file || file.isDirectory()) {
            if (file)
                file.close();
            return std::unexpected(ioError());
        }
        const size_t written = file.write(reinterpret_cast<const uint8_t*>(content.data()), content.size());
        file.flush();
        file.close();
        if (written != content.size()) {
            filesystem.remove(tempPath);
            return std::unexpected(std::make_error_code(std::errc::io_error));
        }

        return replaceFileAtomic(filesystem, path, tempPath, backupPath);
    }

    std::expected<void, std::error_code> replaceFileAtomic(fs::FS& filesystem, const char* path, const char* tempPath,
                                                           const char* backupPath) {
        filesystem.remove(backupPath);
        File original = filesystem.open(path, FILE_READ);
        const bool hadOriginal = original && !original.isDirectory();
        if (original)
            original.close();
        errno = 0;
        if (hadOriginal && !filesystem.rename(path, backupPath)) {
            filesystem.remove(tempPath);
            return std::unexpected(ioError());
        }
        errno = 0;
        if (!filesystem.rename(tempPath, path)) {
            const std::error_code error = ioError();
            if (hadOriginal)
                filesystem.rename(backupPath, path);
            filesystem.remove(tempPath);
            return std::unexpected(error);
        }
        filesystem.remove(backupPath);
        return {};
    }

} // namespace StorageFiles
