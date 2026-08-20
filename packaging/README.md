# Packaging

Installer and bundle definitions, one directory per platform.

Nothing here is built yet. Packaging starts at milestone **M6**.

| Directory | Format | Milestone |
|---|---|---|
| [`windows/`](windows/) | MSI installer | M6 |
| [`macos/`](macos/) | `.app` bundle in a signed, notarised `.dmg` | M7 |
| [`linux/`](linux/) | AppImage, with `.deb` if there is demand | M8 |

## What every package must include

These are licence obligations, not preferences — see
[../THIRD_PARTY_LICENSES.md](../THIRD_PARTY_LICENSES.md):

- The Qt runtime, **dynamically linked**, together with the LGPL v3 text.
- The FFmpeg runtime, **dynamically linked**, LGPL build only, with its licence
  text. GPL and nonfree components must not be present.
- ATK Player's own MIT licence.

Static linking of either dependency would change the licence position of the
whole application, which is why the CMake configuration never offers it as an
option.

## Shared groundwork

The install rules live in `src/app/CMakeLists.txt` and use
`qt_generate_deploy_app_script()`, so `cmake --install` already produces a
correct runtime layout on each platform. The packaging definitions here build on
that rather than assembling their own file lists — one place to get the layout
right.
