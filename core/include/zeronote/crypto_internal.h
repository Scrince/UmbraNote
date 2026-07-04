#pragma once

#include <zeronote/crypto.h>

#include <vector>

namespace zeronote::crypto {

bool IsEncryptedFileV3(const std::vector<std::uint8_t>& data);
bool GetEncryptedFileInfoV3(const std::vector<std::uint8_t>& data, EncryptedFileInfo& info);
bool EncryptTextV3(const std::string& plaintextUtf8, const std::string& passwordUtf8,
                   std::vector<std::uint8_t>& output, std::string& error,
                   const EncryptionOptions& options);
bool DecryptTextV3(const std::vector<std::uint8_t>& data, const std::string& passwordUtf8,
                   std::string& plaintextUtf8, std::string& error,
                   const EncryptionOptions& options);

}