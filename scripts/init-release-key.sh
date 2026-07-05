#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

GPG_BIN="${GPG_BIN:-gpg}"
GNUPG_HOME="${GNUPGHOME:-$ROOT/.gnupg-release}"
PUBKEY_PATH="docs/UmbraNote_Release_Signing_2026_pubkey.asc"

if [[ -d "$GNUPG_HOME" ]] && find "$GNUPG_HOME/private-keys-v1.d" -name '*.key' 2>/dev/null | grep -q .; then
    echo "Release keyring already exists at: $GNUPG_HOME" >&2
    echo "Refusing to overwrite. Delete the directory manually to regenerate." >&2
    exit 1
fi

mkdir -p "$GNUPG_HOME"
chmod 700 "$GNUPG_HOME"
export GNUPGHOME="$GNUPG_HOME"

BATCH_FILE="$(mktemp)"
cat > "$BATCH_FILE" <<'EOF'
%no-protection
Key-Type: RSA
Key-Length: 4096
Name-Real: UmbraNote Release Signing (2026)
Name-Email: release@umbranote.local
Expire-Date: 2y
%commit
EOF

"$GPG_BIN" --batch --generate-key "$BATCH_FILE"
rm -f "$BATCH_FILE"

KEY_ID="$("$GPG_BIN" --list-secret-keys --keyid-format long | awk '/^sec/ {print $2}' | cut -d/ -f2 | head -1)"
if [[ -z "$KEY_ID" ]]; then
    echo "Failed to read new release signing key ID." >&2
    exit 1
fi

mkdir -p docs
"$GPG_BIN" --batch --yes --export --armor "$KEY_ID" > "$PUBKEY_PATH"

echo
echo "UmbraNote release signing key created."
echo "Private keyring: $GNUPG_HOME"
echo "Public key:      $PUBKEY_PATH"
echo "Key ID:          $KEY_ID"
echo
"$GPG_BIN" --show-keys "$PUBKEY_PATH"
echo
echo "This directory is gitignored and must remain offline:"
echo "  $GNUPG_HOME"