# UmbraNote PGP Release Signatures

UmbraNote uses the same release-signing layout as YellowSphere:

| Location | Purpose |
|----------|---------|
| `.gnupg-release/` | **Private** release keyring (gitignored, offline) |
| `docs/UmbraNote_Release_Signing_2026_pubkey.asc` | Public release-signing key |
| `docs/SHA256SUMS` | SHA-256 manifest |
| `docs/SHA256SUMS.asc` | Detached PGP signature for the manifest |
| `releases/windows/UmbraNote.exe` | Windows release binary |
| `releases/windows/UmbraNote.exe.asc` | Detached PGP signature for the binary |

## Private key storage

The secret key never goes in git. It lives only in:

```text
UmbraNote/.gnupg-release/
```

This mirrors YellowSphere's `/.gnupg-release/` layout. The UmbraNote release
keyring uses a **certify-only RSA primary** plus a dedicated **Ed25519 signing
subkey** (and an optional encryption subkey). Typical contents:

```text
.gnupg-release/
  private-keys-v1.d/   # primary + subkey secret material
  pubring.kbx
  trustdb.gpg
```

`scripts/sign-release.bat` signs with the **signing subkey** by default.
Override with `RELEASE_KEY_ID` if needed.

`.gnupg-release/` is listed in `.gitignore`. Back it up securely and separately from the repository.

## Create the UmbraNote release key (one time)

```bat
scripts\init-release-key.bat
```

This generates:

```text
UmbraNote Release Signing (2026) <release@umbranote.local>
```

and exports the public half to `docs/UmbraNote_Release_Signing_2026_pubkey.asc`.

Release signing key:

```text
UmbraNote Release Signing (2026) <release@umbranote.local>
Primary (C):  024D CA14 BAE2 A45C A3D3  C182 208A 7EEA FE2E B289
Signing (S):  6060 D40F 55FF CA75 8BE5  A229 91BA AA3B 96A5 9407
Key ID (S):   91BAAA3B96A59407
```

Inspect the public key before trusting signatures:

```bash
gpg --show-keys docs/UmbraNote_Release_Signing_2026_pubkey.asc
```

## Sign a build

```bat
scripts\build-windows.bat
scripts\sign-release.bat
```

`sign-release.bat` uses `UmbraNote/.gnupg-release/` by default. Override with `GNUPGHOME` if needed.

## Verify signatures

```bash
gpg --import docs/UmbraNote_Release_Signing_2026_pubkey.asc
gpg --verify docs/SHA256SUMS.asc docs/SHA256SUMS
gpg --verify releases/windows/UmbraNote.exe.asc releases/windows/UmbraNote.exe
sha256sum -c docs/SHA256SUMS
```

PGP signatures confirm release integrity against the UmbraNote release key. They do not provide Windows Authenticode publisher trust.