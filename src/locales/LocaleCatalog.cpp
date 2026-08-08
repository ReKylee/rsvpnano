#include "locales/LocaleCatalog.h"

#include <FS.h>
#include <SHA2Builder.h>

#include <algorithm>
#include <array>
#include <functional>
#include <optional>
#include <ranges>

#include "storage/fs/StorageFiles.h"
#include "storage/fs/StoragePaths.h"
#include "text/UnicodeText.h"

namespace locales {
    namespace {

        std::string_view fileName(std::string_view path) {
            const size_t separator = path.find_last_of('/');
            return separator == std::string_view::npos ? path : path.substr(separator + 1);
        }

        std::string assetPath(std::string_view directory, std::string_view relative) {
            std::string path{directory};
            path.push_back('/');
            path.append(relative);
            return path;
        }

        std::string installedDirectory(std::string_view id) {
            return std::string{StoragePaths::kLocalesPath} + "/" + std::string{id};
        }

        std::string stagingDirectory(std::string_view id) {
            return std::string{StoragePaths::kLocalesPath} + "/." + std::string{id} + ".installing";
        }

        std::string backupDirectory(std::string_view id) {
            return std::string{StoragePaths::kLocalesPath} + "/." + std::string{id} + ".backup";
        }

        bool directoryExists(fs::FS& filesystem, const std::string& path) {
            File directory = filesystem.open(path.c_str(), FILE_READ);
            const bool exists = directory && directory.isDirectory();
            if (directory)
                directory.close();
            return exists;
        }

        bool ensureDirectory(fs::FS& filesystem, const std::string& path) {
            if (directoryExists(filesystem, path))
                return true;
            File existing = filesystem.open(path.c_str(), FILE_READ);
            if (existing) {
                existing.close();
                return false;
            }
            return filesystem.mkdir(path.c_str());
        }

        bool removeTree(fs::FS& filesystem, const std::string& path) {
            File directory = filesystem.open(path.c_str(), FILE_READ);
            if (!directory || !directory.isDirectory()) {
                if (directory)
                    directory.close();
                return false;
            }
            for (File entry = directory.openNextFile(); entry; entry = directory.openNextFile()) {
                const std::string child = entry.path();
                const bool childDirectory = entry.isDirectory();
                entry.close();
                const bool removed = childDirectory ? removeTree(filesystem, child) : filesystem.remove(child.c_str());
                if (!removed) {
                    directory.close();
                    return false;
                }
            }
            directory.close();
            return filesystem.rmdir(path.c_str());
        }

        template<typename Function>
        std::expected<void, std::string> forEachAsset(const Manifest& manifest, Function&& function) {
            const auto visit = [&](const std::optional<Asset>& asset, std::string_view name) {
                return asset ? function(*asset, name) : std::expected<void, std::string>{};
            };
            if (manifest.ui) {
                if (auto result = visit(manifest.ui->strings, "ui.strings"); !result)
                    return result;
                if (auto result = visit(manifest.ui->font, "ui.font"); !result)
                    return result;
            }
            return {};
        }

        std::expected<void, std::string> inspectAsset(fs::FS& filesystem, std::string_view directory,
                                                      const Asset& asset, std::string_view name, bool verifyHash) {
            const std::string path = assetPath(directory, asset.path);
            File file = filesystem.open(path.c_str(), FILE_READ);
            if (!file || file.isDirectory()) {
                if (file)
                    file.close();
                return std::unexpected(std::string{name} + " is missing");
            }
            if (file.size() != asset.bytes) {
                file.close();
                return std::unexpected(std::string{name} + " has the wrong byte length");
            }
            if (!verifyHash) {
                file.close();
                return {};
            }

            SHA256Builder hash;
            hash.begin();
            std::array<uint8_t, 4096> buffer;
            uint32_t remaining = asset.bytes;
            while (remaining > 0) {
                const size_t requested = std::min<size_t>(buffer.size(), remaining);
                const size_t count = file.read(buffer.data(), requested);
                if (count != requested) {
                    file.close();
                    return std::unexpected(std::string{name} + " could not be read");
                }
                hash.add(buffer.data(), count);
                remaining -= count;
            }
            file.close();
            hash.calculate();
            const String digest = hash.toString();
            if (std::string_view{digest.c_str()} != asset.sha256)
                return std::unexpected(std::string{name} + " failed SHA-256 validation");
            return {};
        }

        std::expected<void, std::string> inspectPackFiles(fs::FS& filesystem, const InstalledPack& pack,
                                                          bool verifyHashes) {
            return forEachAsset(pack.manifest, [&](const Asset& asset, std::string_view name) {
                return inspectAsset(filesystem, pack.directory, asset, name, verifyHashes);
            });
        }

