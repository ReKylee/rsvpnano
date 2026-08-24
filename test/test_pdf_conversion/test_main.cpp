#include <unity.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

#include "converter/PdfTextExtractor.h"

void setUp() {}
void tearDown() {}

namespace {

    std::filesystem::path fixturePath(std::string_view name) {
        return std::filesystem::temp_directory_path() / name;
    }

    std::string pdfString(std::string_view value) {
        std::string escaped;
        escaped.reserve(value.size());
        for (const char c: value) {
            if (c == '\\' || c == '(' || c == ')')
                escaped += '\\';
            escaped += c;
        }
        return escaped;
    }

    void writePdfObjects(const std::filesystem::path& path, const std::vector<std::string>& objects, size_t infoId) {
        std::string pdf = "%PDF-1.4\n";
        std::vector<size_t> offsets;
        offsets.reserve(objects.size());
        for (size_t i = 0; i < objects.size(); ++i) {
            offsets.push_back(pdf.size());
            pdf += std::to_string(i + 1) + " 0 obj\n" + objects[i] + "\nendobj\n";
        }
        const size_t xref = pdf.size();
        pdf += "xref\n0 " + std::to_string(objects.size() + 1) + "\n0000000000 65535 f \n";
        for (const size_t offset: offsets) {
            std::string value = std::to_string(offset);
            pdf += std::string(10 - value.size(), '0') + value + " 00000 n \n";
        }
        pdf += "trailer\n<< /Size " + std::to_string(objects.size() + 1) + " /Root 1 0 R /Info "
             + std::to_string(infoId) + " 0 R >>\nstartxref\n" + std::to_string(xref) + "\n%%EOF\n";

        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        output.write(pdf.data(), static_cast<std::streamsize>(pdf.size()));
    }

    void writePdf(const std::filesystem::path& path, const std::vector<std::string>& pages) {
        std::vector<std::string> objects;
        objects.emplace_back("<< /Type /Catalog /Pages 2 0 R >>");
        std::string kids;
        for (size_t i = 0; i < pages.size(); ++i)
            kids += std::to_string(4 + i * 2) + " 0 R ";
        objects.push_back("<< /Type /Pages /Kids [" + kids + "] /Count " + std::to_string(pages.size()) + " >>");
        objects.emplace_back("<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica /Encoding /WinAnsiEncoding >>");
        for (size_t i = 0; i < pages.size(); ++i) {
            const std::string content = pages[i].empty()
                                          ? ""
                                          : "BT /F1 12 Tf 72 720 Td (" + pdfString(pages[i]) + ") Tj ET";
            objects.push_back("<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] /Resources << /Font << /F1 3 0 R >> >> /Contents "
                              + std::to_string(5 + i * 2) + " 0 R >>");
            objects.push_back("<< /Length " + std::to_string(content.size()) + " >>\nstream\n" + content
                              + "\nendstream");
        }
        const size_t infoId = objects.size() + 1;
        objects.emplace_back("<< /Title (PDF Book) /Author (PDF Author) >>");
        writePdfObjects(path, objects, infoId);
    }

    void writeUnicodePdf(const std::filesystem::path& path) {
        const std::string cmap = R"(/CIDInit /ProcSet findresource begin
12 dict begin
begincmap
/CIDSystemInfo << /Registry (Adobe) /Ordering (UCS) /Supplement 0 >> def
/CMapName /Adobe-Identity-UCS def
/CMapType 2 def
1 begincodespacerange
<0000> <FFFF>
endcodespacerange
2 beginbfchar
<0001> <4F60>
<0002> <597D>
endbfchar
endcmap
CMapName currentdict /CMap defineresource pop
end
end)";
        const std::string content = "BT /F1 12 Tf 72 720 Td <00010002> Tj ET";
        const std::vector<std::string> objects = {
            "<< /Type /Catalog /Pages 2 0 R >>",
            "<< /Type /Pages /Kids [3 0 R] /Count 1 >>",
            "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] /Resources << /Font << /F1 4 0 R >> >> /Contents 6 0 R >>",
            "<< /Type /Font /Subtype /Type0 /BaseFont /Test /Encoding /Identity-H /ToUnicode 5 0 R >>",
            "<< /Length " + std::to_string(cmap.size()) + " >>\nstream\n" + cmap + "\nendstream",
            "<< /Length " + std::to_string(content.size()) + " >>\nstream\n" + content + "\nendstream",
            "<< /Title (Unicode PDF) >>",
        };
        writePdfObjects(path, objects, 7);
    }

    bool collectLine(void* context, std::string_view line) {
        auto& text = *static_cast<std::string*>(context);
        if (!text.empty())
            text += '\n';
        text += line;
        return true;
    }

    void test_pdf_extractor_reads_metadata_and_pages_in_order() {
        const auto path = fixturePath("rsvpnano-pdf-pages.pdf");
        writePdf(path, {"First page text.", "Second page text."});

        {
            auto extractor = PdfTextExtractor::open(path.string());
            TEST_ASSERT_TRUE(extractor.has_value());
            TEST_ASSERT_EQUAL_STRING("PDF Book", extractor->metadata().title.c_str());
            TEST_ASSERT_EQUAL_STRING("PDF Author", extractor->metadata().author.c_str());
            TEST_ASSERT_EQUAL_UINT32(2, extractor->pageCount());

            std::string text;
            TEST_ASSERT_TRUE(extractor->extractPage(0, collectLine, &text).has_value());
            TEST_ASSERT_TRUE(extractor->extractPage(1, collectLine, &text).has_value());
            TEST_ASSERT_EQUAL_STRING("First page text.\nSecond page text.", text.c_str());
        }
        std::filesystem::remove(path);
    }

    void test_pdf_extractor_returns_no_lines_for_image_only_page() {
        const auto path = fixturePath("rsvpnano-pdf-empty.pdf");
        writePdf(path, {""});

        {
            auto extractor = PdfTextExtractor::open(path.string());
            TEST_ASSERT_TRUE(extractor.has_value());
            std::string text;
            TEST_ASSERT_TRUE(extractor->extractPage(0, collectLine, &text).has_value());
            TEST_ASSERT_TRUE(text.empty());
        }
        std::filesystem::remove(path);
    }

    void test_pdf_extractor_uses_to_unicode_character_map() {
        const auto path = fixturePath("rsvpnano-pdf-unicode.pdf");
        writeUnicodePdf(path);

        {
            auto extractor = PdfTextExtractor::open(path.string());
            TEST_ASSERT_TRUE(extractor.has_value());
            std::string text;
            const auto extracted = extractor->extractPage(0, collectLine, &text);
            if (!extracted) {
                const std::string message = extracted.error().message();
                TEST_FAIL_MESSAGE(message.c_str());
            }
            TEST_ASSERT_EQUAL_STRING("你好", text.c_str());
        }
        std::filesystem::remove(path);
    }

} // namespace

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_pdf_extractor_reads_metadata_and_pages_in_order);
    RUN_TEST(test_pdf_extractor_returns_no_lines_for_image_only_page);
    RUN_TEST(test_pdf_extractor_uses_to_unicode_character_map);
    return UNITY_END();
}
