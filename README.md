# UmbraNote

A lightweight, cross-platform text editor written in modern C++. UmbraNote feels familiar like Windows Notepad, with optional encrypted notes, PDF export, and a clean native UI on Windows and Linux.

![CI](https://github.com/YOUR_USERNAME/UmbraNote/actions/workflows/ci.yml/badge.svg)
![License](https://img.shields.io/badge/license-MIT-blue.svg)
![Platform](https://img.shields.io/badge/platform-Windows%20%7C%20Linux-lightgrey.svg)
![C++](https://img.shields.io/badge/C%2B%2B-17-blue.svg)

## Highlights

- **Familiar editing** — New, Open, Save, Find/Replace, Go To, Word Wrap, Font picker, and status bar line/column display
- **UTF-8 by default** — Plain `.txt` files are saved as UTF-8 without BOM
- **Encrypted notes** — High-security `.zro` files using Argon2id + XChaCha20-Poly1305, optional keyfile, and legacy v1/v2 support
- **PDF export** — Export the current document to PDF from the File menu
- **Cross-platform core** — Shared `core/` library with native Win32 and GTK4 frontends
- **libsodium** — Memory-hard Argon2id and modern AEAD for v3 encryption (bundled at build time if missing)

## Screenshots

> Add screenshots after your first GitHub release:
>
> `docs/screenshots/windows-editor.png`
> `docs/screenshots/linux-editor.png`

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

| Platform | Toolchain | Libraries |
|----------|-----------|-----------|
| Windows 10+ | Visual Studio 2022 Build Tools or VS 2022 with C++ workload | CMake 3.16+, Git (first build fetches libsodium if needed) |
| Linux | GCC or Clang with C++17 | CMake 3.16+, OpenSSL (`libssl-dev`), GTK4 (`libgtk-4-dev`), libsodium (`libsodium-dev`) |

## Build

### Windows (recommended)

```bat
build.bat
```

Equivalent script:

```bat
scripts\build-windows.bat
```

Output: `UmbraNote.exe` in the repository root and `releases/windows/UmbraNote.exe`.

### Sign a Windows release (PGP)

After building, sign release artifacts in the same detached ASCII-armored format used by YellowSphere:

```bat
scripts\sign-release.bat
```

Create the UmbraNote release key once (private key stored in gitignored `.gnupg-release/`):

```bat
scripts\init-release-key.bat
```

Then sign:

```bat
scripts\sign-release.bat
```

This writes `releases/windows/UmbraNote.exe.asc`, `docs/SHA256SUMS`, `docs/SHA256SUMS.asc`, and `docs/UmbraNote_Release_Signing_2026_pubkey.asc`.

Verify:

```bash
gpg --import docs/UmbraNote_Release_Signing_2026_pubkey.asc
gpg --verify docs/SHA256SUMS.asc docs/SHA256SUMS
gpg --verify releases/windows/UmbraNote.exe.asc releases/windows/UmbraNote.exe
```

See [docs/RELEASE_SIGNING.md](docs/RELEASE_SIGNING.md) for key storage layout and fingerprint.

### Linux (recommended)

```bash
./build.sh
```

Equivalent script:

```bash
./scripts/build-linux.sh
```

Output: `UmbraNote` in the repository root.

### Manual CMake

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

Windows binary path:

```
build/platform/win32/UmbraNote.exe
```

Linux binary path:

```
build/platform/linux/UmbraNote
```

## Keyboard shortcuts

| Action | Shortcut |
|--------|----------|
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

```
UmbraNote/
├── .github/
│   └── workflows/
│       └── ci.yml              # GitHub Actions build matrix
├── assets/                     # Local/generated assets (gitignored)
├── core/
│   ├── include/zeronote/       # Public core headers
│   ├── crypto.cpp              # AES-256-GCM + PBKDF2
│   ├── pdf_export.cpp          # PDF writer
│   ├── text_codec.cpp          # UTF-8 / UTF-16 file I/O
│   └── CMakeLists.txt
├── platform/
│   ├── win32/                  # Windows Win32 application
│   │   ├── main.cpp
│   │   ├── notepad.cpp
│   │   ├── resource.rc
│   │   └── app.manifest
│   └── linux/                  # Linux GTK4 application
│       └── main.cpp
├── docs/
│   └── FILE_FORMATS.md         # .txt, .zro, and PDF details
├── scripts/
│   ├── build-windows.bat
│   └── build-linux.sh
├── build.bat                   # Windows wrapper
├── build.sh                    # Linux wrapper
├── CMakeLists.txt
├── CONTRIBUTING.md
├── LICENSE
└── README.md
```

## Architecture

UmbraNote is split into a portable core and thin platform shells:

```
┌─────────────────────────────────────────┐
│           platform/win32 or linux       │
│   menus, dialogs, editor widget, shell  │
└──────────────────┬──────────────────────┘
                   │
┌──────────────────▼──────────────────────┐
│                  core                   │
│  text_codec  crypto  pdf_export         │
└─────────────────────────────────────────┘
```

- **Windows** uses BCrypt for cryptography.
- **Linux** uses OpenSSL for the same file format and API surface.
- UI code converts to UTF-8 at the boundary so file logic stays portable.

See [docs/FILE_FORMATS.md](docs/FILE_FORMATS.md) for on-disk layouts and [docs/SECURITY.md](docs/SECURITY.md) for the v3 threat model.

## File types

| Extension | Purpose |
|-----------|---------|
| `.txt` | Plain UTF-8 text |
| `.zro` | Password-encrypted UmbraNote document |
| `.pdf` | Exported PDF output |

## Roadmap

- [ ] Linux encrypted open/save dialogs
- [ ] macOS port
- [ ] Dark mode
- [ ] Recent files list
- [ ] Autosave and session restore

## Contributing

Contributions are welcome. Read [CONTRIBUTING.md](CONTRIBUTING.md) before opening a pull request.

## License

This project is licensed under the MIT License. See [LICENSE](LICENSE).
