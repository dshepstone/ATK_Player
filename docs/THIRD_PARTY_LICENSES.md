# Third-Party Licenses

A running record of every third-party component ATK Player links, bundles or
ships, so the licence position can be reviewed rather than reconstructed.

> **Not legal advice.** Nothing in this file is a legal conclusion. It records
> what each dependency is, how it is used, and which questions need answering.
> **The licences and redistribution obligations of every component listed here
> must be reviewed — by someone qualified to do so — before ATK Player is
> distributed publicly.** That review has not happened yet.

This file is maintained from the first commit rather than assembled before a
release, because reconstructing it after the fact is how obligations get missed.

**Status:** Phase 0 (M0). Qt is the only dependency currently linked.

---

## Currently used

### Qt 6

| | |
|---|---|
| Version | 6.5 or newer (development uses 6.9.3) |
| Licence | LGPL v3 (also available commercially) |
| Linkage | **Dynamic** |
| Used for | Application framework, widgets, event loop, JSON |
| Source | https://www.qt.io/ |
| Obtained via | Official prebuilt `win64_msvc2022_64` binaries |

**Engineering constraints the project applies.** These are choices made to keep
the licence position simple and reviewable — not determinations about what the
licence requires:

- Qt is **dynamically linked**, and the build offers no static-linking option.
- The Qt DLLs ship beside the executable rather than being merged into it.
- Qt is used unmodified.

**To review before distribution:** which LGPL v3 obligations apply to this
usage; what notice, licence text and relinking provisions must accompany a
binary release; and whether the commercial licence would be preferable.

---

## Planned

### FFmpeg — milestone M1

| | |
|---|---|
| Licence | LGPL v2.1 or later for the core; some components are GPL or nonfree |
| Linkage | **Dynamic** (planned) |
| Used for | Video and audio decoding, and export in M5 |
| Source | https://ffmpeg.org/ |

Components intended for use: `libavformat`, `libavcodec`, `libavutil`,
`libswscale`, `libswresample`.

**Engineering constraints the project intends to apply:**

- Build with `--disable-gpl` and `--disable-nonfree`, so no GPL or nonfree
  component is present in the binary at all.
- Dynamic linking only.
- If a GPL-only codec is ever genuinely required, reach it through a separate
  process or an optional component the user installs themselves — never linked
  into `ATKPlayer.exe`.

**To review before distribution:** the exact build configuration actually used,
which licence each bundled library falls under, and what must ship alongside the
binaries. Record the verbatim FFmpeg `configure` line here once the build is
pinned, so the position can be checked rather than assumed.

---

## Fonts, icons and other assets

None yet. `assets/icons/` is empty in Phase 0; the transport controls use text
glyphs from the system UI font.

When artwork is added, record for each item: the source, the author, the licence,
and whether attribution must appear in the About dialog.

---

## ATK Player itself

Released under the MIT Licence — see [../LICENSE](../LICENSE).

---

## How to add an entry

When adding a dependency, record before merging:

1. Name, version and project URL.
2. The exact licence, including which variant — LGPL v2.1 and LGPL v3 differ.
3. Static or dynamic linkage, and why.
4. What is expected to ship alongside the binary: licence text, notices, source
   offers.
5. Any constraint the project is choosing to adopt so the licence position stays
   simple, and what still needs checking.
