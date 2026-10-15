[CmdletBinding()]
param(
    # The final MSI from build-installer.ps1 -VersionSuffix "" (or the
    # "Windows Release Package" workflow artifact). Defaults to the build
    # output for the version in CMakeLists.txt.
    [string]$MsiPath,
    # Shown in INSTALL.txt: the releases this one installs over in place.
    [string]$UpgradesFrom = "0.5.4, 0.5.3, 0.5.2, 0.5.1, 0.5.0, 0.4.x or 0.3.0",
    # Optional GitHub release text with {{ZIP_SHA256}} and {{MSI_SHA256}}
    # placeholders; a filled copy is written beside the ZIP.
    [string]$ReleaseBodyTemplate,
    [string]$OutputDirectory
)

# Packages a release download the way every ATK Player release since 0.2.1
# has been shipped: ATK-Player-v<version>-Windows.zip holding one folder with
# the MSI, its .sha256, INSTALL.txt, LICENSE.txt and THIRD_PARTY_NOTICES.txt.

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$repoRoot = (Resolve-Path (Join-Path $scriptDir "../..")).Path
$projectCmake = Get-Content -LiteralPath (Join-Path $repoRoot "CMakeLists.txt") -Raw
if ($projectCmake -notmatch '(?s)project\(ATKPlayer\s+VERSION\s+(\d+\.\d+\.\d+)\s') {
    throw "Cannot determine the ATKPlayer project version from CMakeLists.txt."
}
$version = $Matches[1]
$msiName = "ATK-Player-$version-Windows-x64.msi"
if (-not $MsiPath) { $MsiPath = Join-Path $repoRoot "build/package/windows/$msiName" }
if (-not (Test-Path -LiteralPath $MsiPath)) {
    throw "MSI not found: $MsiPath. Build it with .\packaging\windows\build-installer.ps1 -VersionSuffix """" first."
}
if ((Split-Path -Leaf $MsiPath) -ne $msiName) {
    throw "Expected the final MSI $msiName (no rc suffix), got $(Split-Path -Leaf $MsiPath)."
}
if (-not $OutputDirectory) { $OutputDirectory = Join-Path $repoRoot "build/package/release" }

$folderName = "ATK-Player-v$version-Windows"
$zipName = "$folderName.zip"
$staging = Join-Path $OutputDirectory $folderName
$zipPath = Join-Path $OutputDirectory $zipName
if (Test-Path -LiteralPath $staging) { Remove-Item -LiteralPath $staging -Recurse -Force }
if (Test-Path -LiteralPath $zipPath) { Remove-Item -LiteralPath $zipPath -Force }
New-Item -ItemType Directory -Path $staging -Force | Out-Null

Copy-Item -LiteralPath $MsiPath -Destination (Join-Path $staging $msiName)
$msiHash = (Get-FileHash -LiteralPath (Join-Path $staging $msiName) -Algorithm SHA256).Hash.ToLowerInvariant()
Set-Content -LiteralPath (Join-Path $staging "$msiName.sha256") -Encoding ascii -NoNewline `
    -Value "$msiHash  $msiName`n"
Copy-Item -LiteralPath (Join-Path $repoRoot "LICENSE") -Destination (Join-Path $staging "LICENSE.txt")
Copy-Item -LiteralPath (Join-Path $repoRoot "packaging/windows/THIRD_PARTY_NOTICES.txt") `
    -Destination (Join-Path $staging "THIRD_PARTY_NOTICES.txt")

$title = "ATK Player $version for Windows"
$install = @"
$title
$('=' * $title.Length)

Install
-------
1. Extract all files from the ZIP (right-click > Extract All...).
2. Open the extracted folder and double-click $msiName.
3. Follow the setup wizard.

Launch ATK Player from the Start Menu, or tick "Launch ATK Player" at the
end of setup. Installing over $UpgradesFrom upgrades in place and keeps your
preferences, projects and media.

Windows warning
---------------
This build is not code-signed yet. If Windows shows "Windows protected your
PC", click More info > Run anyway. Or right-click the MSI > Properties >
tick Unblock > OK, then run it.

Requirements
------------
- Windows 11 x64
- Microsoft Visual C++ 2015-2022 x64 Redistributable
  https://aka.ms/vs/17/release/vc_redist.x64.exe

Verify the installer (optional)
-------------------------------
In PowerShell, in this folder:
  Get-FileHash .\$msiName -Algorithm SHA256
Expected:
  $msiHash

Licences: LICENSE.txt (ATK Player, MIT) and THIRD_PARTY_NOTICES.txt.
Source: https://github.com/dshepstone/ATK_Player
"@
# Byte for byte as in earlier releases (LF lines, a final CRLF from Windows'
# Set-Content), whatever line endings this script was checked out with.
Set-Content -LiteralPath (Join-Path $staging "INSTALL.txt") -Encoding ascii -NoNewline `
    -Value (($install -replace "`r`n", "`n").TrimEnd("`n") + "`r`n")

Compress-Archive -Path $staging -DestinationPath $zipPath -CompressionLevel Optimal
$zipHash = (Get-FileHash -LiteralPath $zipPath -Algorithm SHA256).Hash.ToLowerInvariant()

if ($ReleaseBodyTemplate) {
    $body = (Get-Content -LiteralPath $ReleaseBodyTemplate -Raw).Replace("{{ZIP_SHA256}}", $zipHash).Replace("{{MSI_SHA256}}", $msiHash)
    $bodyPath = Join-Path $OutputDirectory "RELEASE_BODY_v$version.md"
    Set-Content -LiteralPath $bodyPath -Encoding utf8 -Value $body
    Write-Host "Release text: $bodyPath"
}

Write-Host "ZIP: $zipPath ($([math]::Round((Get-Item -LiteralPath $zipPath).Length / 1MB)) MB)"
Write-Host "ZIP SHA-256: $zipHash"
Write-Host "MSI SHA-256: $msiHash"
