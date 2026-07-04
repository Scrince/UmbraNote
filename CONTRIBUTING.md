# Contributing

Thanks for your interest in UmbraNote.

## Development setup

1. Clone the repository.
2. Install the platform requirements listed in [README.md](README.md).
3. Build with `build.bat` (Windows) or `./build.sh` (Linux).

## Project layout

- `core/` — portable C++ library (text I/O, encryption, PDF export)
- `platform/win32/` — Windows Win32 UI
- `platform/linux/` — Linux GTK4 UI
- `scripts/` — build helpers
- `docs/` — supplementary documentation

## Guidelines

- Keep platform-specific code inside `platform/`.
- Put shared logic in `core/` using UTF-8 `std::string` internally.
- Match the existing code style: C++17, minimal comments, focused diffs.
- Test on Windows and Linux when touching `core/` or CMake files.

## Pull requests

1. Create a feature branch from `main`.
2. Keep changes scoped to one concern.
3. Confirm the project builds on your target platform before opening a PR.