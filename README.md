# ATK Player

A cross-platform animation playback and review application.

ATK Player is **Animation Tool Kit - Media Player**, the companion application
for the Animation Tool Kit - Maya tools series. Created by David Shepstone.

The View menu provides two fullscreen modes: **Full Screen Application** (`F11`)
keeps the ATK interface visible, while **Video Full Screen** (`Ctrl+Shift+F`)
presents only the aspect-correct video on black. Press `Esc` to leave video-only
fullscreen.

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

**Version 0.2.0-dev — M3 projects and playlists complete.**

ATK Player now provides frame-accurate video/audio review, exact ranges,
bookmarks, waveform/audio scrubbing, viewer navigation and persistent global
preferences, portable projects, ordered playlists and missing-media recovery.

**What works today**

- **Projects and playlists** — create/open a project, multi-add clips, reorder or
  remove them, double-click to activate, and save/reopen portable `.atkproj` files.
  Point/Range Bookmarks and review ranges are restored per clip.
- **Project recovery** — missing and unreadable sources stay in the playlist with
  an explicit state. Relink validates replacement media with FFmpeg before it
  changes the project, preserves the source UUID and keeps all review work that
  still fits the replacement clip.
- **Progressive playlist metadata** — rows appear immediately, then a dedicated
  background worker fills in resolution, exact rational frame rate and duration
  without decoding frames or blocking playback/UI work.
- **Recent projects** — File → Recent Projects keeps a normalized ten-project
  history, marks unavailable files, and can clear the list. An opt-in General
  preference reopens the last valid project when no command-line path was given.
- **Opening one video** — File → Open Media, or `ATKPlayer.exe <file>` from the
  command line. FFmpeg decides what is readable, so the format list is a
  convenience rather than a gate.
- **FFmpeg decoding** of MP4, MOV, MKV, AVI and anything else the LGPL build
  demuxes.
- **Normal audio playback**, decoded and resampled with FFmpeg and played through
  Qt Multimedia's `QAudioSink`.
- **Play / pause** with real video and audio.
- **Continuous playlist playback** advances at each clip's review-range end when
  Loop is off; Loop keeps the current clip repeating.
- **Quick skip and global audio** — transport buttons move ±10 seconds within the
  review range; the compact volume popup controls persistent volume and mute.
- **Frame stepping** — Right and Left arrows move exactly one *decoded
  presentation* frame, not one nominal frame duration. Backward stepping seeks to
  an earlier keyframe and decodes forward to land on the right picture.
- **Timeline seek** by click or drag, against the real duration.
- **Whole-clip looping** (`L`).
- **Stop** (`Esc`) returns to frame 0 without unloading the media.
- **Real frame, timecode and FPS readout**, with an estimated total frame count
  marked as such rather than presented as exact.
- Decoding runs on its own thread, so the interface stays responsive while
  seeking.
- **Preferences** live under Edit → Preferences. Review toggles, volume,
  configurable shortcuts and optional window/dock layout persist between runs.
  Shortcut conflicts are identified before assignment and cleared shortcuts are
  remembered.

With no media loaded the window still installs a clearly-marked **placeholder**
100-frame extent so the transport is demonstrable; the status bar says
`NO MEDIA — placeholder values` and the API reports `"placeholder": true`. That
marking disappears the moment a real file opens.

**What is in progress or not implemented yet**

- M4 A/B comparison is complete: transient A/B source selection, Side-by-Side,
  Stacked, Wipe, Blend and Difference views, one Source A master clock,
  timestamp mapping across different frame rates, independent viewer transforms,
  and selectable A, B, or independent External Audio through one output. This
  supports two animation-only takes sharing a dialogue/reference track. The
  Source A timeline displays the selected A/B/External soundtrack waveform, Play
  from comparison end restarts at A's review-range start, B and External Audio
  offsets use frame-oriented controls backed by signed microseconds, and selected
  waveforms follow those offsets without reanalysis. Composite modes share one
  zoom/pan transform, while dual views retain independent transforms. Video Full
  Screen displays the active comparison presentation without recreating decoders.
- M5 export foundation is in progress: **File → Export Review…** renders the
  active inclusive review range to H.264/AAC MP4, including all five comparison
  layouts and the selected A/B/External soundtrack. Rendering is offline and
  independent of viewer visibility, zoom, crop, playback, and window size; it
  provides progress, cancellation, and atomic destination replacement. Image
  sequences, still frames, burn-ins, and the external API remain future M5 work.
- The API server opens no socket — only the dispatcher underneath it is real.
- Maya and Harmony integrations, the MSI installer, macOS and Linux.

Menu entries for unimplemented commands are present and report themselves in the
status bar rather than doing nothing silently.

### High-value default shortcuts

| Action | Shortcut |
|---|---|
| Play / Pause | Space |
| Previous / Next Frame | Left / Right |
| Add Point Bookmark | B |
| Previous / Next Bookmark | Alt+Left / Alt+Right |
| Fit Entire Timeline | F |
| Fit Viewer / Viewer 100% | Ctrl+0 / Ctrl+1 |

Range Bookmark creation remains explicit in the Bookmarks panel; `Shift+B` is
unassigned by default. All commands, including Add Range Bookmark, can be given
a custom binding in Preferences.

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

**Prerequisites:** Visual Studio 2022 with the C++ desktop workload (it bundles
CMake and Ninja), and Qt 6.5+ for MSVC 2022 (64-bit).

Point the build at Qt once — `CMakePresets.json` is committed, so it reads this
rather than carrying anyone's local path:

```bash
setx QT_ROOT C:/Qt/6.9.3/msvc2022_64
```

Then, from a **Developer Command Prompt for VS 2022**:

```bash
cmake --preset windows-debug
```

```bash
cmake --build --preset windows-debug
```

```bash
ctest --preset windows-debug
```

The executable lands at `build/windows-debug/bin/ATKPlayer.exe`. The Qt
runtime is copied next to it automatically, so it can be launched directly.

In VS Code: accept the recommended extensions, pick the **Windows x64 Debug**
preset, build with **F7** and debug with **F5**. Configure, build, test and run
are also available as tasks.

---

## Repository layout

```
src/core/         Version, logging, the command table
src/app/          Entry point and the Application startup object
src/ui/           Widgets: viewer, timeline, transport, sources, status
src/media/        Media sources, metadata, frame cache, decoders
src/playback/     Playback clock, transport controller, A/B compare session
src/timeline/     Timeline model, bookmarks, playback range, timecode
src/project/      Project model and the .atkproj serializer
src/export/       Export jobs (milestone M5)
src/api/          External control API
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
| [docs/THIRD_PARTY_LICENSES.md](docs/THIRD_PARTY_LICENSES.md) | Dependency licences and the obligations they carry |

---

## Licence

ATK Player is released under the MIT Licence — see [LICENSE](LICENSE).

Its dependencies are not MIT. Qt is used under the LGPL v3 and FFmpeg will be
used under the LGPL v2.1; both must remain dynamically linked. See
[docs/THIRD_PARTY_LICENSES.md](docs/THIRD_PARTY_LICENSES.md).
