#include <zeronote/crypto_internal.h>

#include <array>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace {

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

bool WriteHex(const char* path, const std::vector<std::uint8_t>& bytes) {
    std::ofstream out(path);
    if (!out) return false;
    out << BytesToHex(bytes) << '\n';
    return out.good();
}

}

int main() {
    constexpr char kPassword[] = "TestPassword12!";
    constexpr char kPlaintext[] = "UmbraNote legacy fixture";

    const auto saltV2 = FixedSaltV2();
    const auto saltV1 = FixedSaltV1();
    const auto iv = FixedIv();

    std::vector<std::uint8_t> v2;
    if (!zeronote::crypto::EncryptLegacyV2ForTest(
            kPlaintext, kPassword, saltV2.data(), iv.data(), v2)) {
        std::cerr << "Failed to generate v2 fixture\n";
        return 1;
    }

    std::vector<std::uint8_t> v1;
    if (!zeronote::crypto::EncryptLegacyV1ForTest(
            kPlaintext, kPassword, saltV1.data(), iv.data(), v1)) {
        std::cerr << "Failed to generate v1 fixture\n";
        return 1;
    }

    if (!WriteHex("tests/fixtures/v2_golden.hex", v2) ||
        !WriteHex("tests/fixtures/v1_golden.hex", v1)) {
        std::cerr << "Failed to write fixture files\n";
        return 1;
    }

    std::cout << "Wrote tests/fixtures/v2_golden.hex and v1_golden.hex\n";
    return 0;
}