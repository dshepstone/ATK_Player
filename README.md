# ATK Player

ATK Player is an open-source, frame-accurate video playback and animation review application designed for animators, students, educators, and production workflows.

**Latest release: ATK Player 0.2.0**

ATK Player is **Animation Tool Kit — Media Player**, the companion application for the Animation Tool Kit Maya tools series. Created by David Shepstone.

ATK Player is built for the way animators actually review their work: scrubbing a few seconds back and forth, stepping frame by frame, marking frames that need attention, comparing takes, and quickly sending review material back into a production workflow.

It is an **animation review tool first and a media player second**.

> **Independent project**
>
> ATK Player is inspired by the workflows of professional animation review players. It is independently developed and contains no third-party proprietary source code, assets, or branding.

---

## Download

The latest Windows release is available from:

**[Download ATK Player](https://github.com/dshepstone/ATK_Player/releases/latest)**

### Windows Installer — Recommended

Download:

`ATK-Player-0.2.0-Windows-x64.msi`

The installer:

- installs ATK Player under `C:\Program Files\ATK Player`
- adds ATK Player to the Windows Start Menu
- registers ATK Player with Windows Installed Apps
- associates `.atkproj` project files with ATK Player
- includes the required Qt and FFmpeg runtime components
- includes applicable open-source licence texts and third-party notices

### Portable Version

Download:

`ATK-Player-0.2.0-Windows-x64-Portable.zip`

Extract the complete folder and run:

`ATKPlayer.exe`

Do not separate `ATKPlayer.exe` from the DLL and plugin folders included with the portable distribution.

### Windows Requirements

- **Windows 11 x64** is the currently verified release platform
- Microsoft Visual C++ 2015–2022 Redistributable (x64)

### Unsigned Build Notice

ATK Player 0.2.0 is currently distributed without a commercial Windows code-signing certificate.

Windows may therefore display an **Unknown Publisher** or Microsoft Defender SmartScreen warning when launching the installer.

SHA-256 checksum files are provided with the GitHub Release so downloaded files can be independently verified.

---

## Features

### Frame-Accurate Playback

ATK Player is designed around frame-accurate animation review rather than approximate media-player seeking.

- Frame-accurate video playback
- Synchronized audio playback
- Exact forward and backward frame stepping
- Direct frame-number navigation
- Frame, timecode, and frame-rate display
- Exact rational frame-rate handling
- Timeline scrubbing
- Review-range playback
- Looping
- Whole-clip playback
- Quick ±10 second navigation

Internal frame indexing is zero-based where appropriate for automation and APIs, while artist-facing UI frame numbers are presented in a familiar one-based form.

### Audio Review

ATK Player includes animation-focused audio tools for timing, dialogue, and lip-sync review.

- Audible timeline scrubbing
- Reverse audio while scrubbing backward
- Optional frame-step audio
- Progressive waveform generation
- Waveform normalization for useful visual review
- Persistent volume and mute controls
- Audio synchronization after seeks and range changes

### Timeline and Review Ranges

The timeline provides a dedicated animation-review range that controls playback, looping, waveform display, and review operations.

- Inclusive Start and End frames
- Direct numeric range input
- Timeline range handles
- Range body panning
- Fit Entire Timeline
- Frame ruler
- Exact playhead synchronization
- Looping within the selected review range

### Bookmarks and Notes

Bookmarks allow review comments to stay associated with exact frames or frame ranges.

- Point Bookmarks
- Range Bookmarks
- Bookmark names
- Review notes
- Bookmark colours
- Previous / Next Bookmark navigation
- Bookmark timeline display
- Bookmark range activation
- Optional snapping

Bookmarks and review ranges are stored with project sources.

### Projects and Playlists

ATK Player projects use the `.atkproj` format.

Projects support:

- Multiple media sources
- Ordered playlists
- Drag-and-drop reordering
- Current-source tracking
- Per-source bookmarks
- Per-source review ranges
- Relative media paths where possible
- Missing-media detection
- Media relinking
- Recent Projects
- Optional reopen-last-project behavior
- Portable human-readable JSON project files

Missing media remains represented in the project instead of silently discarding review information.

### A/B Comparison

ATK Player includes synchronized A/B animation comparison using **Source A as the master playback clock**.

Comparison layouts include:

- Side-by-Side
- Stacked
- Wipe
- Blend
- Difference

Additional comparison tools include:

- Source A / Source B selection
- Independent viewer transforms
- Source B frame offset
- External Audio source
- External Audio offset
- A / B / External audio selection
- Comparison waveform mapping
- Fullscreen comparison review

Identical-rate, zero-offset A/B sources are mapped frame-for-frame without cumulative timing drift.

### Viewer Navigation

The viewer includes animation-review navigation independent of playback timing.

- Fit Viewer
- True 100% pixel view
- Mouse-wheel zoom
- Middle-mouse pan
- Cursor-anchored zoom
- Smooth minification
- Pixel-oriented sampling above 100%
- Persistent viewer state during playback and stepping

### Fullscreen Review

Two fullscreen modes are available:

- **Full Screen Application** — `F11`
- **Video Full Screen** — `Ctrl+Shift+F`

Video Full Screen displays the active video or comparison presentation without the application interface.

Press `Esc` to leave video-only fullscreen.

---

## Export

ATK Player provides offline review export independent of viewer size, zoom, window layout, or playback state.

### Review Video

**File → Export Review…**

Exports the active inclusive review range to H.264/AAC MP4.

Export supports:

- Single-source review
- A/B comparison layouts
- Selected A/B/External soundtrack
- Offline rendering
- Progress reporting
- Cancellation
- Atomic destination replacement

### Current Frame

**Export Current Frame…**

Exports the current exact frame as a lossless PNG.

### Image Sequence

**Export Image Sequence…**

Exports the selected inclusive review range as a PNG image sequence.

### Burn-ins

Optional review burn-ins include:

- Frame number
- Bookmark label
- Bookmark range information
- Bookmark note
- Bookmark colour accents

Clean output without burn-ins remains the default.

---

## Media Information

**View → Media Information…**

Displays information about the active media including:

- Source name
- Duration
- Resolution
- Frame count
- Frame rate
- Video codec
- Pixel format
- Audio codec
- Channel layout
- Sample rate

Media Information can be copied to the clipboard for troubleshooting or review notes.

---

## Local API

ATK Player includes an optional local automation API for production tools and DCC integrations.

The API:

- is **disabled by default**
- listens only on the local loopback interface
- uses protocol version 1
- uses newline-delimited JSON messages
- uses zero-based API frame indices
- supports playback and review commands
- supports project operations
- supports bookmark operations
- supports A/B comparison
- supports review export
- supports still and image-sequence export

The default port is:

`45571`

See:

**[docs/API.md](docs/API.md)**

for the protocol and command reference.

---

## Maya Integration

ATK Player includes a Python client and Autodesk Maya review integration.

The integration can send animation review media to ATK Player while maintaining explicit scene-frame mapping between Maya and ATK.

Integration files are located under:

`integrations/`

See the integration documentation and source files for setup details.

---

## Toon Boom Harmony Integration

ATK Player includes a Toon Boom Harmony review workflow.

The integration has been validated with:

**Toon Boom Harmony Premium 25**

The workflow supports:

- full-scene review
- custom frame-range review
- movie export with audio
- launch/open in ATK Player
- Harmony-to-ATK frame mapping
- ATK-to-Harmony jump-back
- replacement of previous generated preview media
- Local API connection testing

Harmony frames remain one-based while ATK's internal/API frame mapping remains zero-based relative to the exported review range.

---

## High-Value Default Shortcuts

| Action | Shortcut |
|---|---|
| Play / Pause | `Space` |
| Previous Frame | `Left` |
| Next Frame | `Right` |
| Add Point Bookmark | `B` |
| Previous Bookmark | `Alt+Left` |
| Next Bookmark | `Alt+Right` |
| Fit Entire Timeline | `F` |
| Fit Viewer | `Ctrl+0` |
| Viewer 100% | `Ctrl+1` |
| Full Screen Application | `F11` |
| Video Full Screen | `Ctrl+Shift+F` |
| Stop | `Esc` |

Range Bookmark creation is explicit in the Bookmarks panel. `Shift+B` is unassigned by default.

Shortcuts can be customized under:

**Edit → Preferences → Shortcuts**

---

## Current Release Status

### ATK Player 0.2.0

The first public Windows release includes milestones M0 through M6:

- **M0** — Application framework
- **M1** — FFmpeg video/audio playback
- **M2** — Animation review tools
- **M3** — Projects and playlists
- **M4** — A/B comparison
- **M5** — Export and external API
- **M6** — Windows MSI packaging

The Windows MSI installer and portable distribution have completed release validation.

Future platform milestones:

- **M7** — macOS
- **M8** — Linux

See:

**[docs/ROADMAP.md](docs/ROADMAP.md)**

for the development roadmap.

---

## Platform Support

| Platform | Status |
|---|---|
| Windows 11 x64 | **Supported and currently verified** |
| Windows 10 x64 | Not currently part of the verified release matrix |
| macOS | Planned — M7 |
| Linux | Planned — M8 |

The source architecture is designed for multiple platforms, but the current public binary release is for Windows x64.

Operating-system-specific implementation code is isolated under:

`src/platform/<os>/`

---

## Building from Source

Full build instructions are available in:

**[docs/BUILDING.md](docs/BUILDING.md)**

### Windows Development Requirements

Current development configuration uses:

- Windows 11
- Visual Studio 2022
- MSVC x64
- CMake
- Ninja
- Qt 6.9.3
- vcpkg
- FFmpeg 9.0.1

ATK Player uses C++20.

### Qt

Set the Qt installation through `QT_ROOT` or the project's supported local configuration.

Example:

```powershell
setx QT_ROOT "C:\Qt\6.9.3\msvc2022_64"
```

Do not commit developer-specific Qt paths to the repository.

### Configure

From a Visual Studio 2022 developer environment:

```powershell
cmake --preset windows-debug
```

### Build

```powershell
cmake --build --preset windows-debug
```

### Test

```powershell
ctest --test-dir build/windows-debug --output-on-failure
```

### Release Build

```powershell
cmake --preset windows-release
cmake --build --preset windows-release
ctest --test-dir build/windows-release --output-on-failure
```

See `docs/BUILDING.md` for the complete dependency and packaging workflow.

---

## Windows Packaging

Windows packaging is located under:

`packaging/windows/`

The release pipeline uses:

- CMake install staging
- Qt deployment tooling
- dynamically linked Qt runtime libraries
- dynamically linked FFmpeg runtime libraries
- WiX Toolset v4
- per-machine MSI installation
- SHA-256 release checksums

The installed application location is:

```text
C:\Program Files\ATK Player
```

The installer creates a Start Menu shortcut and registers `.atkproj` files with ATK Player.

It does **not** take over general media associations such as `.mp4`, `.mov`, `.mkv`, or `.avi`.

See:

**[packaging/windows/README.md](packaging/windows/README.md)**

for packaging details.

---

## Repository Layout

```text
.github/workflows/  GitHub Actions build and release validation
.vscode/            VS Code development configuration

assets/icons/       Application and UI icons

cmake/              CMake helper modules

src/core/           Versioning, logging and shared core infrastructure
src/app/            Application startup and executable
src/ui/             Main window, viewer, timeline and controls
src/media/          Media metadata, FFmpeg decoding and frame/audio data
src/playback/       Playback controller, clock and comparison state
src/timeline/       Timeline, bookmarks, ranges and timecode
src/project/        .atkproj project model and serialization
src/export/         Offline video, still and image-sequence export
src/api/            Local external-control API
src/platform/       Windows / macOS / Linux platform implementations

integrations/       Python, Maya and Toon Boom Harmony integrations

tests/              Automated test suites
docs/               Architecture, build, API, roadmap and licence documentation
packaging/          Windows installer and distribution tooling
```

Read:

**[docs/ARCHITECTURE.md](docs/ARCHITECTURE.md)**

before making structural changes. It documents layer boundaries, threading, ownership, decoder behavior, and timing architecture.

---

## Documentation

| Document | Contents |
|---|---|
| [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) | Architecture, layer boundaries, ownership and threading |
| [docs/BUILDING.md](docs/BUILDING.md) | Build prerequisites, presets and development setup |
| [docs/ROADMAP.md](docs/ROADMAP.md) | Development milestones |
| [docs/API.md](docs/API.md) | Local API protocol and commands |
| [docs/THIRD_PARTY_LICENSES.md](docs/THIRD_PARTY_LICENSES.md) | Third-party dependency and distribution information |
| [packaging/windows/README.md](packaging/windows/README.md) | Windows installer and packaging workflow |

---

## Architecture Principles

ATK Player follows several important architectural rules.

### Frame Accuracy

Frame identity is based on actual presentation timestamps and rational frame-rate mapping rather than approximate floating-point frame durations.

### Decoder Ownership

FFmpeg decoder contexts are owned by dedicated decoder workers and do not run on the GUI thread.

### Bounded Memory

Decoded-frame caches and playback queues are explicitly bounded.

### Generation Safety

Source changes and seek operations use generation identifiers so stale asynchronous decoder results cannot replace newer state.

### A/B Synchronization

Source A is the sole comparison playback clock.

Source B follows Source A through deterministic timestamp/frame mapping rather than maintaining an independent playback clock.

### Project Portability

`.atkproj` files are human-readable JSON and prefer relative media paths where possible.

### Local Automation

The Local API is intentionally loopback-only and disabled by default.

---

## Open Source and Third-Party Software

ATK Player is open-source software released under the **MIT License**.

See:

**[LICENSE](LICENSE)**

The Windows distribution also uses open-source third-party runtime components.

Major components include:

### Qt 6.9.3

- GNU Lesser General Public License v3
- dynamically linked
- runtime libraries remain separate from ATK Player

### FFmpeg 9.0.1

- GNU Lesser General Public License v2.1 or later as built for ATK Player
- dynamically linked
- supplied through the project's pinned vcpkg configuration
- optional GPL/nonfree features are not enabled in the distributed build
- x264, x265, and fdk-aac are not enabled

Applicable licence texts and third-party notices are included with the Windows binary distributions.

See:

**[docs/THIRD_PARTY_LICENSES.md](docs/THIRD_PARTY_LICENSES.md)**

and:

**[packaging/windows/THIRD_PARTY_NOTICES.txt](packaging/windows/THIRD_PARTY_NOTICES.txt)**

for more information.

---

## Security

ATK Player's Local API is disabled by default and binds only to the local loopback interface when enabled.

Security issues should not be disclosed through a public issue.

See:

**[SECURITY.md](SECURITY.md)**

for vulnerability-reporting information.

---

## Issues and Feedback

Bug reports, workflow feedback, and feature requests are welcome through GitHub Issues.

When reporting a playback or media problem, useful information includes:

- ATK Player version
- Windows version
- media container
- video codec
- frame rate
- resolution
- audio codec
- whether the issue occurs during playback, stepping, scrubbing, comparison, or export

The **Media Information** window can provide much of this information.

---

## Contributing

Contributions and well-scoped pull requests are welcome.

Before making architectural changes, please review:

- [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md)
- [docs/BUILDING.md](docs/BUILDING.md)
- [AGENTS.md](AGENTS.md)

Changes should preserve ATK Player's core requirements around:

- frame accuracy
- deterministic playback
- bounded memory
- decoder-thread ownership
- source-generation safety
- Source A comparison clock authority
- portable build configuration

---

## Licence

Copyright © 2026 David Shepstone.

ATK Player is released under the **MIT License**.

See [LICENSE](LICENSE) for the full licence text.