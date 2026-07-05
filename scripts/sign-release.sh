#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

GPG_BIN="${GPG_BIN:-gpg}"
VERSION="${UMBRA_VERSION:-1.2}"
RELEASE_DIR="releases/windows"
EXE_NAME="UmbraNote.exe"
EXE_PATH="${RELEASE_DIR}/${EXE_NAME}"
GNUPG_HOME="${GNUPGHOME:-$ROOT/.gnupg-release}"
PUBKEY_PATH="docs/UmbraNote_Release_Signing_2026_pubkey.asc"

if [[ ! -d "$GNUPG_HOME" ]]; then
    echo "Release signing keyring not found: $GNUPG_HOME" >&2
    echo "Create one with: scripts/init-release-key.bat" >&2
    exit 1
fi
export GNUPGHOME="$GNUPG_HOME"

RELEASE_KEY_ID="${RELEASE_KEY_ID:-}"
if [[ -z "$RELEASE_KEY_ID" ]]; then
    RELEASE_KEY_ID="$("$GPG_BIN" --list-secret-keys --keyid-format long | awk '/^sec/ {print $2}' | cut -d/ -f2 | head -1)"
fi
if [[ -z "$RELEASE_KEY_ID" ]]; then
    echo "No release signing secret key found in $GNUPG_HOME" >&2
    echo "Run scripts/init-release-key.bat first." >&2
    exit 1
fi

if [[ ! -f "$EXE_PATH" ]]; then
    if [[ -f "$ROOT/UmbraNote.exe" ]]; then
        mkdir -p "$RELEASE_DIR"
        cp -f "$ROOT/UmbraNote.exe" "$EXE_PATH"
    else
        echo "Release binary not found: $EXE_PATH" >&2
        echo "Build first with scripts/build-windows.bat" >&2
        exit 1
    fi
fi

mkdir -p docs
"$GPG_BIN" --batch --yes --export --armor "$RELEASE_KEY_ID" > "$PUBKEY_PATH"

sign_file() {
    local target="$1"
    local sig="${target}.asc"
    rm -f "$sig"
    "$GPG_BIN" --batch --yes --local-user "$RELEASE_KEY_ID" \
        --detach-sign --armor --output "$sig" "$target"
    echo "Signed: $sig"
}

sign_file "$EXE_PATH"

hash_file() {
    local target="$1"
    if command -v sha256sum >/dev/null 2>&1; then
        sha256sum --text "$target" | awk '{print $1 "  " $2}'
    else
        shasum -a 256 "$target" | awk '{print $1 "  " $2}'
    fi
}

MANIFEST_PATH="docs/SHA256SUMS"
{
    hash_file "$EXE_PATH"
    hash_file "$EXE_PATH.asc"
    hash_file "$PUBKEY_PATH"
    hash_file README.md
    hash_file docs/SECURITY.md
    hash_file docs/FILE_FORMATS.md
} | sed 's|\\|/|g' > "$MANIFEST_PATH"

sign_file "$MANIFEST_PATH"

FINGERPRINT="$("$GPG_BIN" --with-colons --show-keys "$PUBKEY_PATH" | awk -F: '/^fpr:/ {print $10; exit}')"

echo
echo "UmbraNote v${VERSION} release signing complete."
echo "Private keyring: $GNUPG_HOME"
echo "Public key:      $PUBKEY_PATH"
echo "Key ID:          $RELEASE_KEY_ID"
echo "Fingerprint:     $FINGERPRINT"
echo "Manifest:        $MANIFEST_PATH"
echo "Signature:       ${MANIFEST_PATH}.asc"
echo "Artifact:        $EXE_PATH"
echo "Artifact sig:      ${EXE_PATH}.asc"
echo
echo "Verify with:"
echo "  gpg --import $PUBKEY_PATH"
echo "  gpg --verify ${MANIFEST_PATH}.asc ${MANIFEST_PATH}"
echo "  gpg --verify ${EXE_PATH}.asc ${EXE_PATH}"