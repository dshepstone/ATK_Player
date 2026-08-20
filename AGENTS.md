# ATK Player Engineering Rules

## Technology

- Use C++20, Qt 6, target-based CMake, and Ninja. Windows uses MSVC; macOS and Linux follow later milestones.
- FFmpeg begins in M1. Prefer dynamic linkage. Do not enable GPL, nonfree, x264, x265, or fdk-aac without explicit approval.

## Architecture

- Keep decoding out of the UI. The media layer must not depend on widgets.
- `PlaybackController` owns playback behavior and state orchestration. `TimelineModel` owns timeline data, not rendering. `ViewerWidget` displays frames but never decodes them.
- Keep OS-specific code under `src/platform`. Maya- and Harmony-specific code must not become core dependencies.
- Respect milestone boundaries; placeholders do not authorize future milestone work.

## Threading and lifetime

- Future decoding must not block the GUI thread. Give FFmpeg contexts explicit single-thread ownership and return UI changes through thread-safe Qt signals or queued calls.
- Do not introduce casual shared mutable decoder state. Make shutdown order explicit: stop work, disconnect delivery, join workers, then destroy decoder, media, and UI state.
- QObject parentage, smart pointers, and signal contexts must make ownership and callback lifetime unambiguous.

## Frames and timing

- Frame stepping is a core requirement. With real media, do not implement it as only `current_time + 1/fps`; follow decoded presentation order and timestamps, including the actual previous displayed frame.
- Never assume 24 fps. Preserve rational rates such as 24000/1001, 30000/1001, and 60000/1001, and allow timestamp-based variable timing.

## Git and generated files

- Never push directly to `main`; use feature or chore branches and never force-push unrelated history.
- Do not commit build/install trees, binaries, DLLs, Qt deployment output, `vcpkg_installed`, generated test media, or machine-specific `CMakeUserPresets.json`.

## Verification

- Before claiming success, configure, build, and run the complete tests. Never claim a build or test result that was not actually run.
- Validate both Windows Debug and Release for changes that can affect compilation or linkage.
