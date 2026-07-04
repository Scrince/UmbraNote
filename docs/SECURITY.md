# UmbraNote Security Model

## Threat model (v3 high-security)

UmbraNote v3 is designed for users facing **targeted or nation-state-level offline attacks** against encrypted `.zro` files. The design assumes:

| In scope | Out of scope |
|----------|--------------|
| Offline brute-force and GPU/ASIC cracking of stolen `.zro` files | Live malware, keyloggers, or screen capture on a compromised PC |
| Tampering with ciphertext or headers | Physical coercion ("rubber-hose" attacks) |
| Memory scraping while the app is open | Network interception (files are local) |
| Weak or reused passwords | Side-channel attacks on CPU crypto implementations |

No consumer text editor can fully defend against a compromised operating system. v3 focuses on making **stolen ciphertext** as expensive as possible to break and on reducing **forensic exposure** while the app runs.

## v3 cryptographic design

### Algorithms

| Layer | Algorithm | Notes |
|-------|-----------|-------|
| Key derivation | **Argon2id** via libsodium | Memory-hard; resists GPU/ASIC advantage vs PBKDF2 |
| Encryption | **XChaCha20-Poly1305** | 256-bit key, 192-bit nonce; safer random nonce use than AES-GCM |
| Randomness | OS CSPRNG (`randombytes_buf`) | libsodium wrapper |
| Header integrity | AAD over full v3 header | Tampering with KDF params is detected |

### High-security defaults (new saves)

- **Argon2id** with libsodium `OPSLIMIT_SENSITIVE` + `MEMLIMIT_SENSITIVE`
- **Password + keyfile** required (two-factor secret)
- Minimum **20-character** password mixing **3 character classes**
- Keyfile must be **≥ 32 bytes**
- **No stored session password** — re-authentication required on each save
- Derived keys held in **locked, zeroed** memory buffers where the OS allows

### Legacy formats

| Format | Status |
|--------|--------|
| v1 (`ZNENC1`) | Readable; PBKDF2 100k, 16-byte salt |
| v2 (`ZNENC2`) | Readable; AES-256-GCM, PBKDF2 600k, 32-byte salt, AAD |
| v3 (`ZNENC3`) | **Default for new encrypted saves** |

Re-save old notes to upgrade to v3.

## User responsibilities

1. Use a **long, unique passphrase** (20+ characters for high-security mode).
2. Store the **keyfile separately** from the password (different device or safe).
3. Treat an **unlocked editor** as plaintext — close UmbraNote when stepping away.
4. Assume **live compromise** of the machine defeats any local encryption app.

## Recommendations for extreme threat models

- Use UmbraNote inside a **dedicated, hardened OS** (e.g. air-gapped or live USB).
- Combine with **full-disk encryption** and **secure boot**.
- Rotate passwords periodically using **Save Encrypted As** with a new keyfile.
- Never reuse keyfiles across unrelated notes.

See [FILE_FORMATS.md](FILE_FORMATS.md) for on-disk layout details.