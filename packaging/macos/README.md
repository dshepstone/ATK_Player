# macOS Packaging

**Status: first build.** `build-dmg.sh` produces `ATKPlayer.app` inside a `.dmg`
for one architecture at a time, and `.github/workflows/macos.yml` builds, tests
and packages it on GitHub's Apple silicon and Intel runners. It has not yet been
tried by hand on a Mac outside CI.

## Building

Prerequisites, as for Windows (see [docs/BUILDING.md](../../docs/BUILDING.md)):
Xcode command line tools, CMake 3.25+, Ninja, Qt 6.9 for macOS with Qt
Multimedia, and vcpkg cloned at the commit `vcpkg.json` pins. Homebrew is only
used for the build tools (`brew install cmake ninja nasm pkgconf`).

```bash
export QT_ROOT=~/Qt/6.9.3/macos
export VCPKG_ROOT=~/dev/vcpkg
packaging/macos/build-dmg.sh --arch arm64 --smoke-test   # or --arch x86_64
```

The result is `out/macos/ATKPlayer-<version>-macOS-<arch>.dmg` and a `.sha256`
beside it.

## How the bundle is put together

1. The `macos-<arch>-release` preset builds FFmpeg through vcpkg with the overlay
   triplets in [cmake/triplets](../../cmake/triplets): **dynamic** libraries
   (FFmpeg is LGPL, exactly as on Windows) built for **macOS 12**, the same
   minimum Qt 6.9 and `CMAKE_OSX_DEPLOYMENT_TARGET` use.
2. `cmake --install` lays out `ATKPlayer.app` (Info.plist from
   `src/app/Info.plist.in`, the `.icns`, the licence texts under
   `Contents/Resources/licenses`) and runs `macdeployqt` through
   `qt_generate_deploy_app_script()`, which copies the Qt frameworks, plugins
   and FFmpeg dylibs into the bundle.
3. `build-dmg.sh` then
   - removes Qt's FFmpeg media plugin and the FFmpeg copy it brings: ATK Player
     decodes with its own FFmpeg and uses Qt Multimedia only for audio output
     (the Windows package excludes the same plugin);
   - replaces any FFmpeg dylib with the exact one this build linked against;
   - copies in anything still missing, rewrites references to `@rpath/...`,
     and deletes RPATHs that point at the build machine;
   - signs (see below), then fails if any binary lacks the target architecture,
     any non-system dependency does not resolve inside the bundle, or the
     signature does not verify;
   - with `--smoke-test`, launches the app for ten seconds with
     `DYLD_PRINT_LIBRARIES` and fails if it exits or loads any library from
     outside the bundle or the OS;
   - wraps the app, an `/Applications` link and `Read Me First.txt` in a
     compressed `.dmg`.

## Architectures

One `.dmg` per architecture for now: `arm64` (Apple silicon) and `x86_64`
(Intel). Qt's macOS binaries are already universal; FFmpeg is the part that is
built per architecture, because its assembly and `configure` do not build
both slices in one pass. A universal download means building both, then
`lipo -create` each FFmpeg dylib and the `ATKPlayer` executable into one
bundle before signing. That is a follow-up once both single-architecture
builds are known to work on real machines.

## Signing and notarisation — not done yet

Like the Windows MSI, the macOS build is not yet signed with a publisher
certificate. Apple silicon will not run unsigned code at all, so the script
signs **ad hoc** by default. Gatekeeper still blocks an ad-hoc app downloaded
from the internet until the user chooses Open from the Finder context menu;
`READ_ME_FIRST.txt` explains this inside the disk image.

For a proper release:

1. Join the Apple Developer Program and create a **Developer ID Application**
   certificate.
2. Build with `ATK_CODESIGN_IDENTITY="Developer ID Application: <name> (<team>)"`.
   The script then signs inside out with the hardened runtime and a secure
   timestamp.
3. Notarise and staple the disk image:

   ```bash
   xcrun notarytool submit ATKPlayer-<version>-macOS-arm64.dmg \
       --keychain-profile atk-notary --wait
   xcrun stapler staple ATKPlayer-<version>-macOS-arm64.dmg
   ```

4. Move steps 2 and 3 into a release workflow with the certificate and an App
   Store Connect API key as repository secrets.

The hardened runtime may need entitlements once it is on (none are needed for
the ad-hoc build). The local API listens on localhost only and needs none;
check audio output and pen-tablet input on a signed build before release.

## Contents

- `ATKPlayer.app`: `Info.plist` declaring the `.atkproj` document type (owned)
  and movies and images (offered in Open With, never claimed as default),
  minimum macOS 12
- Qt frameworks and plugins, dynamically linked
- FFmpeg dylibs (LGPL build) with install names rewritten to `@rpath`
- Licence texts and `THIRD_PARTY_NOTICES.txt` (this folder's, not the Windows
  one) in `Contents/Resources/licenses`

## Before a public release: dependency source materials

The Windows package ships the corresponding source and build records for Qt,
FFmpeg and zlib under `licenses/dependencies`
(`packaging/windows/prepare-dependency-materials.py`). The macOS bundle does not
yet; its notice points to the upstream source and the pinned vcpkg recipe
instead. Extend that script for the macOS triplets and install its output into
the bundle before distributing the `.dmg` publicly.