        std::expected<InstalledPack, std::string> loadPack(fs::FS& filesystem, const std::string& directory,
                                                           std::string_view id, bool verifyHashes) {
            const std::string manifestPath = directory + "/manifest.toml";
            auto content = StorageFiles::readTextFile(filesystem, manifestPath.c_str(), kMaximumManifestBytes);
            if (!content)
                return std::unexpected("manifest.toml is missing or unreadable");
            auto manifest = decodeManifest(*content, id);
            if (!manifest)
                return std::unexpected(manifest.error());
            uint32_t scriptMask = 0;
            for (const std::string_view script: manifest->scripts)
                scriptMask |= UnicodeText::scriptMask(script);
            InstalledPack pack{directory, std::move(*manifest), scriptMask};
            if (auto files = inspectPackFiles(filesystem, pack, verifyHashes); !files)
                return std::unexpected(files.error());
            return pack;
        }

        std::expected<std::vector<uint8_t>, std::string> readAsset(fs::FS& filesystem, std::string_view directory,
                                                                   const Asset& asset) {
            const std::string path = assetPath(directory, asset.path);
            File file = filesystem.open(path.c_str(), FILE_READ);
            if (!file || file.isDirectory() || file.size() != asset.bytes) {
                if (file)
                    file.close();
                return std::unexpected(asset.path + " is missing or has changed length");
            }
            std::vector<uint8_t> bytes(asset.bytes);
            const size_t read = file.read(bytes.data(), bytes.size());
            file.close();
            if (read != bytes.size())
                return std::unexpected(asset.path + " could not be read");
            return bytes;
        }

        std::expected<std::vector<uint8_t>, std::string> readUiFont(fs::FS& filesystem,
                                                                    const InstalledPack& pack) {
            if (!pack.manifest.ui || !pack.manifest.ui->font)
                return std::unexpected("locale pack has no UI font");
            auto bytes = readAsset(filesystem, pack.directory, *pack.manifest.ui->font);
            if (!bytes)
                return std::unexpected(bytes.error());
            if (auto valid = validateU8g2Font(*bytes); !valid)
                return std::unexpected(valid.error());
            return std::move(*bytes);
        }

    } // namespace

    Catalog scanInstalled(fs::FS& filesystem) {
        Catalog catalog;
        recoverInterrupted(filesystem);
        File root = filesystem.open(StoragePaths::kLocalesPath, FILE_READ);
        if (!root) {
            filesystem.mkdir(StoragePaths::kLocalesPath);
            return catalog;
        }
        if (!root.isDirectory()) {
            root.close();
            catalog.rejected.push_back({"locales", "locale-pack root is not a directory"});
            return catalog;
        }

        for (File entry = root.openNextFile(); entry; entry = root.openNextFile()) {
            const std::string directory = entry.path();
            const std::string id{fileName(directory)};
            const bool usableDirectory = entry.isDirectory() && !id.empty() && !id.starts_with('.');
            entry.close();
            if (!usableDirectory)
                continue;

            auto loaded = loadPack(filesystem, directory, id, false);
            if (!loaded) {
                catalog.rejected.push_back({id, loaded.error()});
                continue;
            }
            InstalledPack pack = std::move(*loaded);
            if (std::ranges::any_of(catalog.packs, [&](const InstalledPack& installed) {
                    return installed.manifest.id == pack.manifest.id;
                })) {
                catalog.rejected.push_back({id, "duplicate pack ID"});
                continue;
            }
            if (std::ranges::any_of(catalog.packs, [&](const InstalledPack& installed) {
                    return installed.manifest.locale == pack.manifest.locale;
                })) {
                catalog.rejected.push_back({id, "duplicate locale pack"});
                continue;
            }
            catalog.packs.push_back(std::move(pack));
        }
        root.close();
        std::ranges::sort(catalog.packs, {}, [](const InstalledPack& pack) {
            return pack.manifest.englishName;
        });
        return catalog;
    }

    std::expected<UiAssets, std::string> loadUiAssets(fs::FS& filesystem, const Catalog& catalog,
                                                       std::string_view locale, size_t expectedStrings) {
        UiAssets assets;
        const auto selected = detail::findPackForLocale(catalog, locale);
        if (!selected)
            return assets;
        const InstalledPack& pack = selected->get();
        assets.packId = pack.manifest.id;
        assets.locale = pack.manifest.locale;
        assets.direction = pack.manifest.direction;

        const UiComponent& ui = *pack.manifest.ui;
        if (ui.strings) {
            auto bytes = readAsset(filesystem, pack.directory, *ui.strings);
            if (!bytes)
                return std::unexpected(bytes.error());
            auto table = decodeStringTable(std::move(*bytes), expectedStrings);
            if (!table)
                return std::unexpected(table.error());
            assets.strings = std::move(*table);
        }
        if (ui.font) {
            auto font = readUiFont(filesystem, pack);
            if (!font)
                return std::unexpected(font.error());
            assets.font = std::move(*font);
        }
        if (assets.strings.bytes.size() + assets.font.size() > kMaximumResidentUiBytes)
            return std::unexpected("selected UI assets exceed the resident memory limit");
        return assets;
    }

