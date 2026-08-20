# macOS Packaging

**Status: not implemented.** Milestone M7, and blocked on the application being
built and tested on macOS at all.

## Plan

A standard `.app` bundle inside a `.dmg`.

CMake already sets `MACOSX_BUNDLE` and the bundle identifier
(`local.atkplayer.ATKPlayer`) on the `ATKPlayer` target, and
`qt_generate_deploy_app_script()` runs `macdeployqt` on install, so the bundle
layout is groundwork that already exists.

## Contents

- `ATKPlayer.app` with a complete `Info.plist` — document types for `.atkproj`,
  supported media UTIs, minimum system version
- Qt frameworks inside the bundle, dynamically linked
- FFmpeg dylibs, LGPL build, with `install_name` rewritten to `@rpath`
- Licence texts

## Signing and notarisation

Unsigned applications are effectively undistributable on current macOS. The
release process must:

1. Sign every nested framework and dylib, inside out, with a Developer ID
   Application certificate and the hardened runtime enabled.
2. Sign the bundle itself.
3. Submit the `.dmg` to Apple for notarisation and staple the ticket.

Do this in CI from the start. Retrofitting signing onto a manual release process
is where it usually goes wrong.

## Universal binaries

Build for `arm64` and `x86_64`. Qt and FFmpeg both need to be universal, or the
application has to ship as two separate downloads — decide before the first
release, not after.
