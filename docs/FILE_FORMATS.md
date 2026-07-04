# File Formats

## Plain text (`.txt`)

UmbraNote saves plain text as **UTF-8 without a BOM**. When opening a file, UmbraNote auto-detects:

- UTF-8 (with or without BOM)
- UTF-16 LE / BE

## Encrypted notes (`.zro`)

### Current format (v3) — high security

| Field | Size | Description |
|-------|------|-------------|
| Magic | 6 bytes | `ZNENC3` |
| Version | 1 byte | `3` |
| Flags | 1 byte | bit0=keyfile used, bit1=paranoid KDF |
| KDF ID | 1 byte | `1` = Argon2id |
| Ops limit | 4 bytes | Little-endian libsodium ops limit |
| Mem limit | 4 bytes | Little-endian libsodium mem limit |
| Salt | 32 bytes | Random |
| Nonce | 24 bytes | Random XChaCha20 nonce |
| Ciphertext | variable | libsodium `crypto_aead_xchacha20poly1305_ietf` blob |

The full header is authenticated as **AAD**. Decryption requires the same password and keyfile (if flagged) used at encryption time.

**High-security mode** requires both a 20+ character password and a keyfile of at least 32 bytes.

### Legacy format (v2)

| Field | Size | Description |
|-------|------|-------------|
| Magic | 6 bytes | `ZNENC2` |
| Version | 1 byte | `2` |
| KDF ID | 1 byte | `0` = PBKDF2-SHA256 |
| Iterations | 4 bytes | Little-endian (`600000` default) |
| Salt | 32 bytes | Random |
| IV | 12 bytes | Random |
| Ciphertext | variable | UTF-8 plaintext |
| Auth tag | 16 bytes | GCM tag |

### Legacy format (v1)

Older `.zro` files using `ZNENC1` remain readable. They use a 16-byte salt, 100,000 PBKDF2 iterations, and UTF-16 storage for plaintext.

## PDF export (`.pdf`)

PDF export generates a minimal PDF 1.4 document with Helvetica at 12pt, wrapping lines and paginating automatically. Unicode text is encoded as UTF-16BE hex strings when needed.