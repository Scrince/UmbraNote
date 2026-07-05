#include <zeronote/pdf_export.h>

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace zeronote::pdf {

namespace {

constexpr double kPageWidth = 612.0;
constexpr double kPageHeight = 792.0;
constexpr double kMargin = 72.0;
constexpr double kFontSize = 12.0;
constexpr double kLineHeight = 14.0;
constexpr int kMaxColumns = 72;

uint32_t DecodeUtf8Codepoint(const std::string& text, size_t& index) {
    const unsigned char c = static_cast<unsigned char>(text[index]);
    if (c < 0x80) {
        ++index;
        return c;
    }
    if ((c & 0xE0) == 0xC0 && index + 1 < text.size()) {
        const uint32_t cp = ((c & 0x1F) << 6) |
                            (static_cast<unsigned char>(text[index + 1]) & 0x3F);
        index += 2;
        return cp;
    }
    if ((c & 0xF0) == 0xE0 && index + 2 < text.size()) {
        const uint32_t cp = ((c & 0x0F) << 12) |
                            ((static_cast<unsigned char>(text[index + 1]) & 0x3F) << 6) |
                            (static_cast<unsigned char>(text[index + 2]) & 0x3F);
        index += 3;
        return cp;
    }
    if ((c & 0xF8) == 0xF0 && index + 3 < text.size()) {
        const uint32_t cp = ((c & 0x07) << 18) |
                            ((static_cast<unsigned char>(text[index + 1]) & 0x3F) << 12) |
                            ((static_cast<unsigned char>(text[index + 2]) & 0x3F) << 6) |
                            (static_cast<unsigned char>(text[index + 3]) & 0x3F);
        index += 4;
        return cp;
    }
    ++index;
    return 0xFFFD;
}

void AppendUtf8ForCodepoint(uint32_t cp, std::string& out) {
    if (cp < 0x80) {
        out.push_back(static_cast<char>(cp));
    } else if (cp < 0x800) {
        out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
        out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
}

int DisplayWidth(uint32_t cp) {
    if (cp < 0x80) return 1;
    if (cp < 0x1100) return 1;
    return 2;
}

bool ContainsNonAscii(const std::string& text) {
    for (unsigned char ch : text) {
        if (ch >= 0x80) return true;
    }
    return false;
}

bool LoadFontBytes(std::vector<uint8_t>& bytes) {
#ifdef _WIN32
    const wchar_t* paths[] = {
        L"C:\\Windows\\Fonts\\segoeui.ttf",
        L"C:\\Windows\\Fonts\\arial.ttf",
        L"C:\\Windows\\Fonts\\calibri.ttf",
    };
    for (const wchar_t* path : paths) {
        HANDLE handle = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, nullptr,
                                    OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (handle == INVALID_HANDLE_VALUE) continue;
        const DWORD size = GetFileSize(handle, nullptr);
        if (size == INVALID_FILE_SIZE || size == 0) {
            CloseHandle(handle);
            continue;
        }
        bytes.resize(size);
        DWORD read = 0;
        const BOOL ok = ReadFile(handle, bytes.data(), size, &read, nullptr);
        CloseHandle(handle);
        if (ok && read == size) return true;
        bytes.clear();
    }
#else
    const char* paths[] = {
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/TTF/DejaVuSans.ttf",
        "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
        "/usr/share/fonts/truetype/noto/NotoSans-Regular.ttf",
        "/usr/local/share/fonts/dejavu/DejaVuSans.ttf",
    };
    for (const char* path : paths) {
        std::ifstream file(path, std::ios::binary);
        if (!file) continue;
        bytes.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
        if (!bytes.empty()) return true;
        bytes.clear();
    }
#endif
    return false;
}

std::vector<std::string> SplitLines(const std::string& text) {
    std::vector<std::string> lines;
    std::string current;
    for (char ch : text) {
        if (ch == '\n') {
            lines.push_back(current);
            current.clear();
        } else if (ch != '\r') {
            current.push_back(ch);
        }
    }
    lines.push_back(current);
    return lines;
}

std::vector<std::string> WrapLine(const std::string& line) {
    if (line.empty()) return {""};

    std::vector<std::string> wrapped;
    std::string current;
    int width = 0;
    size_t index = 0;

    while (index < line.size()) {
        const size_t cpStart = index;
        const uint32_t cp = DecodeUtf8Codepoint(line, index);
        const int cw = DisplayWidth(cp);

        if (width + cw > kMaxColumns && !current.empty()) {
            wrapped.push_back(current);
            current.clear();
            width = 0;
            index = cpStart;
            continue;
        }

        AppendUtf8ForCodepoint(cp, current);
        width += cw;
    }

    if (!current.empty()) {
        wrapped.push_back(current);
    }
    return wrapped;
}

std::vector<std::string> WrapText(const std::string& text) {
    std::vector<std::string> wrapped;
    for (const std::string& line : SplitLines(text)) {
        const auto parts = WrapLine(line);
        wrapped.insert(wrapped.end(), parts.begin(), parts.end());
    }
    if (wrapped.empty()) {
        wrapped.push_back("");
    }
    return wrapped;
}

std::string EscapePdfLiteral(const std::string& text) {
    std::string out;
    out.reserve(text.size() + 8);
    for (unsigned char ch : text) {
        switch (ch) {
            case '\\': out += "\\\\"; break;
            case '(': out += "\\("; break;
            case ')': out += "\\)"; break;
            case '\r': break;
            default:
                if (ch >= 0x20 && ch < 0x7F) {
                    out.push_back(static_cast<char>(ch));
                }
                break;
        }
    }
    return out;
}

void AppendUtf16BeHex(const std::string& text, std::string& hex) {
    size_t i = 0;
    while (i < text.size()) {
        const uint32_t cp = DecodeUtf8Codepoint(text, i);
        if (cp <= 0xFFFF) {
            char buf[8];
            std::snprintf(buf, sizeof(buf), "%04X", static_cast<unsigned int>(cp));
            hex += buf;
        } else {
            const uint32_t value = cp - 0x10000;
            const uint16_t high = static_cast<uint16_t>(0xD800 + (value >> 10));
            const uint16_t low = static_cast<uint16_t>(0xDC00 + (value & 0x3FF));
            char buf[16];
            std::snprintf(buf, sizeof(buf), "%04X%04X",
                          static_cast<unsigned int>(high),
                          static_cast<unsigned int>(low));
            hex += buf;
        }
    }
}

std::string BuildAsciiTextOperator(const std::string& line) {
    if (line.empty()) return "T*";
    return "(" + EscapePdfLiteral(line) + ") Tj T*";
}

std::string BuildUnicodeTextOperator(const std::string& line) {
    if (line.empty()) return "T*";
    std::string hex;
    AppendUtf16BeHex(line, hex);
    return "<FEFF" + hex + "> Tj T*";
}

std::string BuildToUnicodeCMap() {
    return R"(/CIDInit /ProcSet findresource begin
12 dict begin
begincmap
/CIDSystemInfo << /Registry (Adobe) /Ordering (UCS) /Supplement 0 >> def
/CMapName /UmbraNote-UTF16 def
/CMapType 2 def
1 begincodespacerange
<0000> <FFFF>
endcodespacerange
endcmap
CMapName currentdict /CMap defineresource pop
end
end)";
}

}

bool ExportTextToPdf(const std::string& utf8Text, const std::string& path,
                     std::string& error) {
    const std::vector<std::string> lines = WrapText(utf8Text);
    const bool needsUnicode = ContainsNonAscii(utf8Text);
    std::vector<uint8_t> fontBytes;
    if (needsUnicode && !LoadFontBytes(fontBytes)) {
        error = "Cannot export non-ASCII text: no Unicode font found on this system.";
        return false;
    }

    const double usableHeight = kPageHeight - (2.0 * kMargin);
    const int linesPerPage = std::max(1, static_cast<int>(usableHeight / kLineHeight));

    std::vector<std::vector<std::string>> pages;
    for (size_t i = 0; i < lines.size(); i += static_cast<size_t>(linesPerPage)) {
        const size_t end = std::min(lines.size(), i + static_cast<size_t>(linesPerPage));
        pages.emplace_back(lines.begin() + static_cast<std::ptrdiff_t>(i),
                           lines.begin() + static_cast<std::ptrdiff_t>(end));
    }
    if (pages.empty()) {
        pages.push_back({""});
    }

    const int fontBlockSize = needsUnicode ? 5 : 1;
    const int firstFontId = 3 + static_cast<int>(pages.size() * 2);
    std::vector<std::string> bodies;
    bodies.resize(static_cast<size_t>(firstFontId + fontBlockSize - 1));

    bodies[0] = "<< /Type /Catalog /Pages 2 0 R >>";

    std::ostringstream kids;
    kids << "[ ";
    for (size_t i = 0; i < pages.size(); ++i) {
        kids << (3 + static_cast<int>(i * 2)) << " 0 R ";
    }
    kids << "]";
    bodies[1] = "<< /Type /Pages /Kids " + kids.str() +
                " /Count " + std::to_string(pages.size()) + " >>";

    for (size_t pageIndex = 0; pageIndex < pages.size(); ++pageIndex) {
        const int pageId = 3 + static_cast<int>(pageIndex * 2);
        const int contentId = pageId + 1;

        std::ostringstream content;
        content << "BT\n";
        content << "/F1 " << kFontSize << " Tf\n";
        content << "1 0 0 1 " << kMargin << " " << (kPageHeight - kMargin) << " Tm\n";
        content << kLineHeight << " TL\n";
        for (const std::string& line : pages[pageIndex]) {
            content << (needsUnicode ? BuildUnicodeTextOperator(line)
                                     : BuildAsciiTextOperator(line))
                    << "\n";
        }
        content << "ET";

        const std::string stream = content.str();
        bodies[static_cast<size_t>(contentId - 1)] =
            "<< /Length " + std::to_string(stream.size()) + " >>\nstream\n" +
            stream + "\nendstream";

        bodies[static_cast<size_t>(pageId - 1)] =
            "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 " +
            std::to_string(static_cast<int>(kPageWidth)) + " " +
            std::to_string(static_cast<int>(kPageHeight)) +
            "] /Resources << /Font << /F1 " + std::to_string(firstFontId) +
            " 0 R >> >> /Contents " + std::to_string(contentId) + " 0 R >>";
    }

    if (needsUnicode) {
        const int type0Id = firstFontId;
        const int cidFontId = firstFontId + 1;
        const int fontDescriptorId = firstFontId + 2;
        const int fontFileId = firstFontId + 3;
        const int toUnicodeId = firstFontId + 4;

        std::ostringstream fontStream;
        fontStream.write(reinterpret_cast<const char*>(fontBytes.data()),
                         static_cast<std::streamsize>(fontBytes.size()));
        const std::string fontData = fontStream.str();
        bodies[static_cast<size_t>(fontFileId - 1)] =
            "<< /Length " + std::to_string(fontData.size()) + " /Length1 " +
            std::to_string(fontData.size()) + " >>\nstream\n" + fontData + "\nendstream";

        bodies[static_cast<size_t>(fontDescriptorId - 1)] =
            "<< /Type /FontDescriptor /FontName /UmbraNoteUnicode "
            "/Flags 4 /FontBBox [-1000 -500 2000 1500] /ItalicAngle 0 "
            "/Ascent 1000 /Descent -250 /CapHeight 700 /StemV 80 "
            "/FontFile2 " + std::to_string(fontFileId) + " 0 R >>";

        bodies[static_cast<size_t>(cidFontId - 1)] =
            "<< /Type /Font /Subtype /CIDFontType2 /BaseFont /UmbraNoteUnicode "
            "/CIDSystemInfo << /Registry (Adobe) /Ordering (Identity) /Supplement 0 >> "
            "/FontDescriptor " + std::to_string(fontDescriptorId) + " 0 R "
            "/CIDToGIDMap /Identity /DW 1000 >>";

        bodies[static_cast<size_t>(type0Id - 1)] =
            "<< /Type /Font /Subtype /Type0 /BaseFont /UmbraNoteUnicode "
            "/Encoding /Identity-H /DescendantFonts [" +
            std::to_string(cidFontId) + " 0 R] "
            "/ToUnicode " + std::to_string(toUnicodeId) + " 0 R >>";

        const std::string cmap = BuildToUnicodeCMap();
        bodies[static_cast<size_t>(toUnicodeId - 1)] =
            "<< /Length " + std::to_string(cmap.size()) + " >>\nstream\n" + cmap +
            "\nendstream";
    } else {
        bodies[static_cast<size_t>(firstFontId - 1)] =
            "<< /Type /Font /Subtype /Type1 /BaseFont /Courier >>";
    }

    std::ostringstream pdf;
    pdf << "%PDF-1.4\n";
    std::vector<size_t> offsets(bodies.size() + 1, 0);

    for (size_t id = 1; id <= bodies.size(); ++id) {
        offsets[id] = static_cast<size_t>(pdf.tellp());
        pdf << id << " 0 obj\n" << bodies[id - 1] << "\nendobj\n";
    }

    const size_t xrefOffset = static_cast<size_t>(pdf.tellp());
    pdf << "xref\n0 " << (bodies.size() + 1) << "\n";
    pdf << "0000000000 65535 f \n";
    for (size_t id = 1; id <= bodies.size(); ++id) {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%010zu 00000 n \n", offsets[id]);
        pdf << buf;
    }
    pdf << "trailer\n<< /Size " << (bodies.size() + 1) << " /Root 1 0 R >>\n";
    pdf << "startxref\n" << xrefOffset << "\n%%EOF\n";

    std::ofstream file(path, std::ios::binary);
    if (!file) {
        error = "Cannot create PDF file.";
        return false;
    }
    const std::string output = pdf.str();
    file.write(output.data(), static_cast<std::streamsize>(output.size()));
    if (!file) {
        error = "Failed to write PDF file.";
        return false;
    }
    return true;
}

}