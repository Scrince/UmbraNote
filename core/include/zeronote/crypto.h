#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace zeronote::crypto {

constexpr std::size_t kMinPasswordLengthStandard = 12;
constexpr std::size_t kMinPasswordLengthParanoid = 20;

constexpr std::uint8_t kFlagUsesKeyfile = 0x01;
constexpr std::uint8_t kFlagParanoidKdf = 0x02;

struct EncryptionOptions {
    std::vector<std::uint8_t> keyfile;
    bool paranoid_kdf = true;
};

struct EncryptedFileInfo {
    int version = 0;
    bool requires_keyfile = false;
    bool paranoid_kdf = false;
};

bool IsEncryptedFile(const std::vector<std::uint8_t>& data);
bool GetEncryptedFileInfo(const std::vector<std::uint8_t>& data, EncryptedFileInfo& info);

bool ValidateEncryptionPassword(const std::string& passwordUtf8, bool paranoid_kdf,
                                  std::string& error);

bool EncryptText(const std::string& plaintextUtf8, const std::string& passwordUtf8,
                 std::vector<std::uint8_t>& output, std::string& error,
                 const EncryptionOptions& options = {});

bool DecryptText(const std::vector<std::uint8_t>& data, const std::string& passwordUtf8,
                 std::string& plaintextUtf8, std::string& error,
                 const EncryptionOptions& options = {});

}