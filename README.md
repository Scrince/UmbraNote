# UmbraNote

UmbraNote is a small native text editor written in modern C++. It keeps the familiar shape of a desktop notepad while adding encrypted notes, UTF-aware text loading, PDF export, and a portable core that can be shared across platform frontends.

![CI](https://github.com/YOUR_USERNAME/UmbraNote/actions/workflows/ci.yml/badge.svg)
![License](https://img.shields.io/badge/license-MIT-blue.svg)
![Platform](https://img.shields.io/badge/platform-Windows%20%7C%20Linux-lightgrey.svg)
![C++](https://img.shields.io/badge/C%2B%2B-17-blue.svg)

## Features

- Native editor UI with New, Open, Save, Find, Replace, Go To, Word Wrap, font selection, printing, and line/column status
- Plain text files saved as UTF-8 without a BOM
- Automatic opening of UTF-8 and UTF-16 LE/BE text files
- Encrypted `.zro` notes using the current ZNENC3 format for new saves
- Legacy `.zro` read support for ZNENC1 and ZNENC2 files
- PDF export with line wrapping and pagination
- Shared `core/` library for text encoding, cryptography, secure memory helpers, and PDF writing
- Native Win32 frontend and GTK4-based Linux frontend

## Encryption at a glance

New encrypted notes use ZNENC3:

- Argon2id key derivation through libsodium
- XChaCha20-Poly1305 authenticated encryption
- Random 32-byte salt and 24-byte nonce per file
- Full fixed header authenticated as additional data
- Optional keyfile support, with high-security mode requiring password plus keyfile
- Best-effort locked and zeroed memory for derived keys and KDF inputs

ZNENC1 and ZNENC2 remain readable for compatibility. Re-save old encrypted notes with "Save Encrypted As" to move them to the current format.

No local editor can protect plaintext on a compromised machine. UmbraNote encryption is focused on files at rest, especially stolen or copied `.zro` files. See [docs/SECURITY.md](docs/SECURITY.md) and [docs/FILE_FORMATS.md](docs/FILE_FORMATS.md) for details.

## Quick start

### Windows

```bat
git clone https://github.com/YOUR_USERNAME/UmbraNote.git
cd UmbraNote
build.bat
UmbraNote.exe
```

### Linux

```bash
git clone https://github.com/YOUR_USERNAME/UmbraNote.git
cd UmbraNote
chmod +x build.sh
./build.sh
./UmbraNote
```

## Requirements

| Platform | Required tools | Libraries |
| --- | --- | --- |
| Windows 10+ | Visual Studio 2022 Build Tools or Visual Studio 2022 with the C++ workload, CMake 3.16+, Git | BCrypt from Windows; libsodium is fetched by CMake when not installed |
| Linux | GCC or Clang with C++17, CMake 3.16+, pkg-config | GTK4, OpenSSL, libsodium |

Example Ubuntu dependencies:

```bash
sudo apt update
sudo apt install build-essential cmake pkg-config libgtk-4-dev libssl-dev libsodium-dev
```

## Build

Use the top-level wrappers for normal local builds:

```bat
build.bat
```

```bash
./build.sh
```

The platform scripts are available directly too:

```bat
scripts\build-windows.bat
```

```bash
./scripts/build-linux.sh
```

Build outputs:

| Platform | Main output | Release copy |
| --- | --- | --- |
| Windows | `UmbraNote.exe` | `releases/windows/UmbraNote.exe` |
| Linux | `UmbraNote` | none by default |

Manual CMake build:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

## Tests

Tests are enabled by default through CMake:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
ctest --test-dir build --output-on-failure
```

The current test target covers the crypto layer and fixture compatibility. Catch2 is fetched by CMake for the test build.

## Release signing

Windows release artifacts can be signed with detached PGP signatures:

```bat
scripts\build-windows.bat
scripts\sign-release.bat
```

One-time release key setup:

```bat
scripts\init-release-key.bat
```

Generated release files include:

- `releases/windows/UmbraNote.exe`
- `releases/windows/UmbraNote.exe.asc`
- `docs/SHA256SUMS`
- `docs/SHA256SUMS.asc`
- `docs/UmbraNote_Release_Signing_2026_pubkey.asc`

Verify a release:

```bash
gpg --import docs/UmbraNote_Release_Signing_2026_pubkey.asc
gpg --verify docs/SHA256SUMS.asc docs/SHA256SUMS
gpg --verify releases/windows/UmbraNote.exe.asc releases/windows/UmbraNote.exe
sha256sum -c docs/SHA256SUMS
```

See [docs/RELEASE_SIGNING.md](docs/RELEASE_SIGNING.md) for the key layout, fingerprint, and local Windows code-signing certificate notes.

## Keyboard shortcuts

| Action | Shortcut |
| --- | --- |
| New | `Ctrl+N` |
| Open | `Ctrl+O` |
| Save | `Ctrl+S` |
| Print | `Ctrl+P` |
| Undo | `Ctrl+Z` |
| Cut | `Ctrl+X` |
| Copy | `Ctrl+C` |
| Paste | `Ctrl+V` |
| Find | `Ctrl+F` |
| Find Next | `F3` |
| Replace | `Ctrl+H` |
| Go To | `Ctrl+G` |
| Select All | `Ctrl+A` |

## Repository layout

```text
UmbraNote/
  .github/workflows/      CI configuration
  core/                   Portable editor services
    include/zeronote/     Public core headers
    crypto.cpp            Legacy crypto dispatch and compatibility
    crypto_v3.cpp         ZNENC3 implementation
    pdf_export.cpp        Minimal PDF writer
    secure_memory.cpp     Locked and zeroed memory helpers
    text_codec.cpp        Text file encoding support
  docs/                   Security, file format, and release signing docs
  platform/
    win32/                Native Windows application
    linux/                GTK4 Linux application
  releases/               Local release artifacts
  scripts/                Build and signing helpers
  tests/                  Crypto tests and fixtures
```

## Architecture

UmbraNote separates portable document logic from platform UI code:

```text
platform/win32 or platform/linux
  menus, dialogs, editor widget, file picker, shell integration
                |
                v
core/
  text_codec  crypto  secure_memory  pdf_export
```

The platform layers own native windows, menus, dialogs, and editor widgets. The core owns file encoding, encrypted note formats, secure memory helpers, and PDF generation.

For cryptography, ZNENC3 uses libsodium on every platform. BCrypt on Windows and OpenSSL on Linux are retained for legacy ZNENC1/ZNENC2 compatibility.

## Supported file types

| Extension | Purpose |
| --- | --- |
| `.txt` | Plain text |
| `.zro` | UmbraNote encrypted note |
| `.pdf` | Exported PDF |

## Current platform notes

The Windows frontend is the primary complete UI today. The Linux frontend shares the same core and builds against GTK4, but some encrypted-note UI flows are still catching up with the Windows implementation.

## Contributing

Contributions are welcome. Please read [CONTRIBUTING.md](CONTRIBUTING.md) before opening a pull request.

## License

UmbraNote is released under the MIT License. See [LICENSE](LICENSE).
