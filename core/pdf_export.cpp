#include <zeronote/pdf_export.h>

#include <algorithm>
#include <cstdio>
#include <cstdint>
#include <fstream>
#include <set>
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

uint16_t ReadBe16(const std::vector<uint8_t>& bytes, size_t offset) {
    if (offset + 2 > bytes.size()) return 0;
    return static_cast<uint16_t>((bytes[offset] << 8) | bytes[offset + 1]);
}

uint32_t ReadBe32(const std::vector<uint8_t>& bytes, size_t offset) {
    if (offset + 4 > bytes.size()) return 0;
    return (static_cast<uint32_t>(bytes[offset]) << 24) |
           (static_cast<uint32_t>(bytes[offset + 1]) << 16) |
           (static_cast<uint32_t>(bytes[offset + 2]) << 8) |
           static_cast<uint32_t>(bytes[offset + 3]);
}

bool FindTable(const std::vector<uint8_t>& fontBytes, const char tag[4],
               size_t& offset, size_t& length) {
    if (fontBytes.size() < 12) return false;
    const uint16_t tableCount = ReadBe16(fontBytes, 4);
    size_t record = 12;
    for (uint16_t i = 0; i < tableCount && record + 16 <= fontBytes.size(); ++i) {
        if (fontBytes[record] == static_cast<uint8_t>(tag[0]) &&
            fontBytes[record + 1] == static_cast<uint8_t>(tag[1]) &&
            fontBytes[record + 2] == static_cast<uint8_t>(tag[2]) &&
            fontBytes[record + 3] == static_cast<uint8_t>(tag[3])) {
            offset = ReadBe32(fontBytes, record + 8);
            length = ReadBe32(fontBytes, record + 12);
            return offset <= fontBytes.size() && length <= fontBytes.size() - offset;
        }
        record += 16;
    }
    return false;
}

uint16_t GlyphFromFormat4(const std::vector<uint8_t>& fontBytes, size_t subtable,
                          uint32_t codepoint) {
    if (codepoint > 0xFFFF || subtable + 16 > fontBytes.size()) return 0;
    const uint16_t segCount = ReadBe16(fontBytes, subtable + 6) / 2;
    const size_t endCodes = subtable + 14;
    const size_t startCodes = endCodes + static_cast<size_t>(segCount) * 2 + 2;
    const size_t idDeltas = startCodes + static_cast<size_t>(segCount) * 2;
    const size_t idRangeOffsets = idDeltas + static_cast<size_t>(segCount) * 2;
    if (idRangeOffsets + static_cast<size_t>(segCount) * 2 > fontBytes.size()) return 0;

    for (uint16_t i = 0; i < segCount; ++i) {
        const uint16_t endCode = ReadBe16(fontBytes, endCodes + static_cast<size_t>(i) * 2);
        const uint16_t startCode = ReadBe16(fontBytes, startCodes + static_cast<size_t>(i) * 2);
        if (codepoint < startCode || codepoint > endCode) continue;

        const uint16_t rangeOffset = ReadBe16(fontBytes, idRangeOffsets + static_cast<size_t>(i) * 2);
        const int16_t delta = static_cast<int16_t>(
            ReadBe16(fontBytes, idDeltas + static_cast<size_t>(i) * 2));
        uint16_t glyph = 0;
        if (rangeOffset == 0) {
            glyph = static_cast<uint16_t>((codepoint + delta) & 0xFFFF);
        } else {
            const size_t glyphOffset = idRangeOffsets + static_cast<size_t>(i) * 2 +
                rangeOffset + static_cast<size_t>(codepoint - startCode) * 2;
            if (glyphOffset + 2 > fontBytes.size()) return 0;
            glyph = ReadBe16(fontBytes, glyphOffset);
            if (glyph != 0) {
                glyph = static_cast<uint16_t>((glyph + delta) & 0xFFFF);
            }
        }
        return glyph;
    }
    return 0;
}

uint16_t GlyphFromFormat12(const std::vector<uint8_t>& fontBytes, size_t subtable,
                           uint32_t codepoint) {
    if (subtable + 16 > fontBytes.size()) return 0;
    const uint32_t groupCount = ReadBe32(fontBytes, subtable + 12);
    size_t group = subtable + 16;
    for (uint32_t i = 0; i < groupCount && group + 12 <= fontBytes.size(); ++i) {
        const uint32_t startCode = ReadBe32(fontBytes, group);
        const uint32_t endCode = ReadBe32(fontBytes, group + 4);
        const uint32_t startGlyph = ReadBe32(fontBytes, group + 8);
        if (codepoint >= startCode && codepoint <= endCode) {
            const uint32_t glyph = startGlyph + (codepoint - startCode);
            return glyph <= 0xFFFF ? static_cast<uint16_t>(glyph) : 0;
        }
        group += 12;
    }
    return 0;
}

struct CmapSubtables {
    size_t format4 = 0;
    size_t format12 = 0;
};

