#include <zeronote/crypto.h>
#include <zeronote/crypto_internal.h>
#include <zeronote/secure_memory.h>

#include <cstring>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <bcrypt.h>
#else
#include <openssl/evp.h>
#include <openssl/rand.h>
#endif

namespace zeronote::crypto {

namespace {

constexpr char kMagicV1[] = {'Z', 'N', 'E', 'N', 'C', '1'};
constexpr char kMagicV2[] = {'Z', 'N', 'E', 'N', 'C', '2'};
constexpr size_t kMagicSize = 6;

constexpr uint8_t kVersionV1 = 1;
constexpr uint8_t kVersionV2 = 2;
constexpr uint8_t kKdfPbkdf2Sha256 = 0;

constexpr size_t kSaltSizeV1 = 16;
constexpr size_t kSaltSizeV2 = 32;
constexpr size_t kIvSize = 12;
constexpr size_t kTagSize = 16;
constexpr size_t kKeySize = 32;

constexpr uint32_t kPbkdf2IterationsV1 = 100000;
constexpr uint32_t kPbkdf2IterationsV2 = 600000;

struct EncryptedHeader {
    int version = 0;
    uint32_t iterations = 0;
    size_t saltSize = 0;
    size_t headerSize = 0;
};

bool RandomBytes(uint8_t* buffer, size_t size) {
#ifdef _WIN32
    return BCryptGenRandom(nullptr, buffer, static_cast<ULONG>(size),
                           BCRYPT_USE_SYSTEM_PREFERRED_RNG) == 0;
#else
    return RAND_bytes(buffer, static_cast<int>(size)) == 1;
#endif
}

#ifdef _WIN32

bool DeriveKeyPbkdf2(const uint8_t* password, size_t passwordSize,
                     const uint8_t* salt, size_t saltSize,
                     uint32_t iterations, uint8_t* keyOut) {
    BCRYPT_ALG_HANDLE alg = nullptr;
    if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr,
                                    BCRYPT_ALG_HANDLE_HMAC_FLAG) != 0) {
        return false;
    }

    const NTSTATUS status = BCryptDeriveKeyPBKDF2(
        alg,
        const_cast<PUCHAR>(password), static_cast<ULONG>(passwordSize),
        const_cast<PUCHAR>(salt), static_cast<ULONG>(saltSize),
        iterations,
        reinterpret_cast<PUCHAR>(keyOut), static_cast<ULONG>(kKeySize),
        0);

    BCryptCloseAlgorithmProvider(alg, 0);
    return status == 0;
}

bool AesGcmEncrypt(const uint8_t* key, const uint8_t* iv,
                   const uint8_t* aad, size_t aadSize,
                   const uint8_t* input, size_t inputSize,
                   uint8_t* output, uint8_t* tag) {
    BCRYPT_ALG_HANDLE alg = nullptr;
    BCRYPT_KEY_HANDLE keyHandle = nullptr;

    if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_AES_ALGORITHM, nullptr, 0) != 0) {
        return false;
    }

    if (BCryptSetProperty(alg, BCRYPT_CHAINING_MODE,
                          reinterpret_cast<PUCHAR>(const_cast<wchar_t*>(BCRYPT_CHAIN_MODE_GCM)),
                          static_cast<ULONG>((wcslen(BCRYPT_CHAIN_MODE_GCM) + 1) * sizeof(wchar_t)),
                          0) != 0) {
        BCryptCloseAlgorithmProvider(alg, 0);
        return false;
    }

    if (BCryptGenerateSymmetricKey(alg, &keyHandle, nullptr, 0,
                                  const_cast<PUCHAR>(key), static_cast<ULONG>(kKeySize), 0) != 0) {
        BCryptCloseAlgorithmProvider(alg, 0);
        return false;
    }

    BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO authInfo;
    BCRYPT_INIT_AUTH_MODE_INFO(authInfo);
    authInfo.pbNonce = const_cast<PUCHAR>(iv);
    authInfo.cbNonce = static_cast<ULONG>(kIvSize);
    authInfo.pbTag = tag;
    authInfo.cbTag = static_cast<ULONG>(kTagSize);
    authInfo.pbAuthData = const_cast<PUCHAR>(aad);
    authInfo.cbAuthData = static_cast<ULONG>(aadSize);

    ULONG resultSize = 0;
    const NTSTATUS status = BCryptEncrypt(
        keyHandle,
        const_cast<PUCHAR>(input), static_cast<ULONG>(inputSize),
        &authInfo,
        nullptr, 0,
        output, static_cast<ULONG>(inputSize),
        &resultSize, 0);

    BCryptDestroyKey(keyHandle);
    BCryptCloseAlgorithmProvider(alg, 0);
    return status == 0 && resultSize == inputSize;
}

