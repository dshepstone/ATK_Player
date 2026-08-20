# ATK Player

A cross-platform animation playback and review application.

ATK Player is built for the way animators actually watch their work: scrubbing a
few seconds back and forth frame by frame, marking the frames that need fixing,
and comparing a new take against the previous one. It is a review tool first and
a media player second.

> **Independent project.** ATK Player is inspired by the workflow of
> professional animation review players. It is an independently developed
> application and contains no third-party proprietary source code, assets or
> branding.

---

## Current status

**Version 0.1.0-dev — Phase 0 (milestone M0), application framework.**

The framework is in place and the application runs. Media playback is not
implemented yet.

**What works today**

- The application builds and launches on Windows as `ATKPlayer.exe`.
- The full interface is present: menu bar, sources panel, viewer, timeline,
  transport controls and status readout.
- The transport is real: play/pause toggles state visibly, stepping, seeking,
  looping and in/out ranges all drive the timeline model.
- Scrubbing the timeline, setting range in/out (`I` / `O`) and dropping
  bookmarks (`B`) work against the model and redraw.
- Every command runs through one central table, so keyboard shortcuts, menu
  entries and transport buttons are the same objects.
- The external-API command dispatcher is implemented and unit-tested.

**What does not work yet**

- **No media can be opened.** `FFmpegDecoder` is a documented stub, so the
  viewer shows its empty state and the timeline reports `0 / 0`.
- Projects cannot be saved or loaded.
- A/B comparison exists as an architecture, not as a second viewer.
- The API server opens no socket — only the dispatcher underneath it is real.
- Audio, export and the DCC integrations are not started.

Menu entries for unimplemented commands are present and report themselves in the
status bar rather than doing nothing silently.

---

## The long-term goal

An animation review player that a small studio or an individual animator can
rely on:

- Frame-accurate playback of the formats that come out of Maya, Harmony, Blender
  and a render farm.
- Review annotation — bookmarks, notes, in/out ranges — that survives being saved
  and sent to someone else.
- A/B comparison of two takes against a single shared clock, so the two can never
  drift apart.
- A local API that lets Maya and Harmony drive the player directly, so a review
  loop does not mean alt-tabbing and re-finding your place.
- The same source tree building on Windows, macOS and Linux.

See [docs/ROADMAP.md](docs/ROADMAP.md) for the milestone plan.

---

## Platform support

| Platform | Status |
|---|---|
| Windows 10/11 (x64) | **Current development target** |
| macOS | Planned — milestone M7. Source tree and CMake are already structured for it. |
| Linux | Planned — milestone M8. Same. |

Operating-system-specific code is confined to `src/platform/<os>/`; nothing else
in the tree may include a platform header.

---

## Building

Full instructions, including how to install the prerequisites, are in
[docs/BUILDING.md](docs/BUILDING.md). The short version:

**Prerequisites:** Visual Studio 2022 with the C++ desktop workload, CMake 3.25+,
Ninja, and Qt 6.5+ for MSVC 2022 (64-bit).

From a **Developer Command Prompt for VS 2022**, with Qt on `CMAKE_PREFIX_PATH`:

```bash
cmake --preset windows-msvc-debug
```

```bash
cmake --build --preset windows-msvc-debug
```

```bash
ctest --preset windows-msvc-debug
```

The executable lands at `build/windows-msvc-debug/bin/ATKPlayer.exe`. The Qt
runtime is copied next to it automatically, so it can be launched directly.

In VS Code, install the CMake Tools extension, pick the **Windows x64 Debug**
preset, and build with F7 — the presets are configured for it.

---

## Repository layout

```
src/core/         Version, logging, the command table
src/app/          main.cpp -- entry point only
src/ui/           Widgets: viewer, timeline, transport, sources, status
src/media/        Media sources, metadata, frame cache, decoders
src/playback/     Playback clock and transport controller
src/timeline/     Timeline model, bookmarks, playback range, timecode
src/project/      Project model and the .atkproj serializer
src/export/       Export jobs (milestone M5)
src/api/          External control API
src/compare/      A/B comparison session (milestone M4)
src/platform/     Per-OS implementations -- windows / macos / linux
integrations/     Python client, Maya and Harmony bridges (milestone M5)
tests/            Unit tests for the core library
docs/             Architecture, roadmap, build and API documentation
packaging/        Installer and bundle definitions (milestone M6+)
```

Read [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) before making structural
changes — it explains which layer is allowed to depend on which, and why.

---

## Documentation

| Document | Contents |
|---|---|
| [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) | Layer boundaries, ownership, threading and the reasoning behind them |
| [docs/BUILDING.md](docs/BUILDING.md) | Prerequisites, presets, VS Code setup, troubleshooting |
| [docs/ROADMAP.md](docs/ROADMAP.md) | Milestones M0 through M8 |
| [docs/API.md](docs/API.md) | The planned external control API and its command reference |
| [THIRD_PARTY_LICENSES.md](THIRD_PARTY_LICENSES.md) | Dependency licences and the obligations they carry |

---

## Licence

ATK Player is released under the MIT Licence — see [LICENSE](LICENSE).

Its dependencies are not MIT. Qt is used under the LGPL v3 and FFmpeg will be
used under the LGPL v2.1; both must remain dynamically linked. See
[THIRD_PARTY_LICENSES.md](THIRD_PARTY_LICENSES.md).
