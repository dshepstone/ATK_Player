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

## Signing and notarisation

ATK Player is distributed outside the Mac App Store (the website and Gumroad),
so it needs a **Developer ID** signature and Apple **notarisation**, and no App
Sandbox. The build is made to pass both as soon as there is a certificate,
with no code changes; until then the default output is signed **ad hoc**, like
the unsigned Windows MSI.

### What the build already does

- **Signs inside out** (no `--deep` when signing): every Qt plugin, FFmpeg and
  other loose dylib, deepest first; then each Qt framework as a bundle; then the
  app, which alone carries the entitlements.
- **Hardened runtime** (`--options runtime`) and a **secure timestamp**
  (`--timestamp`) whenever `ATK_CODESIGN_IDENTITY` is set. CI also signs its
  ad-hoc build with the hardened runtime (`ATK_HARDENED_RUNTIME=1`), so the
  release signing path, and the app starting under it, are tested on every
  pull request.
- **Entitlements:** [ATKPlayer.entitlements](ATKPlayer.entitlements) is
  deliberately empty. Its comments explain why each candidate is not needed:
  no `disable-library-validation` (every bundled library is signed with the
  app's identity), no JIT or unsigned-memory (FFmpeg and Qt Widgets run
  precompiled code only), and no microphone, camera or Apple Events. That is
  also why `Info.plist` has no usage strings. The localhost API needs no
  network entitlement outside the sandbox.
- **Bundle metadata:** `CFBundleIdentifier` `ca.shepstone.atkplayer` (CMake
  cache variable `ATK_MACOS_BUNDLE_ID`; never change it after the first
  release). `CFBundleShortVersionString` and `CFBundleVersion` come from the
  project version in `CMakeLists.txt`, with `LSMinimumSystemVersion` 12.0 and
  `NSHighResolutionCapable`.
- **[verify-app.sh](verify-app.sh)** fails if any Mach-O file is unsigned,
  links or has an RPATH into `/opt/homebrew`, `/usr/local` or any other
  absolute path outside the bundle and the OS, lacks the target architecture,
  fails `codesign --verify --deep --strict`, or (when expected) lacks the
  hardened-runtime flag or has entitlements other than the checked-in file.
  `build-dmg.sh` runs it on the staged app, and CI runs it again on the app
  inside the finished `.dmg`.
- **Signs the `.dmg`** as well, and with `ATK_NOTARY_PROFILE` set it
  **notarises and staples** it (`notarytool submit --wait`, `stapler staple`,
  `stapler validate`, `spctl --assess`).

### Release steps once enrolled

1. **Enrol** in the Apple Developer Program (individual or organisation).
2. **Create a Developer ID Application certificate** (Xcode > Settings >
   Accounts > Manage Certificates, or developer.apple.com > Certificates) and
   install it in the login keychain of the Mac that builds releases. Check it
   with `security find-identity -v -p codesigning`.
3. **Store notarisation credentials once** with an app-specific password (or an
   App Store Connect API key):

   ```bash
   xcrun notarytool store-credentials atk-notary \
       --apple-id <apple-id-email> --team-id <TEAMID> --password <app-specific-password>
   ```

4. **Build, sign, notarise and staple**, once per architecture:

   ```bash
   export ATK_CODESIGN_IDENTITY="Developer ID Application: David Shepstone (<TEAMID>)"
   export ATK_NOTARY_PROFILE=atk-notary
   export ATK_VERSION_SUFFIX=            # empty for a final release
   packaging/macos/build-dmg.sh --arch arm64 --smoke-test
   packaging/macos/build-dmg.sh --arch x86_64 --smoke-test   # on an Intel Mac or runner
   ```

   The script fails, and prints Apple's log, if notarisation is not accepted.
5. **Check on a second Mac:** download the `.dmg` from the website, open it
   and launch the app. There should be no Gatekeeper warning beyond the usual
   "downloaded from the internet" confirmation.
6. **Later, in CI:** import the certificate (`.p12` and password) into a
   temporary keychain from repository secrets and use an App Store Connect API
   key for `notarytool`, then run step 4 in a release workflow like
   `windows-release.yml`.

### Not verified yet

Notarisation itself cannot be tested without a Developer ID, so it has never
been run. CI proves the parts that can be checked locally: hardened-runtime
signing, strict verification, no stray install names, and the app starting
under the hardened runtime. Apple's service can still reject things these
checks do not cover. Its log (`notarytool log`) names the file and the reason.

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
