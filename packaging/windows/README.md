# Windows Packaging

**Status: not implemented.** Milestone M6.

## Plan

**WiX Toolset v4** for an MSI. MSI rather than a self-extracting installer
because studios deploy by Group Policy, which needs an MSI.

## Contents

- `ATKPlayer.exe`
- The Qt runtime and its plugins, as produced by `cmake --install` (which runs
  `windeployqt` through `qt_generate_deploy_app_script`)
- The FFmpeg DLLs, LGPL build
- Licence texts: MIT for ATK Player, LGPL v3 for Qt, LGPL v2.1 for FFmpeg
- The Visual C++ redistributable, or a documented prerequisite check

## Behaviour

- Per-machine and per-user install options
- Start-menu shortcut; desktop shortcut optional
- File associations for `.atkproj` and, opt-in, for common media extensions —
  opt-in because silently taking over `.mp4` on an artist's machine is hostile
- Clean uninstall that leaves user preferences behind unless explicitly removed
- Upgrade in place, keyed on a stable upgrade GUID
- Code signing with an EV certificate, so SmartScreen does not warn on first run

## CI

The Windows workflow builds and tests on every push. Producing the MSI is added
as a separate job triggered on tagged releases, so ordinary pushes stay fast.
