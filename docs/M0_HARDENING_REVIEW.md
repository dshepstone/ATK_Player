# M0 Hardening Review

## Reviewed

Application/UI ownership and shutdown, playback/timing/timeline models, media placeholders and cache, project/API/export boundaries, platform isolation, CMake/presets, Windows CI, VS Code settings, repository hygiene, signal connections, and all M0 tests.

## Issues Fixed

- **API numeric validation:** JSON frame/range values were cast from `double` without rejecting fractions, non-finite numbers, or values outside `int64_t`. This could truncate commands or invoke undefined behavior at the future network boundary. Integer parameters are now validated before conversion and covered by behavioral tests.
- **Playback-range normalization:** negative or out-of-extent endpoints could remain in the stored model even though playback used effective clamped bounds. This exposed contradictory state to API/project consumers. Ranges now normalize to non-negative endpoints and the loaded extent, with edge-case coverage.
- **Bookmark coverage:** bookmark optional fields, labels, equality, and palette bounds had no dedicated suite. A focused deterministic suite now protects public behavior without constraining future UI or serialization internals.
- **Agent workflow:** repository-level engineering rules now state the architectural, threading, timing, FFmpeg licensing, scope, Git, and verification constraints future contributors need.

## M1 Risks to Watch

- Give decoder contexts and worker objects explicit thread affinity and ownership; make shutdown cancel work and join before media/UI destruction.
- Map display frame indexes to decoded presentation order and timestamps. Treat stream-reported frame counts as advisory and handle VFR, B-frames, and missing or duplicate timestamps.
- Make seeks generation-aware so stale decode results cannot overwrite a newer seek; define cache invalidation when source, stream, or presentation mapping changes.
- Keep GUI objects and image presentation on the GUI thread. Avoid synchronous decode or blocking worker calls from transport/timeline handlers.
- Reconcile the monotonic playback clock with audio/timestamp authority without accumulating floating-point frame error; preserve neutral rational types outside FFmpeg-facing code.
- Define end/error/loading/seeking transitions before layering decoder callbacks onto the M0 state enum, and prevent late timer events during source replacement or shutdown.