bool AesGcmDecrypt(const uint8_t* key, const uint8_t* iv,
                   const uint8_t* aad, size_t aadSize,
                   const uint8_t* input, size_t inputSize,
                   const uint8_t* tag, uint8_t* output) {
    BCRYPT_ALG_HANDLE alg = nullptr;
    BCRYPT_KEY_HANDLE keyHandle = nullptr;

    if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_AES_ALGORITHM, nullptr, 0) != 0) {
        return false;
    }

    if (BCryptSetProperty(alg, BCRYPT_CHAINING_MODE,
                          reinterpret_cast<PUCHAR>(const_cast<wchar_t*>(BCRYPT_CHAIN_MODE_GCM)),
                          static_cast<ULONG>((wcslen(BCRYPT_CHAIN_MODE_GCM) + 1) * sizeof(wchar_t)),
                          0) != 0) {
        BCryptCloseAlgorithmProvider(alg, 0);
        return false;
    }

    if (BCryptGenerateSymmetricKey(alg, &keyHandle, nullptr, 0,
                                  const_cast<PUCHAR>(key), static_cast<ULONG>(kKeySize), 0) != 0) {
        BCryptCloseAlgorithmProvider(alg, 0);
        return false;
    }

    BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO authInfo;
    BCRYPT_INIT_AUTH_MODE_INFO(authInfo);
    authInfo.pbNonce = const_cast<PUCHAR>(iv);
    authInfo.cbNonce = static_cast<ULONG>(kIvSize);
    authInfo.pbTag = const_cast<PUCHAR>(tag);
    authInfo.cbTag = static_cast<ULONG>(kTagSize);
    authInfo.pbAuthData = const_cast<PUCHAR>(aad);
    authInfo.cbAuthData = static_cast<ULONG>(aadSize);

    ULONG resultSize = 0;
    const NTSTATUS status = BCryptDecrypt(
        keyHandle,
        const_cast<PUCHAR>(input), static_cast<ULONG>(inputSize),
        &authInfo,
        nullptr, 0,
        output, static_cast<ULONG>(inputSize),
        &resultSize, 0);

    BCryptDestroyKey(keyHandle);
    BCryptCloseAlgorithmProvider(alg, 0);
    return status == 0 && resultSize == inputSize;
}

#else

bool DeriveKeyPbkdf2(const uint8_t* password, size_t passwordSize,
                     const uint8_t* salt, size_t saltSize,
                     uint32_t iterations, uint8_t* keyOut) {
    return PKCS5_PBKDF2_HMAC(
               reinterpret_cast<const char*>(password), static_cast<int>(passwordSize),
               salt, static_cast<int>(saltSize), static_cast<int>(iterations),
               EVP_sha256(), static_cast<int>(kKeySize), keyOut) == 1;
}

