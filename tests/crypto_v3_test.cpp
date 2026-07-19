#include <zeronote/crypto.h>
#include <zeronote/crypto_internal.h>
#include <zeronote/text_codec.h>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <chrono>
#include <cctype>
#include <cstring>
#include <cstdint>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#ifndef _WIN32
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace {

constexpr char kLegacyPassword[] = "TestPassword12!";
constexpr char kParanoidPassword[] = "TestPasswordTwentyChars!!";
constexpr char kFixturePlaintext[] = "UmbraNote legacy fixture";

std::vector<std::uint8_t> HexToBytes(const std::string& hex) {
    std::vector<std::uint8_t> out;
    out.reserve(hex.size() / 2);
    for (std::size_t i = 0; i + 1 < hex.size(); i += 2) {
        const auto byte = static_cast<std::uint8_t>(std::stoul(hex.substr(i, 2), nullptr, 16));
        out.push_back(byte);
    }
    return out;
}

std::string BytesToHex(const std::vector<std::uint8_t>& bytes) {
    static const char* kHex = "0123456789abcdef";
    std::string out;
    out.reserve(bytes.size() * 2);
    for (const auto b : bytes) {
        out.push_back(kHex[b >> 4]);
        out.push_back(kHex[b & 0x0F]);
    }
    return out;
}

std::vector<std::uint8_t> LoadFixtureHex(const char* filename) {
    std::string path = std::string("tests/fixtures/") + filename;
    std::ifstream in(path);
    if (!in) {
        in.open(std::string("fixtures/") + filename);
    }
    REQUIRE(in.good());
    std::ostringstream ss;
    ss << in.rdbuf();
    std::string hex;
    for (char ch : ss.str()) {
        if ((ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f') || (ch >= 'A' && ch <= 'F')) {
            hex.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
        }
    }
    return HexToBytes(hex);
}

std::vector<std::uint8_t> MakeKeyfile(std::size_t size, std::uint8_t seed = 0x41) {
    std::vector<std::uint8_t> keyfile(size);
    for (std::size_t i = 0; i < size; ++i) {
        keyfile[i] = static_cast<std::uint8_t>(seed + static_cast<std::uint8_t>(i));
    }
    return keyfile;
}

std::array<std::uint8_t, 32> FixedSaltV2() {
    std::array<std::uint8_t, 32> salt{};
    for (std::size_t i = 0; i < salt.size(); ++i) {
        salt[i] = static_cast<std::uint8_t>(i + 1);
    }
    return salt;
}

std::array<std::uint8_t, 16> FixedSaltV1() {
    std::array<std::uint8_t, 16> salt{};
    for (std::size_t i = 0; i < salt.size(); ++i) {
        salt[i] = static_cast<std::uint8_t>(0x10 + i);
    }
    return salt;
}

std::array<std::uint8_t, 12> FixedIv() {
    std::array<std::uint8_t, 12> iv{};
    for (std::size_t i = 0; i < iv.size(); ++i) {
        iv[i] = static_cast<std::uint8_t>(0xA0 + i);
    }
    return iv;
}

zeronote::crypto::EncryptionOptions ParanoidOptions() {
    zeronote::crypto::EncryptionOptions options;
    options.paranoid_kdf = true;
    options.keyfile = MakeKeyfile(zeronote::crypto::kMinKeyfileSize);
    return options;
}

zeronote::crypto::EncryptionOptions StandardOptions() {
    zeronote::crypto::EncryptionOptions options;
    options.paranoid_kdf = false;
    return options;
}

}

TEST_CASE("v3 encrypt/decrypt round-trip (standard)", "[crypto][v3]") {
    std::vector<std::uint8_t> encrypted;
    std::string error;
    REQUIRE(zeronote::crypto::EncryptText(
        "Hello, UmbraNote v3!", kLegacyPassword, encrypted, error, StandardOptions()));
    REQUIRE(error.empty());

    std::string plaintext;
    REQUIRE(zeronote::crypto::DecryptText(encrypted, kLegacyPassword, plaintext, error));
    REQUIRE(error.empty());
    REQUIRE(plaintext == "Hello, UmbraNote v3!");
}

TEST_CASE("v3 encrypt/decrypt round-trip (paranoid + keyfile)", "[crypto][v3]") {
    const auto options = ParanoidOptions();
    std::vector<std::uint8_t> encrypted;
    std::string error;
    REQUIRE(zeronote::crypto::EncryptText(
        "High-security note body.", kParanoidPassword, encrypted, error, options));
    REQUIRE(error.empty());

    std::string plaintext;
    REQUIRE(zeronote::crypto::DecryptText(
        encrypted, kParanoidPassword, plaintext, error, options));
    REQUIRE(error.empty());
    REQUIRE(plaintext == "High-security note body.");
}

TEST_CASE("v3 rejects tampered ciphertext", "[crypto][v3]") {
    std::vector<std::uint8_t> encrypted;
    std::string error;
    REQUIRE(zeronote::crypto::EncryptText(
        "tamper test", kLegacyPassword, encrypted, error, StandardOptions()));

    encrypted.back() ^= 0x01;
    std::string plaintext;
    REQUIRE_FALSE(zeronote::crypto::DecryptText(encrypted, kLegacyPassword, plaintext, error));
    REQUIRE(error == "Incorrect password, keyfile, or corrupted file.");
}

TEST_CASE("v3 rejects disallowed KDF parameters before derivation", "[crypto][v3]") {
    std::vector<std::uint8_t> encrypted;
    std::string error;
    REQUIRE(zeronote::crypto::EncryptText(
        "kdf allowlist", kLegacyPassword, encrypted, error, StandardOptions()));

    encrypted[10] = 0xFF;
    encrypted[11] = 0xFF;
    encrypted[12] = 0xFF;
    encrypted[13] = 0xFF;

    const auto start = std::chrono::steady_clock::now();
    std::string plaintext;
    REQUIRE_FALSE(zeronote::crypto::DecryptText(encrypted, kLegacyPassword, plaintext, error));
    const auto elapsed = std::chrono::steady_clock::now() - start;
    REQUIRE(error == "Encrypted file is corrupt.");
    REQUIRE(elapsed < std::chrono::seconds(2));
}

TEST_CASE("v3 rejects inconsistent paranoid flag and KDF profile", "[crypto][v3]") {
    std::vector<std::uint8_t> encrypted;
    std::string error;
    REQUIRE(zeronote::crypto::EncryptText(
        "flag consistency", kLegacyPassword, encrypted, error, StandardOptions()));

    encrypted[7] |= zeronote::crypto::kFlagParanoidKdf;

    std::string plaintext;
    REQUIRE_FALSE(zeronote::crypto::DecryptText(encrypted, kLegacyPassword, plaintext, error));
    REQUIRE(error == "Encrypted file is corrupt.");
}

TEST_CASE("v3 rejects short keyfile on encrypt", "[crypto][v3]") {
    zeronote::crypto::EncryptionOptions options;
    options.paranoid_kdf = false;
    options.keyfile = MakeKeyfile(8);

    std::vector<std::uint8_t> encrypted;
    std::string error;
    REQUIRE_FALSE(zeronote::crypto::EncryptText(
        "short keyfile", kLegacyPassword, encrypted, error, options));
    REQUIRE(error == "Keyfile must be at least 32 bytes.");
}

TEST_CASE("v3 rejects short keyfile on decrypt when keyfile flag set", "[crypto][v3]") {
    const auto options = ParanoidOptions();
    std::vector<std::uint8_t> encrypted;
    std::string error;
    REQUIRE(zeronote::crypto::EncryptText(
        "keyfile size", kParanoidPassword, encrypted, error, options));

    zeronote::crypto::EncryptionOptions badOptions;
    badOptions.keyfile = MakeKeyfile(8);

    std::string plaintext;
    REQUIRE_FALSE(zeronote::crypto::DecryptText(
        encrypted, kParanoidPassword, plaintext, error, badOptions));
    REQUIRE(error == "Keyfile must be at least 32 bytes.");
}

TEST_CASE("legacy v2 decrypt via golden fixture", "[crypto][legacy]") {
    const auto fixture = LoadFixtureHex("v2_golden.hex");
    REQUIRE(zeronote::crypto::IsEncryptedFile(fixture));

    std::string plaintext;
    std::string error;
    REQUIRE(zeronote::crypto::DecryptText(fixture, kLegacyPassword, plaintext, error));
    REQUIRE(error.empty());
    REQUIRE(plaintext == kFixturePlaintext);
}

TEST_CASE("legacy v1 decrypt via golden fixture", "[crypto][legacy]") {
    const auto fixture = LoadFixtureHex("v1_golden.hex");
    REQUIRE(zeronote::crypto::IsEncryptedFile(fixture));

    std::string plaintext;
    std::string error;
    REQUIRE(zeronote::crypto::DecryptText(fixture, kLegacyPassword, plaintext, error));
    REQUIRE(error.empty());
    REQUIRE(plaintext == kFixturePlaintext);
}

TEST_CASE("legacy v2 round-trip via test encrypt helper", "[crypto][legacy]") {
    const auto salt = FixedSaltV2();
    const auto iv = FixedIv();
    std::vector<std::uint8_t> encrypted;
    REQUIRE(zeronote::crypto::EncryptLegacyV2ForTest(
        kFixturePlaintext, kLegacyPassword, salt.data(), iv.data(), encrypted));

    std::string plaintext;
    std::string error;
    REQUIRE(zeronote::crypto::DecryptText(encrypted, kLegacyPassword, plaintext, error));
    REQUIRE(error.empty());
    REQUIRE(plaintext == kFixturePlaintext);
}

TEST_CASE("legacy v2 rejects unexpected KDF parameters before derivation", "[crypto][legacy]") {
    const auto salt = FixedSaltV2();
    const auto iv = FixedIv();
    std::vector<std::uint8_t> encrypted;
    REQUIRE(zeronote::crypto::EncryptLegacyV2ForTest(
        kFixturePlaintext, kLegacyPassword, salt.data(), iv.data(), encrypted));

    encrypted[7] = 0x7F;
    encrypted[10] = 0xFF;
    encrypted[11] = 0xFF;
    encrypted[12] = 0xFF;
    encrypted[13] = 0xFF;

    const auto start = std::chrono::steady_clock::now();
    std::string plaintext;
    std::string error;
    REQUIRE_FALSE(zeronote::crypto::DecryptText(
        encrypted, kLegacyPassword, plaintext, error));
    const auto elapsed = std::chrono::steady_clock::now() - start;
    REQUIRE(error == "File is not an UmbraNote encrypted note.");
    REQUIRE(elapsed < std::chrono::seconds(2));
}

TEST_CASE("GetEncryptedFileInfo legacy v2 has paranoid_kdf false", "[crypto][info]") {
    const auto salt = FixedSaltV2();
    const auto iv = FixedIv();
    std::vector<std::uint8_t> encrypted;
    REQUIRE(zeronote::crypto::EncryptLegacyV2ForTest(
        kFixturePlaintext, kLegacyPassword, salt.data(), iv.data(), encrypted));

    zeronote::crypto::EncryptedFileInfo info;
    REQUIRE(zeronote::crypto::GetEncryptedFileInfo(encrypted, info));
    REQUIRE(info.version == 2);
    REQUIRE_FALSE(info.requires_keyfile);
    REQUIRE_FALSE(info.paranoid_kdf);
}

TEST_CASE("GetEncryptedFileInfo v3 reflects keyfile and paranoid flags", "[crypto][info]") {
    const auto options = ParanoidOptions();
    std::vector<std::uint8_t> encrypted;
    std::string error;
    REQUIRE(zeronote::crypto::EncryptText(
        "info flags", kParanoidPassword, encrypted, error, options));

    zeronote::crypto::EncryptedFileInfo info;
    REQUIRE(zeronote::crypto::GetEncryptedFileInfo(encrypted, info));
    REQUIRE(info.version == 3);
    REQUIRE(info.requires_keyfile);
    REQUIRE(info.paranoid_kdf);
}

TEST_CASE("golden fixtures match deterministic test encrypt output", "[crypto][fixtures]") {
    const auto saltV2 = FixedSaltV2();
    const auto saltV1 = FixedSaltV1();
    const auto iv = FixedIv();

    std::vector<std::uint8_t> v2Generated;
    REQUIRE(zeronote::crypto::EncryptLegacyV2ForTest(
        kFixturePlaintext, kLegacyPassword, saltV2.data(), iv.data(), v2Generated));
    REQUIRE(BytesToHex(v2Generated) == BytesToHex(LoadFixtureHex("v2_golden.hex")));

    std::vector<std::uint8_t> v1Generated;
    REQUIRE(zeronote::crypto::EncryptLegacyV1ForTest(
        kFixturePlaintext, kLegacyPassword, saltV1.data(), iv.data(), v1Generated));
    REQUIRE(BytesToHex(v1Generated) == BytesToHex(LoadFixtureHex("v1_golden.hex")));
}

#ifndef _WIN32
TEST_CASE("atomic binary write preserves existing file mode", "[io]") {
    char path[] = "/tmp/umbranote-mode-XXXXXX";
    const int fd = mkstemp(path);
    REQUIRE(fd >= 0);
    close(fd);

    REQUIRE(chmod(path, S_IRUSR | S_IWUSR) == 0);
    const std::vector<std::uint8_t> bytes = {'o', 'k'};
    REQUIRE(zeronote::WriteFileBytesAtomic(path, bytes));

    struct stat after {};
    REQUIRE(stat(path, &after) == 0);
    REQUIRE((after.st_mode & (S_IRWXU | S_IRWXG | S_IRWXO)) == (S_IRUSR | S_IWUSR));

    std::vector<std::uint8_t> readBack;
    REQUIRE(zeronote::ReadFileBytes(path, readBack));
    REQUIRE(readBack == bytes);
    unlink(path);
}
#endif
