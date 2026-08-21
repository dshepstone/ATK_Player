# Roadmap

Milestones are ordered by dependency, not by preference. Each one should leave
the application in a state that runs and is worth using — no milestone is a
half-landed refactor.

**Current milestone: M1 — complete.**

---

## M0 — Application Framework ✅

Establish an architecture that the rest of the work can be built on top of
incrementally.

- [x] CMake project with `atk_core`, `atk_ui`, `ATKPlayer` and test targets
- [x] `CMakePresets.json` for Ninja + MSVC and the Visual Studio generator
- [x] Qt 6 main window: menu bar, sources panel, viewer, timeline, transport,
      status readout
- [x] Reusable `ViewerWidget` and `TimelineWidget`
- [x] Central command table driving menus, shortcuts and transport buttons from
      one definition
- [x] Default shortcuts: Space, ←, →, Home, End, L, I, O, B
- [x] Transport that visibly changes state, with stepping, seeking, looping and
      in/out ranges wired to the timeline model
- [x] Placeholder architecture for media, playback, projects, export, A/B
      comparison and the external API
- [x] Platform abstraction with Windows, macOS and Linux implementations
- [x] Logging with per-subsystem categories and a startup banner
- [x] Version defined once in CMake and surfaced in the title bar
- [x] Unit tests for the core library
- [x] Documentation: architecture, building, roadmap, API, licences
- [x] Windows CI workflow
- [x] Portable presets: Qt located through `QT_ROOT`, no machine path committed
- [x] VS Code configure / build / test / run / debug workflow

---

## M1 — FFmpeg Single Video Playback

Make the player play something. This is the milestone that turns the framework
into an application.

- Link FFmpeg dynamically, LGPL components only
- Implement `FFmpegDecoder`: probe, seek, decode
- Populate `MediaMetadata` — resolution, exact rational frame rate, duration,
  frame count
- Decode on a dedicated thread feeding `FrameCache`; the UI thread never blocks
- File → Open Media, with drag and drop onto the viewer
- Real-time playback at the source frame rate, with dropped-frame handling
- Frame-accurate seeking and stepping
- Viewer fit modes: fit in window, 1:1, and the aspect-ratio handling that goes
  with them

**Capabilities this milestone delivers**

| | |
|---|---|
| Formats | MOV, MP4, MKV, AVI and anything else the LGPL FFmpeg build demuxes |
| Decoding | `libavformat`, `libavcodec`, `libswscale`, `libswresample` |
| Audio | Decoded and played in sync with the master clock |
| Stepping | Frame-accurate in both directions, no drift after seeking |
| Clock | `PlaybackClock` driving real frames rather than a placeholder extent |
| Cache | `FrameCache` fed by the decode thread, sized against a byte budget |

The Phase 0 placeholder extent disappears here: `setFrameCount()` clears
`isPlaceholder()`, so the **NO MEDIA** marking removes itself the first time a
file opens.

**Done when** a 1080p clip opens, plays at the correct rate, and stepping lands
on exactly the frame the status bar reports.

---

## M2 — Animation Review Tools

Make it a review tool rather than a viewer. **In progress.**

Delivered so far:

- **Audible timeline scrubbing.** Dragging the timeline plays short faded grains
  at the media position under the cursor, from a dedicated audio path that never
  touches the playback clock.
- **Waveform on the timeline.** Amplitude peaks generated in the background and
  drawn above the track, so dialogue, silence and impacts are visible and the
  playhead crosses both.
- **Optional frame-step audio.** A separate, default-off command gives forward
  and backward arrow stepping centred review grains without compromising the
  exact visual navigation queue.
- **Active animation-review range.** `TimelineViewport` is both the visible
  interval and inclusive playback boundary. Loop-off stops on the selected end;
  Loop-on wraps end to start. F restores whole-clip playback.
- **Animation timeline controls.** A Maya-style lower review-range slider with
  independent handles and body pan, synchronized one-based numeric start/end
  fields, adaptive per-frame ticks/labels, and double-click Fit Entire Clip.
- **Session bookmarks.** Stable single-frame markers, exact click navigation,
  wrapping next/previous commands, deletion, and default-on pixel-based scrub
  snapping. The dockable editor now manages names, multiline notes, palette
  colours and exact one-based frame positions. Inclusive range bookmarks save
  the active review range, render as compact timeline bands, and activate that
  same `TimelineViewport`; persistence remains M3 work.