bool AesGcmEncrypt(const uint8_t* key, const uint8_t* iv,
                   const uint8_t* aad, size_t aadSize,
                   const uint8_t* input, size_t inputSize,
                   uint8_t* output, uint8_t* tag) {
    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx) return false;

    int ok = EVP_EncryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, key, iv);
    int outLen = 0;
    int totalLen = 0;

    if (ok == 1 && aadSize > 0) {
        ok = EVP_EncryptUpdate(ctx, nullptr, &outLen, aad, static_cast<int>(aadSize));
    }
    if (ok == 1) {
        ok = EVP_EncryptUpdate(ctx, output, &outLen, input, static_cast<int>(inputSize));
        totalLen = outLen;
    }
    if (ok == 1) {
        ok = EVP_EncryptFinal_ex(ctx, output + totalLen, &outLen);
        totalLen += outLen;
    }
    if (ok == 1) {
        ok = EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, static_cast<int>(kTagSize), tag);
    }

    EVP_CIPHER_CTX_free(ctx);
    return ok == 1 && static_cast<size_t>(totalLen) == inputSize;
}

bool AesGcmDecrypt(const uint8_t* key, const uint8_t* iv,
                   const uint8_t* aad, size_t aadSize,
                   const uint8_t* input, size_t inputSize,
                   const uint8_t* tag, uint8_t* output) {
    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx) return false;

    int ok = EVP_DecryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, key, iv);
    int outLen = 0;
    int totalLen = 0;

    if (ok == 1 && aadSize > 0) {
        ok = EVP_DecryptUpdate(ctx, nullptr, &outLen, aad, static_cast<int>(aadSize));
    }
    if (ok == 1) {
        ok = EVP_DecryptUpdate(ctx, output, &outLen, input, static_cast<int>(inputSize));
        totalLen = outLen;
    }
    if (ok == 1) {
        ok = EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, static_cast<int>(kTagSize),
                                 const_cast<uint8_t*>(tag));
    }
    if (ok == 1) {
        ok = EVP_DecryptFinal_ex(ctx, output + totalLen, &outLen);
        totalLen += outLen;
    }

    EVP_CIPHER_CTX_free(ctx);
    return ok == 1 && static_cast<size_t>(totalLen) == inputSize;
}

#endif

