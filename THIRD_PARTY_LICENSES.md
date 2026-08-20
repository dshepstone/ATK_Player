# Third-Party Licenses

Every third-party component ATK Player links, bundles or ships is recorded here,
with the licence it is used under and the obligations that follow from it. The
file is maintained from the first commit rather than assembled before a release,
because reconstructing this after the fact is how licence violations happen.

**Status:** Phase 0. Qt is the only dependency currently linked.

---

## Currently used

### Qt 6

| | |
|---|---|
| Version | 6.5 or newer |
| Licence | LGPL v3 |
| Linkage | **Dynamic** |
| Used for | Application framework, widgets, event loop, JSON |
| Source | https://www.qt.io/ |

**Obligations under LGPL v3:**

- Qt must remain **dynamically linked**. Static linking would extend the LGPL's
  relinking obligation to ATK Player itself. This is why `docs/BUILDING.md` and
  the CMake configuration never enable a static Qt build.
- The user must be able to replace the Qt libraries with their own build. Shipping
  the Qt DLLs beside the executable satisfies this.
- The LGPL text and Qt's copyright notice must be distributed with the
  application. This is a packaging task for milestone M6.
- Any modifications made to Qt itself must be published. ATK Player does not
  modify Qt.

---

## Planned

### FFmpeg (milestone M1)

| | |
|---|---|
| Licence | LGPL v2.1 or later — **LGPL build only** |
| Linkage | **Dynamic** |
| Used for | Video and audio decoding, and export in M5 |
| Source | https://ffmpeg.org/ |

**Rules this project applies:**

- Build with `--disable-gpl` and `--disable-nonfree`. GPL components (such as
  x264 and x265 when linked directly) would make the whole application GPL;
  nonfree components cannot be redistributed at all.
- Dynamic linking only, for the same reason as Qt.
- If a GPL-only codec is ever genuinely required, it must be reached through a
  separate process or an optional plugin the user installs — never linked into
  `ATKPlayer.exe`.
- The FFmpeg licence text and its `LICENSE.md` must ship with the application.

Record the exact FFmpeg build configuration here when it is first bundled, so the
licence position can be verified rather than assumed.

---

## Fonts, icons and other assets

Nothing yet. `assets/icons/` is empty in Phase 0; the transport controls use
text glyphs from the system UI font.

When artwork is added, record for each item: the source, the author, the licence,
and whether attribution must appear in the About dialog.

---

## How to add an entry

When adding a dependency, record before merging:

1. Name, version and project URL.
2. The exact licence, including which variant (LGPL v2.1 and LGPL v3 differ).
3. Static or dynamic linkage, and why.
4. What must be shipped alongside the binary — licence text, notices, source
   offer.
5. Anything the licence forbids, so a later change does not quietly break it.
