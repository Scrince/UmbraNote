#include <zeronote/crypto.h>
#include <zeronote/secure_memory.h>

#include <sodium.h>

#include <array>
#include <cstring>
#include <vector>

namespace zeronote::crypto {

namespace {

constexpr char kMagicV3[] = {'Z', 'N', 'E', 'N', 'C', '3'};
constexpr std::size_t kMagicSize = 6;
constexpr std::uint8_t kVersionV3 = 3;
constexpr std::uint8_t kKdfArgon2id = 1;

constexpr std::size_t kSaltSizeV3 = 32;
constexpr std::size_t kNonceSizeV3 = crypto_aead_xchacha20poly1305_ietf_NPUBBYTES;
constexpr std::size_t kKeySizeV3 = crypto_aead_xchacha20poly1305_ietf_KEYBYTES;
constexpr std::size_t kHeaderSizeV3 =
    kMagicSize + 1 + 1 + 1 + 4 + 4 + kSaltSizeV3 + kNonceSizeV3;

struct V3Header {
    std::uint8_t flags = 0;
    std::uint32_t opslimit = 0;
    std::uint32_t memlimit = 0;
    std::array<std::uint8_t, kSaltSizeV3> salt{};
    std::array<std::uint8_t, kNonceSizeV3> nonce{};
};

std::uint32_t ReadLe32(const std::uint8_t* bytes) {
    return static_cast<std::uint32_t>(bytes[0]) |
           (static_cast<std::uint32_t>(bytes[1]) << 8) |
           (static_cast<std::uint32_t>(bytes[2]) << 16) |
           (static_cast<std::uint32_t>(bytes[3]) << 24);
}

void WriteLe32(std::uint8_t* bytes, std::uint32_t value) {
    bytes[0] = static_cast<std::uint8_t>(value & 0xFF);
    bytes[1] = static_cast<std::uint8_t>((value >> 8) & 0xFF);
    bytes[2] = static_cast<std::uint8_t>((value >> 16) & 0xFF);
    bytes[3] = static_cast<std::uint8_t>((value >> 24) & 0xFF);
}

void GetKdfLimits(bool paranoid, std::uint32_t& opslimit, std::uint32_t& memlimit) {
    if (paranoid) {
        opslimit = crypto_pwhash_OPSLIMIT_SENSITIVE;
        memlimit = crypto_pwhash_MEMLIMIT_SENSITIVE;
    } else {
        opslimit = crypto_pwhash_OPSLIMIT_MODERATE;
        memlimit = crypto_pwhash_MEMLIMIT_MODERATE;
    }
}

bool BuildKdfInput(const std::string& passwordUtf8, const std::vector<std::uint8_t>& keyfile,
                   SecureBuffer& out) {
    if (passwordUtf8.empty()) return false;
    out.resize(passwordUtf8.size() + keyfile.size());
    if (!out.empty()) {
        std::memcpy(out.data(), passwordUtf8.data(), passwordUtf8.size());
        if (!keyfile.empty()) {
            std::memcpy(out.data() + passwordUtf8.size(), keyfile.data(), keyfile.size());
        }
    }
    return true;
}

bool DeriveKeyV3(const SecureBuffer& kdfInput, const std::uint8_t* salt,
                 std::uint32_t opslimit, std::uint32_t memlimit, SecureBuffer& keyOut) {
    keyOut.resize(kKeySizeV3);
    if (crypto_pwhash(keyOut.data(), keyOut.size(),
                      reinterpret_cast<const char*>(kdfInput.data()), kdfInput.size(),
                      salt, opslimit, memlimit,
                      crypto_pwhash_ALG_ARGON2ID13) != 0) {
        keyOut.clear();
        return false;
    }
    return true;
}

bool BuildHeaderBytes(const V3Header& header, std::vector<std::uint8_t>& out) {
    out.resize(kHeaderSizeV3);
    std::memcpy(out.data(), kMagicV3, kMagicSize);
    out[kMagicSize] = kVersionV3;
    out[kMagicSize + 1] = header.flags;
    out[kMagicSize + 2] = kKdfArgon2id;
    WriteLe32(out.data() + kMagicSize + 3, header.opslimit);
    WriteLe32(out.data() + kMagicSize + 7, header.memlimit);
    std::memcpy(out.data() + kMagicSize + 11, header.salt.data(), header.salt.size());
    std::memcpy(out.data() + kMagicSize + 11 + header.salt.size(),
                header.nonce.data(), header.nonce.size());
    return true;
}

bool ParseV3Header(const std::vector<std::uint8_t>& data, V3Header& header) {
    if (data.size() < kHeaderSizeV3 + crypto_aead_xchacha20poly1305_ietf_ABYTES) {
        return false;
    }
    if (std::memcmp(data.data(), kMagicV3, kMagicSize) != 0) return false;
    if (data[kMagicSize] != kVersionV3) return false;
    if (data[kMagicSize + 2] != kKdfArgon2id) return false;

    header.flags = data[kMagicSize + 1];
    header.opslimit = ReadLe32(data.data() + kMagicSize + 3);
    header.memlimit = ReadLe32(data.data() + kMagicSize + 7);
    std::memcpy(header.salt.data(), data.data() + kMagicSize + 11, header.salt.size());
    std::memcpy(header.nonce.data(),
                data.data() + kMagicSize + 11 + header.salt.size(),
                header.nonce.size());
    return true;
}

}

bool IsEncryptedFileV3(const std::vector<std::uint8_t>& data) {
    V3Header header;
    return ParseV3Header(data, header);
}

bool GetEncryptedFileInfoV3(const std::vector<std::uint8_t>& data, EncryptedFileInfo& info) {
    V3Header header;
    if (!ParseV3Header(data, header)) return false;
    info.version = 3;
    info.requires_keyfile = (header.flags & kFlagUsesKeyfile) != 0;
    info.paranoid_kdf = (header.flags & kFlagParanoidKdf) != 0;
    return true;
}

bool EncryptTextV3(const std::string& plaintextUtf8, const std::string& passwordUtf8,
                   std::vector<std::uint8_t>& output, std::string& error,
                   const EncryptionOptions& options) {
    if (sodium_init() < 0) {
        error = "Failed to initialize cryptographic library.";
        return false;
    }

    std::string passwordError;
    if (!ValidateEncryptionPassword(passwordUtf8, options.paranoid_kdf, passwordError)) {
        error = passwordError;
        return false;
    }
    if (options.paranoid_kdf && options.keyfile.empty()) {
        error = "High-security encryption requires a keyfile in addition to the password.";
        return false;
    }

    SecureBuffer kdfInput;
    if (!BuildKdfInput(passwordUtf8, options.keyfile, kdfInput)) {
        error = "Failed to prepare key derivation input.";
        return false;
    }

    V3Header header;
    header.flags = 0;
    if (!options.keyfile.empty()) header.flags |= kFlagUsesKeyfile;
    if (options.paranoid_kdf) header.flags |= kFlagParanoidKdf;
    GetKdfLimits(options.paranoid_kdf, header.opslimit, header.memlimit);
    randombytes_buf(header.salt.data(), header.salt.size());
    randombytes_buf(header.nonce.data(), header.nonce.size());

    SecureBuffer key;
    if (!DeriveKeyV3(kdfInput, header.salt.data(), header.opslimit, header.memlimit, key)) {
        error = "Failed to derive encryption key.";
        return false;
    }
    kdfInput.clear();

    std::vector<std::uint8_t> headerBytes;
    BuildHeaderBytes(header, headerBytes);

    unsigned long long cipherLen = 0;
    if (crypto_aead_xchacha20poly1305_ietf_encrypt(
            nullptr, &cipherLen,
            reinterpret_cast<const unsigned char*>(plaintextUtf8.data()),
            plaintextUtf8.size(),
            headerBytes.data(), headerBytes.size(),
            nullptr, header.nonce.data(), key.data()) != 0) {
        error = "Failed to size ciphertext.";
        return false;
    }

    std::vector<std::uint8_t> cipher(static_cast<std::size_t>(cipherLen));
    if (crypto_aead_xchacha20poly1305_ietf_encrypt(
            cipher.data(), &cipherLen,
            reinterpret_cast<const unsigned char*>(plaintextUtf8.data()),
            plaintextUtf8.size(),
            headerBytes.data(), headerBytes.size(),
            nullptr, header.nonce.data(), key.data()) != 0) {
        error = "Encryption failed.";
        return false;
    }

    output.clear();
    output.reserve(headerBytes.size() + cipher.size());
    output.insert(output.end(), headerBytes.begin(), headerBytes.end());
    output.insert(output.end(), cipher.begin(), cipher.begin() + static_cast<std::ptrdiff_t>(cipherLen));
    return true;
}

bool DecryptTextV3(const std::vector<std::uint8_t>& data, const std::string& passwordUtf8,
                   std::string& plaintextUtf8, std::string& error,
                   const EncryptionOptions& options) {
    if (sodium_init() < 0) {
        error = "Failed to initialize cryptographic library.";
        return false;
    }

    V3Header header;
    if (!ParseV3Header(data, header)) {
        error = "Encrypted file is corrupt.";
        return false;
    }

    const bool requiresKeyfile = (header.flags & kFlagUsesKeyfile) != 0;
    if (requiresKeyfile && options.keyfile.empty()) {
        error = "This file requires the original keyfile.";
        return false;
    }
    if (passwordUtf8.empty()) {
        error = "Password cannot be empty.";
        return false;
    }

    SecureBuffer kdfInput;
    if (!BuildKdfInput(passwordUtf8, options.keyfile, kdfInput)) {
        error = "Failed to prepare key derivation input.";
        return false;
    }

    SecureBuffer key;
    if (!DeriveKeyV3(kdfInput, header.salt.data(), header.opslimit, header.memlimit, key)) {
        error = "Failed to derive decryption key.";
        return false;
    }
    kdfInput.clear();

    std::vector<std::uint8_t> headerBytes;
    BuildHeaderBytes(header, headerBytes);

    const std::uint8_t* cipher = data.data() + kHeaderSizeV3;
    const std::size_t cipherSize = data.size() - kHeaderSizeV3;
    unsigned long long plainLen = 0;
    if (crypto_aead_xchacha20poly1305_ietf_decrypt(
            nullptr, &plainLen,
            nullptr,
            cipher, cipherSize,
            headerBytes.data(), headerBytes.size(),
            header.nonce.data(), key.data()) != 0) {
        error = "Incorrect password, keyfile, or corrupted file.";
        return false;
    }

    std::vector<std::uint8_t> plain(static_cast<std::size_t>(plainLen));
    if (crypto_aead_xchacha20poly1305_ietf_decrypt(
            plain.data(), &plainLen,
            nullptr,
            cipher, cipherSize,
            headerBytes.data(), headerBytes.size(),
            header.nonce.data(), key.data()) != 0) {
        error = "Incorrect password, keyfile, or corrupted file.";
        return false;
    }

    plaintextUtf8.assign(reinterpret_cast<const char*>(plain.data()),
                         static_cast<std::size_t>(plainLen));
    sodium_memzero(plain.data(), plain.size());
    return true;
}

}