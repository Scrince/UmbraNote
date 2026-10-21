# Windows installer

Installs the Windows editor. The MSVC build requires the Microsoft Visual C++ 2015–2022 Redistributable (x64), if it is not already present.

Installer: `release/windows/UmbraNote-Setup-1.2.0.exe`

SHA-256: `39a044b36a8b5020baff02d2db36bd16569be21f27b7a58b7fc27f4d8c4b7892`

Detached OpenPGP signature: `release/windows/UmbraNote-Setup-1.2.0.exe.asc`

This is a detached OpenPGP release signature, not a Windows Authenticode signature.

Public key: `docs/UmbraNote_Release_Signing_2026_pubkey.asc`
Fingerprint: `024DCA14BAE2A45CA3D3C182208A7EEAFE2EB289`

Compare the fingerprint above before importing the key. From the project root:

```powershell
gpg --show-keys docs/UmbraNote_Release_Signing_2026_pubkey.asc
gpg --import docs/UmbraNote_Release_Signing_2026_pubkey.asc
gpg --verify release/windows/UmbraNote-Setup-1.2.0.exe.asc release/windows/UmbraNote-Setup-1.2.0.exe
gpg --verify release/windows/SHA256SUMS.asc release/windows/SHA256SUMS
```
