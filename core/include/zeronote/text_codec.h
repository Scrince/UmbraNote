#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace zeronote {

bool ReadFileBytes(const std::string& path, std::vector<uint8_t>& bytes);
bool WriteFileBytes(const std::string& path, const std::vector<uint8_t>& bytes);

bool DecodeTextFromBytes(const std::vector<uint8_t>& bytes, std::string& out);
bool LoadTextFile(const std::string& path, std::string& out);
bool SaveTextFileUtf8(const std::string& path, const std::string& text);

#ifdef _WIN32
bool ReadFileBytes(const std::wstring& path, std::vector<uint8_t>& bytes);
bool WriteFileBytes(const std::wstring& path, const std::vector<uint8_t>& bytes);
bool SaveTextFileUtf8(const std::wstring& path, const std::string& text);
#endif

std::string WideToUtf8(const std::wstring& text);
std::wstring Utf8ToWide(const std::string& text);

}