CmapSubtables FindUnicodeCmaps(const std::vector<uint8_t>& fontBytes) {
    CmapSubtables subtables;
    size_t cmapOffset = 0;
    size_t cmapLength = 0;
    if (!FindTable(fontBytes, "cmap", cmapOffset, cmapLength) || cmapLength < 4) {
        return subtables;
    }

    const uint16_t encodingCount = ReadBe16(fontBytes, cmapOffset + 2);
    size_t record = cmapOffset + 4;
    for (uint16_t i = 0; i < encodingCount && record + 8 <= fontBytes.size(); ++i) {
        const uint16_t platformId = ReadBe16(fontBytes, record);
        const uint16_t encodingId = ReadBe16(fontBytes, record + 2);
        const uint32_t subOffset = ReadBe32(fontBytes, record + 4);
        const size_t subtable = cmapOffset + subOffset;
        if (subtable + 2 <= fontBytes.size() && subOffset < cmapLength) {
            const uint16_t format = ReadBe16(fontBytes, subtable);
            const bool unicodeEncoding =
                platformId == 0 || (platformId == 3 && (encodingId == 1 || encodingId == 10));
            if (unicodeEncoding && format == 4 && subtables.format4 == 0) {
                subtables.format4 = subtable;
            } else if (unicodeEncoding && format == 12 && subtables.format12 == 0) {
                subtables.format12 = subtable;
            }
        }
        record += 8;
    }
    return subtables;
}

uint16_t GlyphForCodepoint(const std::vector<uint8_t>& fontBytes, const CmapSubtables& subtables,
                           uint32_t codepoint) {
    if (subtables.format12 != 0) {
        const uint16_t glyph = GlyphFromFormat12(fontBytes, subtables.format12, codepoint);
        if (glyph != 0) return glyph;
    }
    if (subtables.format4 != 0) {
        return GlyphFromFormat4(fontBytes, subtables.format4, codepoint);
    }
    return 0;
}

std::set<uint32_t> CollectPdfCodeUnits(const std::string& text) {
    std::set<uint32_t> codes;
    size_t index = 0;
    while (index < text.size()) {
        uint32_t cp = DecodeUtf8Codepoint(text, index);
        if (cp == '\r' || cp == '\n') continue;
        if (cp > 0xFFFF) cp = 0xFFFD;
        codes.insert(cp);
    }
    return codes;
}

std::string BuildCidToGidMap(const std::vector<uint8_t>& fontBytes,
                             const std::set<uint32_t>& codes) {
    if (codes.empty()) return std::string(2, '\0');
    const CmapSubtables subtables = FindUnicodeCmaps(fontBytes);
    const uint32_t maxCode = std::min<uint32_t>(*codes.rbegin(), 0xFFFF);
    std::string map;
    map.assign(static_cast<size_t>(maxCode + 1) * 2, '\0');
    for (uint32_t code : codes) {
        if (code > maxCode) continue;
        const uint16_t glyph = GlyphForCodepoint(fontBytes, subtables, code);
        map[static_cast<size_t>(code) * 2] = static_cast<char>((glyph >> 8) & 0xFF);
        map[static_cast<size_t>(code) * 2 + 1] = static_cast<char>(glyph & 0xFF);
    }
    return map;
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
        uint32_t cp = DecodeUtf8Codepoint(text, i);
        if (cp > 0xFFFF) {
            cp = 0xFFFD;
        }
        char buf[8];
        std::snprintf(buf, sizeof(buf), "%04X", static_cast<unsigned int>(cp));
        hex += buf;
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
    return "<" + hex + "> Tj T*";
}

std::string HexCode(uint32_t code) {
    char buf[8];
    std::snprintf(buf, sizeof(buf), "<%04X>", static_cast<unsigned int>(code & 0xFFFF));
    return buf;
}

std::string BuildToUnicodeCMap(const std::set<uint32_t>& codes) {
    std::ostringstream cmap;
    cmap << R"(/CIDInit /ProcSet findresource begin
12 dict begin
begincmap
/CIDSystemInfo << /Registry (Adobe) /Ordering (UCS) /Supplement 0 >> def
/CMapName /UmbraNote-UTF16 def
/CMapType 2 def
1 begincodespacerange
<0000> <FFFF>
endcodespacerange
)";

    auto it = codes.begin();
    while (it != codes.end()) {
        const size_t emitted = static_cast<size_t>(std::distance(codes.begin(), it));
        const size_t count = std::min<size_t>(codes.size() - emitted, 100);
        cmap << count << " beginbfchar\n";
        for (size_t i = 0; i < count && it != codes.end(); ++i, ++it) {
            const std::string hex = HexCode(*it);
            cmap << hex << " " << hex << "\n";
        }
        cmap << "endbfchar\n";
    }

    cmap << R"(endcmap
CMapName currentdict /CMap defineresource pop
end
end)";
    return cmap.str();
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

    const int fontBlockSize = needsUnicode ? 6 : 1;
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
        const int cidToGidId = firstFontId + 4;
        const int toUnicodeId = firstFontId + 5;
        const std::set<uint32_t> pdfCodes = CollectPdfCodeUnits(utf8Text);
        const std::string cidToGid = BuildCidToGidMap(fontBytes, pdfCodes);

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
            "/CIDToGIDMap " + std::to_string(cidToGidId) + " 0 R /DW 1000 >>";

        bodies[static_cast<size_t>(type0Id - 1)] =
            "<< /Type /Font /Subtype /Type0 /BaseFont /UmbraNoteUnicode "
            "/Encoding /Identity-H /DescendantFonts [" +
            std::to_string(cidFontId) + " 0 R] "
            "/ToUnicode " + std::to_string(toUnicodeId) + " 0 R >>";

        bodies[static_cast<size_t>(cidToGidId - 1)] =
            "<< /Length " + std::to_string(cidToGid.size()) +
            " >>\nstream\n" + cidToGid + "\nendstream";

        const std::string cmap = BuildToUnicodeCMap(pdfCodes);
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
