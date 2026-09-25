# Code Signing and the Windows "Unsafe App" Warning

## Why Windows warns

When a downloaded `.msi` or `.exe` has no Authenticode signature, Windows shows
**"Windows protected your PC"** (Microsoft Defender SmartScreen) and the UAC
prompt says **Publisher: Unknown**. Nothing in ATK Player's code or build causes
this, so no source change can remove it. Windows is asking two questions:

1. **Who published this?** Answered by signing the MSI and `ATKPlayer.exe` with a
   code-signing certificate that chains to a CA Microsoft trusts.
2. **Is this publisher/file known to be safe?** Answered by SmartScreen
   *reputation*, which builds up as signed files are downloaded and run without
   problems.

A signature fixes (1) immediately and replaces "Unknown Publisher" with your
name. (2) takes some download volume. Since 2024, EV certificates no longer skip
this reputation period, so a more expensive certificate doesn't make the
warning go away faster.

`build-installer.ps1` already signs both `ATKPlayer.exe` and the MSI with
SHA-256 and an RFC 3161 timestamp as soon as it's given a certificate. The
only missing piece is the certificate.

## Options, in recommended order

Prices and eligibility rules change, so check each provider's current terms.

### 1. Azure Trusted Signing (recommended)

Microsoft's managed signing service. Certificates chain to Microsoft's own root,
keys live in Microsoft's HSM (no USB token), and it costs about US$10/month on the
Basic tier. Individual developers can use it in some countries, which at the
time of writing include Canada and the USA. It needs identity validation
through Microsoft Entra.

Setup:

1. Create an Azure subscription and a **Trusted Signing account** in the Azure portal.
2. Complete **identity validation** (Individual or Organization). The validated
   name is what Windows shows as the publisher, e.g. "David Shepstone".
3. Create a **certificate profile** (Public Trust).
4. Grant yourself (or a CI service principal) the
   **Trusted Signing Certificate Profile Signer** role.
5. On the build machine install the Windows SDK `signtool` and the
   `Microsoft.Trusted.Signing.Client` NuGet package. It contains
   `bin\x64\Azure.CodeSigning.Dlib.dll`.
6. Create `metadata.json` (keep it out of the repository):

   ```json
   {
     "Endpoint": "https://<region>.codesigning.azure.net",
     "CodeSigningAccountName": "<account-name>",
     "CertificateProfileName": "<profile-name>"
   }
   ```

7. Sign in with `az login` (or set `AZURE_CLIENT_ID` / `AZURE_TENANT_ID` /
   `AZURE_CLIENT_SECRET` for a service principal), then build:

   ```powershell
   .\packaging\windows\build-installer.ps1 -VersionSuffix "" `
       -TrustedSigningDlib "C:\tools\trusted-signing\bin\x64\Azure.CodeSigning.Dlib.dll" `
       -TrustedSigningMetadata "C:\secure\metadata.json"
   ```

   The script uses Microsoft's timestamp server (`http://timestamp.acs.microsoft.com`)
   automatically in this mode.

For GitHub Actions, Microsoft publishes the `azure/trusted-signing-action`.
Store the client ID, tenant ID and secret as repository **secrets**, never in the
workflow file.

### 2. SignPath Foundation (free for open-source projects)

SignPath Foundation signs releases of qualifying OSI-licensed projects for free.
ATK Player's MIT licence qualifies. Artifacts must be built by a public CI
pipeline (GitHub Actions works) and approved per release. The certificate is
issued to **SignPath Foundation**, so that name appears as the publisher, not
yours. Apply at <https://signpath.org>.

### 3. A traditional OV code-signing certificate

Available from CAs such as Sectigo, DigiCert, SSL.com or Certum (Certum's
open-source developer certificate is the cheapest). Since June 2023 private keys
must live on a hardware token or cloud HSM. Once installed, pass its thumbprint:

```powershell
.\packaging\windows\build-installer.ps1 -VersionSuffix "" -CertificateThumbprint "<SHA1 thumbprint>"
```

`-PfxPath`/`-PfxPassword` also work where the CA permits exporting a PFX. Never
commit certificate files, passwords or `metadata.json`.

## After signing: build reputation faster

- **Sign every release with the same identity.** Reputation attaches to the
  certificate/publisher, so later releases inherit it.
- **Submit each release to Microsoft** at
  <https://www.microsoft.com/wdsi/filesubmission> ("Software developer",
  "Incorrectly detected as malware/malicious" → SmartScreen). This often clears
  the warning within a few days.
- **Publish to winget** (`microsoft/winget-pkgs`). Users installing with
  `winget install` skip the browser-download path that triggers most warnings.
- Keep publishing the SHA-256 checksum next to each download.

## Until a signed build exists: what to tell users

Add this to the release notes:

1. Download `ATK-Player-<version>-Windows-x64.msi` from the official GitHub
   Releases page only.
2. Optionally verify it in PowerShell and compare with the published `.sha256`:
   `Get-FileHash .\ATK-Player-<version>-Windows-x64.msi -Algorithm SHA256`
3. If the browser flags the download, choose **Keep** (Edge: "…" → Keep → Show more → Keep anyway).
4. When **Windows protected your PC** appears, click **More info**, then **Run anyway**.
5. Alternatively, right-click the MSI → **Properties** → tick **Unblock** → **OK**,
   then run it. This removes the "downloaded from the internet" mark.

Organisations deploying internally can also sign the MSI with their own
internal CA (trusted by their managed machines through Group Policy). That
removes the warning inside the studio without a public certificate.
