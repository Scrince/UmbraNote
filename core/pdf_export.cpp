#include <zeronote/pdf_export.h>

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace zeronote::pdf {

namespace {

constexpr double kPageWidth = 612.0;
constexpr double kPageHeight = 792.0;
constexpr double kMargin = 72.0;
constexpr double kFontSize = 12.0;
constexpr double kLineHeight = 14.0;

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

bool IsAsciiOnly(const std::string& text) {
    for (unsigned char ch : text) {
        if (ch >= 0x80) return false;
    }
    return true;
}

std::string BuildPdfTextOperator(const std::string& line) {
    if (line.empty()) return "T*";
    if (IsAsciiOnly(line)) {
        return "(" + EscapePdfLiteral(line) + ") Tj T*";
    }
    std::string hex;
    AppendUtf16BeHex(line, hex);
    return "<FEFF" + hex + "> Tj T*";
}

}

bool ExportTextToPdf(const std::string& utf8Text, const std::string& path,
                     std::string& error) {
    std::vector<std::string> lines;
    std::string current;
    for (char ch : utf8Text) {
        if (ch == '\n') {
            lines.push_back(current);
            current.clear();
        } else if (ch != '\r') {
            current.push_back(ch);
        }
    }
    lines.push_back(current);

    const double usableHeight = kPageHeight - (2.0 * kMargin);
    const int linesPerPage = std::max(1, static_cast<int>(usableHeight / kLineHeight));

    std::vector<std::vector<std::string>> pages;
    for (size_t i = 0; i < lines.size(); i += static_cast<size_t>(linesPerPage)) {
        const size_t end = std::min(lines.size(), i + static_cast<size_t>(linesPerPage));
        pages.emplace_back(lines.begin() + static_cast<std::ptrdiff_t>(i),
                           lines.begin() + static_cast<std::ptrdiff_t>(end));
    }
    if (pages.empty()) {
        pages.push_back({});
    }

    const int fontId = 3 + static_cast<int>(pages.size() * 2);
    std::vector<std::string> bodies;
    bodies.resize(static_cast<size_t>(fontId));

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
            content << BuildPdfTextOperator(line) << "\n";
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
            "] /Resources << /Font << /F1 " + std::to_string(fontId) +
            " 0 R >> >> /Contents " + std::to_string(contentId) + " 0 R >>";
    }

    bodies[static_cast<size_t>(fontId - 1)] =
        "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>";

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