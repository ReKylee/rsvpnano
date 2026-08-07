#include "storage/fs/StoragePaths.h"

#include <algorithm>

#include "text/AsciiText.h"

namespace StoragePaths {
    namespace {

        bool hasExtension(std::string_view path, std::string_view extension) {
            return path.size() >= extension.size()
                && std::ranges::equal(path.substr(path.size() - extension.size()), extension, {}, AsciiText::toLower,
                                      AsciiText::toLower);
        }

    } // namespace

    bool hasTextExtension(std::string_view path) {
        return hasExtension(path, kTextExtension);
    }

    bool hasRsvpExtension(std::string_view path) {
        return hasExtension(path, kRsvpExtension);
    }

    bool hasEpubExtension(std::string_view path) {
        return hasExtension(path, kEpubExtension);
    }

    std::string parentDirectoryForPath(std::string_view path) {
        const size_t separator = path.find_last_of('/');
        if (separator == std::string_view::npos || separator == 0) {
            return "/";
        }
        return std::string{path.substr(0, separator)};
    }

    std::string siblingPathWithExtension(std::string_view path, std::string_view extension) {
        const size_t dot = path.find_last_of('.');
        if (dot != std::string_view::npos && dot > 0) {
            path = path.substr(0, dot);
        }
        std::string siblingPath{path};
        siblingPath += extension;
        return siblingPath;
    }

    std::string epubSiblingPathForRsvp(std::string_view rsvpPath) {
        return siblingPathWithExtension(rsvpPath, kEpubExtension);
    }

    std::string displayNameForPath(std::string_view path) {
        const size_t separator = path.find_last_of('/');
        return std::string{separator == std::string_view::npos ? path : path.substr(separator + 1)};
    }

    std::string displayNameWithoutExtension(std::string_view path) {
        std::string name = displayNameForPath(path);
        for (const std::string_view extension:
             {std::string_view{kTextExtension}, std::string_view{kRsvpExtension}, std::string_view{kEpubExtension}}) {
            if (hasExtension(name, extension)) {
                name.resize(name.size() - extension.size());
                break;
            }
        }
        return name;
    }

    std::string rsvpCachePathForEpub(std::string_view epubPath) {
        return siblingPathWithExtension(epubPath, kRsvpExtension);
    }

    std::string indexedIndexPathFor(std::string_view path) {
        return std::string{path} + kIndexExtension;
    }

    std::string indexedDataPathFor(std::string_view path) {
        return std::string{path} + kDataExtension;
    }

    std::string bookStatePathFor(std::string_view path) {
        return siblingPathWithExtension(path, kBookStateExtension);
    }

    std::string indexedTempPathFor(std::string_view path) {
        return std::string{path} + kTempExtension;
    }

    bool isHiddenOrSidecarPath(std::string_view path) {
        const std::string name = displayNameForPath(path);
        if (name.empty()) {
            return true;
        }

        if (name.starts_with('.')) {
            return true;
        }

        if (hasExtension(name, kIndexExtension) || hasExtension(name, kDataExtension)
            || hasExtension(name, kBookStateExtension) || hasExtension(name, kTempExtension)) {
            return true;
        }

        return std::ranges::equal(name, "thumbs.db", {}, AsciiText::toLower, AsciiText::toLower)
            || std::ranges::equal(name, "desktop.ini", {}, AsciiText::toLower, AsciiText::toLower);
    }

} // namespace StoragePaths
