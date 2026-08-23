#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace EpubPackage {

    struct ManifestItem {
        std::string id;
        std::string path;
        std::string mediaType;
        std::string properties;
    };

    struct TocEntry {
        std::string path;
        std::string title;
        std::string fragment;
    };

    std::string toLowerCopy(std::string_view value);
    std::string basenameWithoutExtension(std::string_view path);
    std::string normalizeZipName(std::string_view path);
    bool isArchiveHintEntry(std::string_view name);
    std::string directoryForPath(std::string_view path);
    bool isContentDocument(const ManifestItem& item);
    std::string parseRootfilePath(std::string_view containerXml);
    std::string parseDcMetadata(std::string_view opfXml, std::string_view tagName);
    std::string parsePackageVersion(std::string_view opfXml);
    std::vector<ManifestItem> parseManifestItems(std::string_view opfXml, std::string_view opfBaseDir);
    std::vector<std::string> parseSpineIds(std::string_view opfXml);
    std::vector<TocEntry> parseNcxTocEntries(std::string_view xml, std::string_view tocPath,
                                             std::string_view bookTitle);
    std::vector<TocEntry> parseNavTocEntries(std::string_view markup, std::string_view tocPath,
                                             std::string_view bookTitle);
    const ManifestItem* findManifestItem(const std::vector<ManifestItem>& items, std::string_view id);

} // namespace EpubPackage
