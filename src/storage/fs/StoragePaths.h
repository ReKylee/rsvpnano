#pragma once

#include <Arduino.h>

namespace StoragePaths {

    constexpr const char* kMountPoint = "/sdcard";
    constexpr const char* kBooksPath = "/books";
    constexpr const char* kBookFilesPath = "/books/books";
    constexpr const char* kArticleFilesPath = "/books/articles";
    constexpr const char* kArticleFilesPrefix = "/books/articles/";
    constexpr const char* kConfigPath = "/config";
    constexpr const char* kSettingsConfigPath = "/config/settings.conf";
    constexpr const char* kSettingsConfigTempPath = "/config/settings.conf.tmp";
    constexpr const char* kSettingsConfigBackupPath = "/config/settings.conf.bak";
    constexpr const char* kRssConfigPath = "/config/rss.conf";
    constexpr const char* kRssConfigTempPath = "/config/rss.conf.tmp";
    constexpr const char* kFocusConfigPath = "/config/focus.conf";
    constexpr const char* kFocusConfigTempPath = "/config/focus.conf.tmp";
    constexpr const char* kFocusConfigBackupPath = "/config/focus.conf.bak";
    constexpr const char* kSdFrequencyProbePath = "/.sdfreq.tmp";
    constexpr const char* kThemesPath = "/themes";
    constexpr const char* kFontsPath = "/fonts";
    constexpr const char* kTextExtension = ".txt";
    constexpr const char* kRsvpExtension = ".rsvp";
    constexpr const char* kEpubExtension = ".epub";
    constexpr const char* kIndexExtension = ".ridx";
    constexpr const char* kDataExtension = ".rdat";
    constexpr const char* kProgressExtension = ".rpos";
    constexpr const char* kFontExtension = ".rfont4";
    constexpr const char* kTempExtension = ".tmp";
    constexpr const char* kFailedExtension = ".failed";
    constexpr const char* kConvertingExtension = ".converting";

    bool hasTextExtension(const String& path);
    bool hasRsvpExtension(const String& path);
    bool hasEpubExtension(const String& path);
    bool hasFontExtension(const String& path);
    String parentDirectoryForPath(const String& path);
    String siblingPathWithExtension(const String& path, const char* extension);
    String epubSiblingPathForRsvp(const String& rsvpPath);
    String displayNameForPath(const String& path);
    String displayNameWithoutExtension(const String& path);
    String rsvpCachePathForEpub(const String& epubPath);
    String indexedIndexPathFor(const String& path);
    String indexedDataPathFor(const String& path);
    String progressSidecarPathFor(const String& path);
    String indexedTempPathFor(const String& path);
    bool isHiddenOrSidecarPath(const String& path);

} // namespace StoragePaths
