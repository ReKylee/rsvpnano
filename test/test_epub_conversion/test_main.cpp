#include <Arduino.h>
#include <FS.h>
#include <unity.h>

#include <span>
#include <vector>

#include "converter/EpubContentWriter.h"
#include "converter/EpubPackage.h"

void setUp() {}
void tearDown() {}

namespace {

    void test_package_parses_nav_metadata_and_encoded_manifest_paths() {
        const String opf = R"(<package version="3.0"><metadata><dc:title>Fixture</dc:title></metadata><manifest>
            <item id="nav" href="nav.xhtml" media-type="application/xhtml+xml" properties="nav"/>
            <item id="chapter" href="Text/Chapter%20One.xhtml" media-type="application/xhtml+xml"/>
            </manifest><spine><itemref idref="chapter"/></spine></package>)";

        const auto manifest = EpubPackage::parseManifestItems(opf, "OEBPS/");
        TEST_ASSERT_EQUAL_STRING("3.0", EpubPackage::parsePackageVersion(opf).c_str());
        TEST_ASSERT_EQUAL_UINT32(2, manifest.size());
        TEST_ASSERT_EQUAL_STRING("nav", manifest[0].properties.c_str());
        TEST_ASSERT_EQUAL_STRING("OEBPS/Text/Chapter One.xhtml", manifest[1].path.c_str());
    }

    void test_nav_toc_flattens_nested_entries_and_decodes_fragments() {
        const String nav = R"(<html><body><nav epub:type="landmarks"><a href="cover.xhtml">Start</a></nav>
            <nav epub:type="toc"><ol>
            <li><a href="one.xhtml#one">Part One</a><ol>
                <li><a href="Text/Chapter%20One.xhtml#section%201">Chapter One A</a></li>
            </ol></li><li><a href="two.xhtml#two">Part Two</a></li>
            </ol></nav></body></html>)";

        const auto entries = EpubPackage::parseNavTocEntries(nav, "OEBPS/nav.xhtml", "Fixture");
        TEST_ASSERT_EQUAL_UINT32(3, entries.size());
        TEST_ASSERT_EQUAL_STRING("Part One", entries[0].title.c_str());
        TEST_ASSERT_EQUAL_STRING("OEBPS/Text/Chapter One.xhtml", entries[1].path.c_str());
        TEST_ASSERT_EQUAL_STRING("section 1", entries[1].fragment.c_str());
        TEST_ASSERT_EQUAL_STRING("Part Two", entries[2].title.c_str());
    }

    void test_ncx_toc_ignores_non_content_labels() {
        const String ncx = R"(<ncx><navMap>
            <navPoint><navLabel><text>Fixture</text></navLabel><content src="title.xhtml"/></navPoint>
            <navPoint><navLabel><text>Contents</text></navLabel><content src="toc.xhtml"/></navPoint>
            <navPoint><navLabel><text>I. The Arrival</text></navLabel><content src="content.xhtml#chapter-1"/></navPoint>
            </navMap></ncx>)";

        const auto entries = EpubPackage::parseNcxTocEntries(ncx, "OEBPS/toc.ncx", "Fixture");
        TEST_ASSERT_EQUAL_UINT32(1, entries.size());
        TEST_ASSERT_EQUAL_STRING("I. The Arrival", entries[0].title.c_str());
        TEST_ASSERT_EQUAL_STRING("OEBPS/content.xhtml", entries[0].path.c_str());
        TEST_ASSERT_EQUAL_STRING("chapter-1", entries[0].fragment.c_str());
    }

    void test_writer_uses_ordered_toc_labels_and_paragraph_markers() {
        File output;
        size_t wordCount = 0;
        size_t chapterCount = 0;
        String lastChapter;
        const std::vector<EpubPackage::TocEntry> toc = {
            {"content.xhtml", "I. The Arrival", "chapter-1"},
            {"content.xhtml", "II. Father and Son", "chapter-2"},
        };
        const String markup = R"(<body><h1>Book Title</h1><h2 id="chapter-1">I</h2>
            <p>The harbour was bright.</p><h2 id="chapter-2">II</h2><p>The door opened.</p></body>)";

        EpubContent::RsvpContentWriter writer(output, wordCount, 0, lastChapter, chapterCount, toc, true, "content",
                                               "Book Title");
        TEST_ASSERT_TRUE(writer.write(reinterpret_cast<const uint8_t*>(markup.c_str()), markup.length()));
        TEST_ASSERT_TRUE(writer.finish());

        TEST_ASSERT_EQUAL_UINT32(2, chapterCount);
        TEST_ASSERT_EQUAL_STRING(
            "@chapter I. The Arrival\n\n@chapter II. Father and Son\nThe harbour was bright.\n\n@para\nThe door opened.\n",
            output.contents().c_str());
    }

    void test_writer_preserves_punctuation_only_inline_fragments_without_counting_them_as_words() {
        File output;
        size_t wordCount = 0;
        size_t chapterCount = 0;
        String lastChapter;
        const String markup = R"(<body><h1>Letter</h1><p>Dear Reader <span>,</span></p></body>)";

        EpubContent::RsvpContentWriter writer(output, wordCount, 0, lastChapter, chapterCount, {}, false, "Letter",
                                               "Letter");
        TEST_ASSERT_TRUE(writer.write(reinterpret_cast<const uint8_t*>(markup.c_str()), markup.length()));
        TEST_ASSERT_TRUE(writer.finish());

        TEST_ASSERT_EQUAL_UINT32(2, wordCount);
        TEST_ASSERT_EQUAL_STRING("@chapter Letter\nDear Reader ,\n", output.contents().c_str());
    }

} // namespace

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_package_parses_nav_metadata_and_encoded_manifest_paths);
    RUN_TEST(test_nav_toc_flattens_nested_entries_and_decodes_fragments);
    RUN_TEST(test_ncx_toc_ignores_non_content_labels);
    RUN_TEST(test_writer_uses_ordered_toc_labels_and_paragraph_markers);
    RUN_TEST(test_writer_preserves_punctuation_only_inline_fragments_without_counting_them_as_words);
    return UNITY_END();
}
