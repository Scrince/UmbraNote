#include <zeronote/text_codec.h>

#include <fstream>
#include <iterator>
#include <string>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

namespace zeronote {

namespace {

#ifdef _WIN32
bool WriteBytesAtomicImpl(const std::wstring& path, const std::vector<uint8_t>& bytes) {
    const std::wstring temp = path + L".tmp";
    HANDLE handle = CreateFileW(
        temp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        return false;
    }

    const BYTE* data = bytes.empty() ? nullptr : reinterpret_cast<const BYTE*>(bytes.data());
    const DWORD total = static_cast<DWORD>(bytes.size());
    DWORD written = 0;
    const BOOL ok = WriteFile(handle, data, total, &written, nullptr);
    if (!ok || written != total) {
        CloseHandle(handle);
        DeleteFileW(temp.c_str());
        return false;
    }
    if (!FlushFileBuffers(handle)) {
        CloseHandle(handle);
        DeleteFileW(temp.c_str());
        return false;
    }
    CloseHandle(handle);

    if (!MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING)) {
        DeleteFileW(temp.c_str());
        return false;
    }
    return true;
}
#else
bool WriteBytesAtomicImpl(const std::string& path, const std::vector<uint8_t>& bytes) {
    const std::string temp = path + ".tmp";
    const int fd = open(temp.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        return false;
    }

    std::size_t total = 0;
    while (total < bytes.size()) {
        const ssize_t chunk = write(
            fd, bytes.data() + total, bytes.size() - total);
        if (chunk <= 0) {
            close(fd);
            unlink(temp.c_str());
            return false;
        }
        total += static_cast<std::size_t>(chunk);
    }
    if (fsync(fd) != 0) {
        close(fd);
        unlink(temp.c_str());
        return false;
    }
    close(fd);

    if (rename(temp.c_str(), path.c_str()) != 0) {
        unlink(temp.c_str());
        return false;
    }
    return true;
}
#endif

bool Utf8FromBytes(const uint8_t* data, size_t size, std::string& out) {
    if (size == 0) {
        out.clear();
        return true;
    }
    out.assign(reinterpret_cast<const char*>(data), size);
    return true;
}

bool Utf16LeFromBytes(const uint8_t* data, size_t size, std::string& out) {
    if (size < 2 || size % 2 != 0) return false;
    const size_t wcharCount = size / 2;
#ifdef _WIN32
    const int utf8Len = WideCharToMultiByte(
        CP_UTF8, 0, reinterpret_cast<const wchar_t*>(data), static_cast<int>(wcharCount),
        nullptr, 0, nullptr, nullptr);
    if (utf8Len <= 0) return false;
    out.resize(static_cast<size_t>(utf8Len));
    WideCharToMultiByte(CP_UTF8, 0, reinterpret_cast<const wchar_t*>(data),
                        static_cast<int>(wcharCount), &out[0], utf8Len, nullptr, nullptr);
    return true;
#else
    std::wstring wide(wcharCount, L'\0');
    for (size_t i = 0; i < wcharCount; ++i) {
        wide[i] = static_cast<wchar_t>(data[i * 2] | (data[i * 2 + 1] << 8));
    }
    return !wide.empty() ? (out = WideToUtf8(wide), true) : (out.clear(), true);
#endif
}

bool Utf16BeFromBytes(const uint8_t* data, size_t size, std::string& out) {
    if (size < 2 || size % 2 != 0) return false;
    const size_t wcharCount = size / 2;
#ifdef _WIN32
    std::wstring wide(wcharCount, L'\0');
    for (size_t i = 0; i < wcharCount; ++i) {
        wide[i] = static_cast<wchar_t>((data[i * 2] << 8) | data[i * 2 + 1]);
    }
    const int utf8Len = WideCharToMultiByte(
        CP_UTF8, 0, wide.c_str(), static_cast<int>(wide.size()),
        nullptr, 0, nullptr, nullptr);
    if (utf8Len <= 0) return false;
    out.resize(static_cast<size_t>(utf8Len));
    WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), static_cast<int>(wide.size()),
                        &out[0], utf8Len, nullptr, nullptr);
    return true;
#else
    std::wstring wide(wcharCount, L'\0');
    for (size_t i = 0; i < wcharCount; ++i) {
        wide[i] = static_cast<wchar_t>((data[i * 2] << 8) | data[i * 2 + 1]);
    }
    out = WideToUtf8(wide);
    return true;
#endif
}

#ifndef _WIN32
std::wstring Utf8ToWidePortable(const std::string& text) {
    std::wstring wide;
    size_t i = 0;
    while (i < text.size()) {
        const unsigned char c = static_cast<unsigned char>(text[i]);
        uint32_t cp = 0;
        if (c < 0x80) {
            cp = c;
            ++i;
        } else if ((c & 0xE0) == 0xC0 && i + 1 < text.size()) {
            cp = ((c & 0x1F) << 6) | (static_cast<unsigned char>(text[i + 1]) & 0x3F);
            i += 2;
        } else if ((c & 0xF0) == 0xE0 && i + 2 < text.size()) {
            cp = ((c & 0x0F) << 12) |
                 ((static_cast<unsigned char>(text[i + 1]) & 0x3F) << 6) |
                 (static_cast<unsigned char>(text[i + 2]) & 0x3F);
            i += 3;
        } else if ((c & 0xF8) == 0xF0 && i + 3 < text.size()) {
            cp = ((c & 0x07) << 18) |
                 ((static_cast<unsigned char>(text[i + 1]) & 0x3F) << 12) |
                 ((static_cast<unsigned char>(text[i + 2]) & 0x3F) << 6) |
                 (static_cast<unsigned char>(text[i + 3]) & 0x3F);
            i += 4;
        } else {
            ++i;
            continue;
        }
        if (cp <= 0xFFFF) {
            wide.push_back(static_cast<wchar_t>(cp));
        } else {
            cp -= 0x10000;
            wide.push_back(static_cast<wchar_t>(0xD800 + (cp >> 10)));
            wide.push_back(static_cast<wchar_t>(0xDC00 + (cp & 0x3FF)));
        }
    }
    return wide;
}