- **Viewer zoom and pan.** Viewer Fit, device-pixel 100%, cursor-anchored wheel
  zoom, bounded middle-mouse pan, double-click Fit, and a compact status-bar
  percentage. Navigation persists through playback and frame changes and is
  deliberately independent of the timeline and playback state.

Still to come in M2: further review refinements.

Deferred here deliberately from M1: audio scrubbing, audible single-frame
stepping, In/Out points, specific loop ranges, bookmarks and range bookmarks,
finer cache controls.

- In/out range editing by dragging the range handles on the timeline
- Viewer magnifier for close inspection
- Frame-by-frame navigation refinements: play backwards, shuttle speeds
- Onion skinning / previous-frame ghosting
- Configurable keyboard shortcuts, loaded into `CommandRegistry` from settings
- Icons in `assets/icons/`, replacing the placeholder text glyphs
- Preferences dialog

The supplied 32×32 ATK Player PNG is embedded for runtime application/window
identity. A true multi-resolution Windows `.ico` remains M6 packaging work when
a larger master asset is available.

**Capabilities this milestone delivers:** timeline scrubbing with audio
scrubbing, bookmarks with names/notes/colours, In/Out points, loop ranges, and
viewer zoom and pan.

---

## M3 — Projects and Playlists

Make a review session something you can save and hand to someone else.

- Implement `ProjectSerializer` for the `.atkproj` JSON format
- Playlist: multiple sources, reordering, switching between them
- Per-source review state persisted — bookmarks, ranges, frame offsets
- Recent projects, and reopening the last session
- Relative media paths so a review folder stays portable between machines
- Missing-media handling that asks for a new path instead of failing silently

---

## M4 — A/B Comparison

- Second `ViewerWidget` bound to `CompareSession`
- Horizontal, vertical and wipe layouts
- Both viewers driven by the single master `PlaybackClock`
- Per-source frame offset, adjustable live, for aligning takes with different
  handles
- Sources at different frame rates resolved through the master time base
- Difference and split-screen display modes

**Capabilities this milestone delivers:** horizontal A/B, vertical A/B,
synchronised playback and seeking, and per-source frame offsets — all driven by
one master clock, never two.

---

## M5 — Export and External API

The milestone that connects ATK Player to the rest of a pipeline.

- Implement `FFmpegExporter`: video, image sequence and single frame
- Burn-in of frame numbers, bookmarks and notes
- Export runs off the UI thread with progress and cancellation
- Implement `ApiServer`: newline-delimited JSON over a **loopback-only** TCP
  socket, off by default
- Python client library in `integrations/python/`
- Maya integration built on the Python client
- Harmony integration via the Python client or a script bridge

**Capabilities this milestone delivers:** FFmpeg encoding for export, a Python
client library, and Maya and Harmony integrations built on top of it.

---

## M6 — Windows MSI Installer

- WiX or equivalent installer definition in `packaging/windows/`
- Bundled Qt and FFmpeg runtimes with their licence texts
- File associations for supported media and for `.atkproj`
- Start-menu entry and optional per-user install
- Code signing
- CI builds the installer on tagged releases

---

## M7 — macOS

- Build and test the existing source tree on macOS
- Complete `PlatformInfo_Mac.cpp`, including display-sleep inhibition
- `.app` bundle with the correct `Info.plist`, a `.dmg`, notarisation
- Native menu-bar placement and macOS keyboard conventions
- Retina / high-DPI verification in the viewer
- macOS CI job

---

## M8 — Linux

- Build and test on Ubuntu LTS as the reference distribution
- Complete `PlatformInfo_Linux.cpp`, including D-Bus screensaver inhibition
- AppImage as the primary distribution, with `.deb` if there is demand
- Wayland and X11 verification
- Linux CI job

---

## Not scheduled

Ideas that are worth keeping but do not have a milestone yet:

- Audio playback and waveform display
- Drawn annotations over frames
- Colour management (OCIO), for review that has to match a grading pipeline
- Image-sequence support: EXR, DPX, PNG
- Network review sessions with a synchronised remote playhead
- A GPU decode and display path