bool Utf8FromWideStorage(const std::vector<uint8_t>& bytes, std::string& out) {
    if (bytes.size() < 2) return false;

    const auto b0 = static_cast<unsigned char>(bytes[0]);
    const auto b1 = static_cast<unsigned char>(bytes[1]);
    if (b0 == 0xFF && b1 == 0xFE) {
        const size_t wcharBytes = bytes.size() - 2;
        if (wcharBytes % 2 != 0) return false;
        const size_t wcharCount = wcharBytes / 2;
#ifdef _WIN32
        const int utf8Len = WideCharToMultiByte(
            CP_UTF8, 0, reinterpret_cast<const wchar_t*>(bytes.data() + 2),
            static_cast<int>(wcharCount), nullptr, 0, nullptr, nullptr);
        if (utf8Len <= 0) return false;
        out.resize(static_cast<size_t>(utf8Len));
        WideCharToMultiByte(CP_UTF8, 0, reinterpret_cast<const wchar_t*>(bytes.data() + 2),
                            static_cast<int>(wcharCount), &out[0], utf8Len, nullptr, nullptr);
        return true;
#else
        size_t i = 0;
        while (i < wcharCount) {
            uint16_t unit = static_cast<uint16_t>(bytes[2 + i * 2]) |
                            (static_cast<uint16_t>(bytes[3 + i * 2]) << 8);
            uint32_t cp = unit;
            if (unit >= 0xD800 && unit <= 0xDBFF && i + 1 < wcharCount) {
                const uint16_t low = static_cast<uint16_t>(bytes[2 + (i + 1) * 2]) |
                                     (static_cast<uint16_t>(bytes[3 + (i + 1) * 2]) << 8);
                if (low >= 0xDC00 && low <= 0xDFFF) {
                    cp = 0x10000 + ((unit - 0xD800) << 10) + (low - 0xDC00);
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
            ++i;
        }
        return true;
#endif
    }

    out.assign(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    return true;
}

EncryptedHeader ParseHeader(const std::vector<uint8_t>& data) {
    EncryptedHeader header;
    if (data.size() < kMagicSize + 1) return header;

    if (IsEncryptedFileV3(data)) {
        header.version = 3;
        header.headerSize = 1;
        return header;
    }

    if (std::memcmp(data.data(), kMagicV2, kMagicSize) == 0) {
        if (data.size() < kMagicSize + 1 + 1 + 4 + kSaltSizeV2 + kIvSize + kTagSize) {
            return header;
        }
        header.version = 2;
        header.iterations = static_cast<uint32_t>(data[kMagicSize + 2]) |
                            (static_cast<uint32_t>(data[kMagicSize + 3]) << 8) |
                            (static_cast<uint32_t>(data[kMagicSize + 4]) << 16) |
                            (static_cast<uint32_t>(data[kMagicSize + 5]) << 24);
        header.saltSize = kSaltSizeV2;
        header.headerSize = kMagicSize + 1 + 1 + 4 + kSaltSizeV2 + kIvSize;
        return header;
    }

    if (std::memcmp(data.data(), kMagicV1, kMagicSize) == 0) {
        if (data[kMagicSize] == kVersionV1) {
            header.version = 1;
            header.iterations = kPbkdf2IterationsV1;
            header.saltSize = kSaltSizeV1;
            header.headerSize = kMagicSize + 1 + kSaltSizeV1 + kIvSize;
            return header;
        }
        if (data.size() >= kMagicSize + 2 && data[kMagicSize] == 0 && data[kMagicSize + 1] == kVersionV1) {
            header.version = 1;
            header.iterations = kPbkdf2IterationsV1;
            header.saltSize = kSaltSizeV1;
            header.headerSize = kMagicSize + 2 + kSaltSizeV1 + kIvSize;
            return header;
        }
    }

    return header;
}

std::vector<uint8_t> BuildAadV2(uint32_t iterations) {
    std::vector<uint8_t> aad(kMagicSize + 1 + 1 + 4);
    std::memcpy(aad.data(), kMagicV2, kMagicSize);
    aad[kMagicSize] = kVersionV2;
    aad[kMagicSize + 1] = kKdfPbkdf2Sha256;
    aad[kMagicSize + 2] = static_cast<uint8_t>(iterations & 0xFF);
    aad[kMagicSize + 3] = static_cast<uint8_t>((iterations >> 8) & 0xFF);
    aad[kMagicSize + 4] = static_cast<uint8_t>((iterations >> 16) & 0xFF);
    aad[kMagicSize + 5] = static_cast<uint8_t>((iterations >> 24) & 0xFF);
    return aad;
}

}

bool IsEncryptedFile(const std::vector<uint8_t>& data) {
    if (IsEncryptedFileV3(data)) return true;
    const EncryptedHeader header = ParseHeader(data);
    if (header.headerSize == 0) return false;
    return data.size() >= header.headerSize + kTagSize;
}

bool GetEncryptedFileInfo(const std::vector<std::uint8_t>& data, EncryptedFileInfo& info) {
    if (GetEncryptedFileInfoV3(data, info)) return true;

    EncryptedHeader header = ParseHeader(data);
    if (header.headerSize == 0) return false;
    info.version = header.version;
    info.requires_keyfile = false;
    info.paranoid_kdf = false;
    return true;
}

bool ValidateEncryptionPassword(const std::string& passwordUtf8, bool paranoid_kdf,
                                  std::string& error) {
    if (passwordUtf8.empty()) {
        error = "Password cannot be empty.";
        return false;
    }
    const std::size_t minLen =
        paranoid_kdf ? kMinPasswordLengthParanoid : kMinPasswordLengthStandard;
    if (passwordUtf8.size() < minLen) {
        error = paranoid_kdf
                    ? "High-security encryption requires a password of at least 20 characters."
                    : "Password must be at least 12 characters.";
        return false;
    }

    bool hasLower = false;
    bool hasUpper = false;
    bool hasDigit = false;
    bool hasSymbol = false;
    for (unsigned char ch : passwordUtf8) {
        if (ch >= 'a' && ch <= 'z') hasLower = true;
        else if (ch >= 'A' && ch <= 'Z') hasUpper = true;
        else if (ch >= '0' && ch <= '9') hasDigit = true;
        else hasSymbol = true;
    }

    int classes = 0;
    if (hasLower) ++classes;
    if (hasUpper) ++classes;
    if (hasDigit) ++classes;
    if (hasSymbol) ++classes;

    const int requiredClasses = paranoid_kdf ? 3 : 2;
    if (classes < requiredClasses) {
        error = paranoid_kdf
                    ? "High-security passwords must mix at least three character classes."
                    : "Password must mix at least two character classes.";
        return false;
    }
    return true;
}

bool EncryptText(const std::string& plaintextUtf8, const std::string& passwordUtf8,
                 std::vector<std::uint8_t>& output, std::string& error,
                 const EncryptionOptions& options) {
    return EncryptTextV3(plaintextUtf8, passwordUtf8, output, error, options);
}

bool DecryptText(const std::vector<uint8_t>& data, const std::string& passwordUtf8,
                 std::string& plaintextUtf8, std::string& error,
                 const EncryptionOptions& options) {
    if (!IsEncryptedFile(data)) {
        error = "File is not an UmbraNote encrypted note.";
        return false;
    }

    if (IsEncryptedFileV3(data)) {
        return DecryptTextV3(data, passwordUtf8, plaintextUtf8, error, options);
    }

    if (passwordUtf8.empty()) {
        error = "Password cannot be empty.";
        return false;
    }

    const EncryptedHeader header = ParseHeader(data);
    const size_t cipherSize = data.size() - header.headerSize - kTagSize;
    if (header.headerSize == 0 || cipherSize == 0) {
        error = "Encrypted file is corrupt.";
        return false;
    }

    const uint8_t* salt = data.data() + header.headerSize - header.saltSize - kIvSize;
    const uint8_t* iv = salt + header.saltSize;
    const uint8_t* cipher = iv + kIvSize;
    const uint8_t* tag = cipher + cipherSize;

    const std::vector<uint8_t> passwordBytes(passwordUtf8.begin(), passwordUtf8.end());
    uint8_t key[kKeySize]{};
    if (!DeriveKeyPbkdf2(passwordBytes.data(), passwordBytes.size(),
                         salt, header.saltSize, header.iterations, key)) {
        zeronote::SecureClear(key, sizeof(key));
        error = "Failed to derive decryption key.";
        return false;
    }

    std::vector<uint8_t> plain(cipherSize);
    bool ok = false;
    if (header.version == 2) {
        const std::vector<uint8_t> aad = BuildAadV2(header.iterations);
        ok = AesGcmDecrypt(key, iv, aad.data(), aad.size(),
                           cipher, cipherSize, tag, plain.data());
    } else {
        ok = AesGcmDecrypt(key, iv, nullptr, 0, cipher, cipherSize, tag, plain.data());
    }
    zeronote::SecureClear(key, sizeof(key));

    if (!ok) {
        error = "Incorrect password or corrupted file.";
        return false;
    }

    if (header.version == 2) {
        plaintextUtf8.assign(reinterpret_cast<const char*>(plain.data()), plain.size());
        return true;
    }

    if (!Utf8FromWideStorage(plain, plaintextUtf8)) {
        error = "Decrypted data is invalid.";
        return false;
    }
    return true;
}

bool EncryptLegacyV2ForTest(const std::string& plaintextUtf8, const std::string& passwordUtf8,
                            const std::uint8_t* salt, const std::uint8_t* iv,
                            std::vector<std::uint8_t>& output) {
    if (!salt || !iv || passwordUtf8.empty()) return false;

    const std::vector<uint8_t> passwordBytes(passwordUtf8.begin(), passwordUtf8.end());
    uint8_t key[kKeySize]{};
    if (!DeriveKeyPbkdf2(passwordBytes.data(), passwordBytes.size(),
                         salt, kSaltSizeV2, kPbkdf2IterationsV2, key)) {
        zeronote::SecureClear(key, sizeof(key));
        return false;
    }

    std::vector<uint8_t> cipher(plaintextUtf8.size());
    uint8_t tag[kTagSize]{};
    const std::vector<uint8_t> aad = BuildAadV2(kPbkdf2IterationsV2);
    const bool ok = AesGcmEncrypt(key, iv, aad.data(), aad.size(),
                                  reinterpret_cast<const uint8_t*>(plaintextUtf8.data()),
                                  plaintextUtf8.size(), cipher.data(), tag);
    zeronote::SecureClear(key, sizeof(key));
    if (!ok) return false;

    output.resize(kMagicSize + 1 + 1 + 4 + kSaltSizeV2 + kIvSize + cipher.size() + kTagSize);
    std::memcpy(output.data(), kMagicV2, kMagicSize);
    output[kMagicSize] = kVersionV2;
    output[kMagicSize + 1] = kKdfPbkdf2Sha256;
    output[kMagicSize + 2] = static_cast<uint8_t>(kPbkdf2IterationsV2 & 0xFF);
    output[kMagicSize + 3] = static_cast<uint8_t>((kPbkdf2IterationsV2 >> 8) & 0xFF);
    output[kMagicSize + 4] = static_cast<uint8_t>((kPbkdf2IterationsV2 >> 16) & 0xFF);
    output[kMagicSize + 5] = static_cast<uint8_t>((kPbkdf2IterationsV2 >> 24) & 0xFF);
    std::memcpy(output.data() + kMagicSize + 6, salt, kSaltSizeV2);
    std::memcpy(output.data() + kMagicSize + 6 + kSaltSizeV2, iv, kIvSize);
    std::memcpy(output.data() + kMagicSize + 6 + kSaltSizeV2 + kIvSize,
                cipher.data(), cipher.size());
    std::memcpy(output.data() + kMagicSize + 6 + kSaltSizeV2 + kIvSize + cipher.size(),
                tag, kTagSize);
    return true;
}

bool EncryptLegacyV1ForTest(const std::string& plaintextUtf8, const std::string& passwordUtf8,
                            const std::uint8_t* salt, const std::uint8_t* iv,
                            std::vector<std::uint8_t>& output) {
    if (!salt || !iv || passwordUtf8.empty()) return false;

    const std::vector<uint8_t> passwordBytes(passwordUtf8.begin(), passwordUtf8.end());
    uint8_t key[kKeySize]{};
    if (!DeriveKeyPbkdf2(passwordBytes.data(), passwordBytes.size(),
                         salt, kSaltSizeV1, kPbkdf2IterationsV1, key)) {
        zeronote::SecureClear(key, sizeof(key));
        return false;
    }

    std::vector<uint8_t> cipher(plaintextUtf8.size());
    uint8_t tag[kTagSize]{};
    const bool ok = AesGcmEncrypt(key, iv, nullptr, 0,
                                  reinterpret_cast<const uint8_t*>(plaintextUtf8.data()),
                                  plaintextUtf8.size(), cipher.data(), tag);
    zeronote::SecureClear(key, sizeof(key));
    if (!ok) return false;

    output.resize(kMagicSize + 1 + kSaltSizeV1 + kIvSize + cipher.size() + kTagSize);
    std::memcpy(output.data(), kMagicV1, kMagicSize);
    output[kMagicSize] = kVersionV1;
    std::memcpy(output.data() + kMagicSize + 1, salt, kSaltSizeV1);
    std::memcpy(output.data() + kMagicSize + 1 + kSaltSizeV1, iv, kIvSize);
    std::memcpy(output.data() + kMagicSize + 1 + kSaltSizeV1 + kIvSize,
                cipher.data(), cipher.size());
    std::memcpy(output.data() + kMagicSize + 1 + kSaltSizeV1 + kIvSize + cipher.size(),
                tag, kTagSize);
    return true;
}

}