std::string WideToUtf8Portable(const std::wstring& text) {
    std::string out;
    for (size_t i = 0; i < text.size(); ++i) {
        uint32_t cp = static_cast<uint32_t>(text[i]);
        if (cp >= 0xD800 && cp <= 0xDBFF && i + 1 < text.size()) {
            const uint32_t low = static_cast<uint32_t>(text[i + 1]);
            if (low >= 0xDC00 && low <= 0xDFFF) {
                cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
                ++i;
            }
        }
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
    return out;
}
#endif

}

bool ReadFileBytes(const std::string& path, std::vector<uint8_t>& bytes) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return false;
    bytes.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
    return true;
}

bool WriteFileBytes(const std::string& path, const std::vector<uint8_t>& bytes) {
    std::ofstream file(path, std::ios::binary);
    if (!file) return false;
    if (!bytes.empty()) {
        file.write(reinterpret_cast<const char*>(bytes.data()),
                   static_cast<std::streamsize>(bytes.size()));
    }
    return static_cast<bool>(file);
}

bool WriteFileBytesAtomic(const std::string& path, const std::vector<uint8_t>& bytes) {
#ifndef _WIN32
    return WriteBytesAtomicImpl(path, bytes);
#else
    return WriteBytesAtomicImpl(Utf8ToWide(path), bytes);
#endif
}

bool DecodeTextFromBytes(const std::vector<uint8_t>& bytes, std::string& out) {
    if (bytes.empty()) {
        out.clear();
        return true;
    }

    if (bytes.size() >= 2) {
        const unsigned char b0 = bytes[0];
        const unsigned char b1 = bytes[1];
        if (b0 == 0xFF && b1 == 0xFE) {
            return Utf16LeFromBytes(bytes.data() + 2, bytes.size() - 2, out);
        }
        if (b0 == 0xFE && b1 == 0xFF) {
            return Utf16BeFromBytes(bytes.data() + 2, bytes.size() - 2, out);
        }
        if (bytes.size() >= 3 && b0 == 0xEF && b1 == 0xBB && bytes[2] == 0xBF) {
            return Utf8FromBytes(bytes.data() + 3, bytes.size() - 3, out);
        }
    }

    return Utf8FromBytes(bytes.data(), bytes.size(), out);
}

bool LoadTextFile(const std::string& path, std::string& out) {
    std::vector<uint8_t> bytes;
    return ReadFileBytes(path, bytes) && DecodeTextFromBytes(bytes, out);
}

bool SaveTextFileUtf8(const std::string& path, const std::string& text) {
    std::ofstream file(path, std::ios::binary);
    if (!file) return false;
    if (!text.empty()) {
        file.write(text.data(), static_cast<std::streamsize>(text.size()));
    }
    return static_cast<bool>(file);
}

#ifdef _WIN32
bool ReadFileBytes(const std::wstring& path, std::vector<uint8_t>& bytes) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return false;
    bytes.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
    return true;
}

bool WriteFileBytes(const std::wstring& path, const std::vector<uint8_t>& bytes) {
    std::ofstream file(path, std::ios::binary);
    if (!file) return false;
    if (!bytes.empty()) {
        file.write(reinterpret_cast<const char*>(bytes.data()),
                   static_cast<std::streamsize>(bytes.size()));
    }
    return static_cast<bool>(file);
}

bool WriteFileBytesAtomic(const std::wstring& path, const std::vector<uint8_t>& bytes) {
    return WriteBytesAtomicImpl(path, bytes);
}

bool SaveTextFileUtf8(const std::wstring& path, const std::string& text) {
    std::ofstream file(path, std::ios::binary);
    if (!file) return false;
    if (!text.empty()) {
        file.write(text.data(), static_cast<std::streamsize>(text.size()));
    }
    return static_cast<bool>(file);
}
#endif

std::string WideToUtf8(const std::wstring& text) {
#ifdef _WIN32
    if (text.empty()) return {};
    const int len = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (len <= 1) return {};
    std::string out(static_cast<size_t>(len - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.c_str(), -1, &out[0], len, nullptr, nullptr);
    return out;
#else
    return WideToUtf8Portable(text);
#endif
}

std::wstring Utf8ToWide(const std::string& text) {
#ifdef _WIN32
    if (text.empty()) return {};
    const int len = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0);
    if (len <= 1) return {};
    std::wstring out(static_cast<size_t>(len - 1), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, &out[0], len);
    return out;
#else
    return Utf8ToWidePortable(text);
#endif
}

}
