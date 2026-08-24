#include "localization/LocaleCatalog.h"

#include <FS.h>
#include <SHA2Builder.h>
#include <esp_log.h>

#include <algorithm>
#include <array>
#include <functional>
#include <optional>
#include <ranges>

#include "conversion/epub/EpubZip.h"
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

        std::expected<File, std::string> openAsset(fs::FS& filesystem, std::string_view directory, const Asset& asset,
                                                   std::string_view name) {
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
            return file;
        }

        std::expected<void, std::string> inspectAsset(fs::FS& filesystem, std::string_view directory,
                                                      const Asset& asset, std::string_view name) {
            return openAsset(filesystem, directory, asset, name).transform([](File file) mutable {
                file.close();
            });
        }

        std::expected<void, std::string> verifyAssetHash(fs::FS& filesystem, std::string_view directory,
                                                         const Asset& asset, std::string_view name) {
            return openAsset(filesystem, directory, asset, name)
                .and_then([&asset, name](File file) mutable -> std::expected<void, std::string> {
                    SHA256Builder hash;
                    hash.begin();
                    std::array<uint8_t, 512> buffer;
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
                    if (std::string_view{digest.c_str()} != asset.sha256) {
                        return std::unexpected(std::string{name} + " failed SHA-256 validation");
                    }
                    return {};
                });
        }

        std::expected<void, std::string> inspectPackFiles(fs::FS& filesystem, std::string_view directory,
                                                          const Manifest& manifest) {
            return forEachAsset(manifest, [&](const Asset& asset, std::string_view name) {
                return inspectAsset(filesystem, directory, asset, name);
            });
        }

        std::expected<void, std::string> verifyPackFiles(fs::FS& filesystem, std::string_view directory,
                                                         const Manifest& manifest) {
            return forEachAsset(manifest, [&](const Asset& asset, std::string_view name) {
                return verifyAssetHash(filesystem, directory, asset, name);
            });
        }

        std::expected<Manifest, std::string> loadManifest(fs::FS& filesystem, const std::string& directory,
                                                          std::string_view id) {
            const std::string manifestPath = directory + "/manifest.toml";
            return StorageFiles::readTextFile(filesystem, manifestPath.c_str(), kMaximumManifestBytes)
                .transform_error([](std::error_code) {
                    return std::string{"manifest.toml is missing or unreadable"};
                })
                .and_then([&](const std::string& content) {
                    return decodeManifest(content, id);
                });
        }

        InstalledPack installedPack(std::string directory, Manifest manifest) {
            uint32_t scriptMask = 0;
            for (const std::string_view script: manifest.scripts)
                scriptMask |= UnicodeText::scriptMask(script);
            auto asset = [](std::optional<Asset> source) -> std::optional<InstalledAsset> {
                if (!source)
                    return std::nullopt;
                return InstalledAsset{.path = std::move(source->path), .bytes = source->bytes};
            };
            std::optional<InstalledUiComponent> ui;
            if (manifest.ui) {
                ui = InstalledUiComponent{
                    .strings = asset(std::move(manifest.ui->strings)),
                    .font = asset(std::move(manifest.ui->font)),
                };
            }
            return {
                .directory = std::move(directory),
                .id = std::move(manifest.id),
                .locale = std::move(manifest.locale),
                .nativeName = std::move(manifest.nativeName),
                .englishName = std::move(manifest.englishName),
                .direction = manifest.direction,
                .scriptMask = scriptMask,
                .ui = std::move(ui),
            };
        }

        template<typename AssetType>
        std::expected<std::vector<uint8_t>, std::string> readAsset(fs::FS& filesystem, std::string_view directory,
                                                                   const AssetType& asset) {
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

        std::expected<std::vector<uint8_t>, std::string> readUiFont(fs::FS& filesystem, const InstalledPack& pack) {
            if (!pack.ui || !pack.ui->font)
                return std::unexpected("locale pack has no UI font");
            return readAsset(filesystem, pack.directory, *pack.ui->font)
                .and_then([](std::vector<uint8_t> bytes) {
                    return validateU8g2Font(bytes).transform([bytes = std::move(bytes)]() mutable {
                        return std::move(bytes);
                    });
                });
        }

        std::expected<UiAssets, std::string> readUiAssets(fs::FS& filesystem, const InstalledPack& pack,
                                                          size_t expectedStrings) {
            UiAssets assets{.direction = pack.direction};
            const InstalledUiComponent& ui = *pack.ui;
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

    } // namespace

    Catalog scanInstalled(fs::FS& filesystem, size_t expectedStrings) {
        Catalog catalog;
        recoverInterrupted(filesystem);
        File root = filesystem.open(StoragePaths::kLocalesPath, FILE_READ);
        if (!root) {
            filesystem.mkdir(StoragePaths::kLocalesPath);
            return catalog;
        }
        if (!root.isDirectory()) {
            root.close();
            ESP_LOGW("languages", "rejected locales: locale-pack root is not a directory");
            return catalog;
        }

        for (File entry = root.openNextFile(); entry; entry = root.openNextFile()) {
            const std::string directory = entry.path();
            const std::string id{fileName(directory)};
            const bool usableDirectory = entry.isDirectory() && !id.empty() && !id.starts_with('.');
            entry.close();
            if (!usableDirectory)
                continue;

            auto manifest = loadManifest(filesystem, directory, id);
            if (!manifest) {
                ESP_LOGW("languages", "rejected %s: %s", id.c_str(), manifest.error().c_str());
                continue;
            }
            if (auto files = inspectPackFiles(filesystem, directory, *manifest); !files) {
                ESP_LOGW("languages", "rejected %s: %s", id.c_str(), files.error().c_str());
                continue;
            }
            InstalledPack pack = installedPack(directory, std::move(*manifest));
            if (auto assets = readUiAssets(filesystem, pack, expectedStrings); !assets) {
                ESP_LOGW("languages", "rejected %s: %s", id.c_str(), assets.error().c_str());
                continue;
            }
            if (std::ranges::any_of(catalog, [&](const InstalledPack& installed) {
                    return installed.id == pack.id;
                })) {
                ESP_LOGW("languages", "rejected %s: duplicate pack ID", id.c_str());
                continue;
            }
            if (std::ranges::any_of(catalog, [&](const InstalledPack& installed) {
                    return installed.locale == pack.locale;
                })) {
                ESP_LOGW("languages", "rejected %s: duplicate locale pack", id.c_str());
                continue;
            }
            catalog.push_back(std::move(pack));
        }
        root.close();
        std::ranges::sort(catalog, {}, [](const InstalledPack& pack) {
            return pack.englishName;
        });
        return catalog;
    }

    std::expected<std::vector<uint8_t>, std::string> loadUiFont(fs::FS& filesystem, const InstalledPack& pack) {
        if (!pack.ui || !pack.ui->font)
            return std::unexpected("locale pack has no UI font");
        return readAsset(filesystem, pack.directory, *pack.ui->font);
    }

    std::expected<UiAssets, std::string> loadUiAssets(fs::FS& filesystem, const Catalog& catalog,
                                                      std::string_view locale, size_t expectedStrings) {
        UiAssets assets;
        const auto selected = findPackForLocale(catalog, locale);
        if (!selected)
            return assets;
        return readUiAssets(filesystem, *selected, expectedStrings);
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

    std::expected<std::reference_wrapper<const InstalledPack>, std::string> activateStaged(fs::FS& filesystem,
                                                                                           Catalog& catalog,
                                                                                           std::string_view id,
                                                                                           size_t expectedStrings) {
        if (!isValidPackId(id))
            return std::unexpected("invalid pack ID");
        const std::string current = installedDirectory(id);
        const std::string staging = stagingDirectory(id);
        const std::string backup = backupDirectory(id);

        recoverInterrupted(filesystem);
        auto manifest = loadManifest(filesystem, staging, id);
        if (!manifest)
            return std::unexpected(manifest.error());
        if (auto files = verifyPackFiles(filesystem, staging, *manifest); !files)
            return std::unexpected(files.error());
        InstalledPack staged = installedPack(staging, std::move(*manifest));
        if (auto assets = readUiAssets(filesystem, staged, expectedStrings); !assets)
            return std::unexpected(assets.error());

        if (std::ranges::any_of(catalog, [&](const InstalledPack& pack) {
                return pack.id != id && pack.locale == staged.locale;
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
        staged.directory = current;
        const auto existing = std::ranges::find(catalog, id, [](const InstalledPack& pack) {
            return std::string_view{pack.id};
        });
        if (existing == catalog.end())
            catalog.push_back(std::move(staged));
        else
            *existing = std::move(staged);
        std::ranges::sort(catalog, {}, [](const InstalledPack& pack) {
            return pack.englishName;
        });
        return std::cref(*std::ranges::find(catalog, id, [](const InstalledPack& pack) {
            return std::string_view{pack.id};
        }));
    }

    std::expected<std::reference_wrapper<const InstalledPack>, std::string> installArchive(fs::FS& filesystem,
                                                                                           Catalog& catalog,
                                                                                           std::string_view
                                                                                               archivePath,
                                                                                           size_t expectedStrings) {
        constexpr size_t kMaximumFiles = 4;
        EpubZip::Archive archive;
        if (!archive.open(archivePath))
            return std::unexpected("Locale pack is not a supported ZIP archive");

        const auto entries = archive.entries();
        if (entries.empty() || entries.size() > kMaximumFiles)
            return std::unexpected("Locale pack has an invalid file count");

        std::string id;
        for (const auto& entry: entries) {
            const auto candidate = packIdFromArchiveManifest(entry.name);
            if (!candidate)
                continue;
            if (!id.empty())
                return std::unexpected("Locale pack must contain one valid manifest path");
            id = *candidate;
        }
        if (id.empty())
            return std::unexpected("Locale pack manifest is missing");

        const std::string root = "locales/" + id + "/";
        if (auto staged = beginStaging(filesystem, id); !staged)
            return std::unexpected(staged.error());

        for (size_t index = 0; index < entries.size(); ++index) {
            const auto& entry = entries[index];
            if (!entry.name.starts_with(root))
                return std::unexpected("Locale pack contains files outside its package folder");
            const std::string_view relative = std::string_view{entry.name}.substr(root.size());
            if (!isValidPackFilePath(relative))
                return std::unexpected("Locale pack contains an invalid file path");
            for (size_t previous = 0; previous < index; ++previous) {
                if (entries[previous].name == entry.name)
                    return std::unexpected("Locale pack contains duplicate files");
            }

            const size_t maximum = relative == "manifest.toml" ? kMaximumManifestBytes
                                 : relative == "ui/font.u8g2"  ? kMaximumUiFontBytes
                                                               : kMaximumResidentUiBytes;
            std::string contents;
            if (!archive.extractToString(entry.name, contents, maximum))
                return std::unexpected("Locale pack contains an invalid compressed file");
            auto target = prepareStagedFile(filesystem, id, relative);
            if (!target)
                return std::unexpected(target.error());
            File output = filesystem.open(target->c_str(), FILE_WRITE);
            if (!output
                || output.write(reinterpret_cast<const uint8_t*>(contents.data()), contents.size())
                       != contents.size()) {
                if (output)
                    output.close();
                return std::unexpected("Locale pack file could not be staged");
            }
            output.close();
        }

        archive.close();
        return activateStaged(filesystem, catalog, id, expectedStrings);
    }

    std::expected<void, std::string> removeInstalled(fs::FS& filesystem, Catalog& catalog, std::string_view id) {
        if (!isValidPackId(id))
            return std::unexpected("invalid pack ID");
        const std::string directory = installedDirectory(id);
        if (directoryExists(filesystem, directory) && !removeTree(filesystem, directory))
            return std::unexpected("could not remove the installed pack");
        std::erase_if(catalog, [id](const InstalledPack& pack) {
            return pack.id == id;
        });
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
        const auto pack = findPackForLocale(catalog, locale);
        return pack ? std::string_view{pack->englishName} : locale;
    }

} // namespace locales