    std::expected<void, std::string> beginStaging(fs::FS& filesystem, std::string_view id) {
        if (!isValidPackId(id))
            return std::unexpected("invalid pack ID");
        if (!ensureDirectory(filesystem, StoragePaths::kLocalesPath))
            return std::unexpected("locale-pack root is unavailable");
        const std::string staging = stagingDirectory(id);
        if (directoryExists(filesystem, staging) && !removeTree(filesystem, staging))
            return std::unexpected("could not reset the staging directory");
        if (!ensureDirectory(filesystem, staging))
            return std::unexpected("could not create the staging directory");
        return {};
    }

    std::expected<std::string, std::string> prepareStagedFile(fs::FS& filesystem, std::string_view id,
                                                               std::string_view relativePath) {
        if (!isValidPackId(id))
            return std::unexpected("invalid pack ID");
        if (!isValidPackFilePath(relativePath))
            return std::unexpected("invalid locale-pack file path");
        const std::string staging = stagingDirectory(id);
        if (!directoryExists(filesystem, staging))
            return std::unexpected("locale-pack staging has not been started");

        size_t separator = relativePath.find('/');
        while (separator != std::string_view::npos) {
            const std::string directory = staging + "/" + std::string{relativePath.substr(0, separator)};
            if (!ensureDirectory(filesystem, directory))
                return std::unexpected("could not create a component directory");
            separator = relativePath.find('/', separator + 1);
        }
        return staging + "/" + std::string{relativePath};
    }

    std::expected<void, std::string> activateStaged(fs::FS& filesystem, Catalog& catalog, std::string_view id) {
        if (!isValidPackId(id))
            return std::unexpected("invalid pack ID");
        const std::string current = installedDirectory(id);
        const std::string staging = stagingDirectory(id);
        const std::string backup = backupDirectory(id);

        recoverInterrupted(filesystem);
        auto staged = loadPack(filesystem, staging, id, true);
        if (!staged)
            return std::unexpected(staged.error());

        if (std::ranges::any_of(catalog.packs, [&](const InstalledPack& pack) {
                return pack.manifest.id != id && pack.manifest.locale == staged->manifest.locale;
            }))
            return std::unexpected("another pack already provides this locale");

        const bool hadCurrent = directoryExists(filesystem, current);
        if (hadCurrent && !filesystem.rename(current.c_str(), backup.c_str()))
            return std::unexpected("could not back up the installed pack");
        if (!filesystem.rename(staging.c_str(), current.c_str())) {
            if (hadCurrent)
                filesystem.rename(backup.c_str(), current.c_str());
            return std::unexpected("could not activate the staged pack");
        }

        if (hadCurrent)
            removeTree(filesystem, backup);
        staged->directory = current;
        const auto existing = std::ranges::find(catalog.packs, id, [](const InstalledPack& pack) {
            return std::string_view{pack.manifest.id};
        });
        if (existing == catalog.packs.end())
            catalog.packs.push_back(std::move(*staged));
        else
            *existing = std::move(*staged);
        std::erase_if(catalog.rejected, [id](const CatalogIssue& issue) { return issue.id == id; });
        std::ranges::sort(catalog.packs, {}, [](const InstalledPack& pack) {
            return pack.manifest.englishName;
        });
        return {};
    }

    std::expected<void, std::string> removeInstalled(fs::FS& filesystem, Catalog& catalog, std::string_view id) {
        if (!isValidPackId(id))
            return std::unexpected("invalid pack ID");
        const std::string directory = installedDirectory(id);
        if (directoryExists(filesystem, directory) && !removeTree(filesystem, directory))
            return std::unexpected("could not remove the installed pack");
        std::erase_if(catalog.packs, [id](const InstalledPack& pack) { return pack.manifest.id == id; });
        std::erase_if(catalog.rejected, [id](const CatalogIssue& issue) { return issue.id == id; });
        return {};
    }

    void recoverInterrupted(fs::FS& filesystem) {
        File root = filesystem.open(StoragePaths::kLocalesPath, FILE_READ);
        if (!root || !root.isDirectory()) {
            if (root)
                root.close();
            return;
        }
        for (File entry = root.openNextFile(); entry; entry = root.openNextFile()) {
            const std::string path = entry.path();
            const std::string name{fileName(path)};
            const bool directory = entry.isDirectory();
            entry.close();
            constexpr std::string_view suffix = ".backup";
            if (!directory || !name.starts_with('.') || !name.ends_with(suffix))
                continue;
            const std::string_view id{name.data() + 1, name.size() - 1 - suffix.size()};
            if (!isValidPackId(id))
                continue;
            const std::string current = installedDirectory(id);
            if (directoryExists(filesystem, current))
                removeTree(filesystem, path);
            else
                filesystem.rename(path.c_str(), current.c_str());
        }
        root.close();
    }

    std::string_view localeName(const Catalog& catalog, std::string_view locale) {
        if (locale == "en")
            return "English";
        const auto pack = detail::findPackForLocale(catalog, locale);
        return pack ? std::string_view{pack->get().manifest.englishName} : locale;
    }

} // namespace locales
