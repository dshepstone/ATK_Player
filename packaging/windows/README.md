# Windows Packaging

**Status: implemented — awaiting RC1 installer human acceptance.**

ATK Player uses WiX Toolset **4.0.6** to produce a per-machine Windows x64 MSI.
The permanent installer UpgradeCode is
`{6E41AAE8-13C4-4D46-AB5B-7F04E92E9B76}`. Never change it for ordinary ATK
Player upgrades.

## Requirements and build

- Windows 11 x64 and Visual Studio 2022 C++ tools
- CMake, Ninja, Qt 6.9.3 with `qtmultimedia`
- The vcpkg baseline pinned by `vcpkg.json`
- `QT_ROOT`, `VCPKG_ROOT`, and optionally `ATK_VCPKG_INSTALLED_DIR`
- Internet access on the first packaging run if WiX is absent

From an x64 MSVC developer environment:

```powershell
.\packaging\windows\build-installer.ps1
```

The script pins WiX 4.0.6. If WiX is absent it bootstraps .NET SDK 8.0.419 and
WiX into ignored `build/package/windows/tools`, without changing global tools.
It configures `build/windows-package` with `ATK_VERSION_SUFFIX=rc1`, builds,
tests, installs to `build/package/windows/stage`, smoke-tests with a clean PATH,
builds/verifies the MSI, and writes SHA-256.

RC output is
`build/package/windows/ATK-Player-0.2.0-rc1-Windows-x64.msi`. After acceptance,
the same script supports `-VersionSuffix ""` for final 0.2.0.

## Installed product

- Per-machine/UAC install at `C:\Program Files\ATK Player`
- Start Menu `ATK Player\ATK Player`; no desktop shortcut
- `.atkproj` → `ATK Player Project`; no media associations
- Publisher: David Shepstone
- MSI ProductVersion `0.2.0`; RC identity remains in artifact name, executable
  version string and Installed Apps comments
- Permanent UpgradeCode shown above; major upgrades remove older products,
  same-version upgrades support RC→final, and numeric downgrades are blocked
- Uninstall removes installer-owned files/registry/shortcut, never user projects,
  media, exports or preferences

Bare `.atkproj` paths were already supported, so association command handling
needs no parallel startup implementation.

## Runtime, licensing, signing

`cmake --install` creates the stage. Qt's generated deploy script selects Qt
DLLs/plugins. CMake installs only avcodec, avformat, avutil, swresample and
swscale FFmpeg runtime DLLs. ffmpeg.exe, ffprobe.exe, tests, symbols and SDK
files are prohibited.

The Microsoft Visual C++ 2015-2022 x64 Redistributable is a separately supported
prerequisite; the MSI does not copy arbitrary Visual Studio files.

Installed `licenses` contains ATK Player MIT, Qt LGPL v3, the pinned FFmpeg
license/component notices, and source/version notices.

RC1 is unsigned unless a trusted certificate is supplied. SmartScreen may warn;
verify SHA-256. The build script accepts `-CertificateThumbprint` or `-PfxPath`
plus `-PfxPassword`, signing EXE and MSI with SHA-256/RFC3161 timestamping.
Never commit certificate material.

## Verification, CI, uninstall and upgrades

`verify-installer.ps1` inspects MSI metadata and validates x64 identity,
UpgradeCode, shortcut, association, runtime/license contents, and prohibited
file absence. `.github/workflows/windows-release.yml` supports manual and `v*`
tag builds, uploads MSI/checksum artifacts, and never publishes a release.

Install interactively with `Start-Process "<absolute-msi-path>"`. Uninstall
through Windows Installed Apps or `msiexec.exe /x "{PRODUCT-CODE}"`; verify
installer-created files are gone and user data remains, then reinstall RC1.
When final 0.2.0 exists, install it over RC1 and confirm a major upgrade using
the same UpgradeCode. Do not create a fake public final release for this test.
