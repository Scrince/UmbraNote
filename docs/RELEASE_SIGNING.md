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

This mirrors YellowSphere's `/.gnupg-release/` layout: a single RSA primary key
with no subkey (one secret file in `private-keys-v1.d/`). Typical contents:

```text
.gnupg-release/
  private-keys-v1.d/   # single secret key file
  pubring.kbx
  trustdb.gpg
```

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
Fingerprint: 024D CA14 BAE2 A45C A3D3  C182 208A 7EEA FE2E B289
Key ID:      208A7EEAFE2EB289
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

## Local code-signing certificate

UmbraNote also includes the public half of a local Windows Authenticode test certificate:

```text
docs/UmbraNote_Local_Code_Signing_2026.cer
```

Certificate identity:

```text
CN=UmbraNote Local Code Signing
Thumbprint: AA9B590FB698EBFD1651F14D2576602F01039FAB
Valid: 2026-07-04 to 2028-07-04
```

The certificate file is safe to publish and can be imported by testers who need to trust local test-signed builds. The private key remains in the local Windows certificate store and must not be committed.

Inspect the certificate:

```bat
certutil -dump docs\UmbraNote_Local_Code_Signing_2026.cer
```
