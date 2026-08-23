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

**Status:** M6 release packaging implemented, awaiting installer acceptance.

---

## Currently used

### Qt 6

| | |
|---|---|
| Version | **6.9.3** in the Windows RC distribution |
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

The Windows stage installs the authoritative LGPL v3 text as
`licenses/Qt-LGPL-3.0.txt`, identifies Qt/source URLs in
`THIRD_PARTY_NOTICES.txt`, and keeps every Qt DLL separately replaceable.

---

### FFmpeg — in use since M1

| | |
|---|---|
| Version | **9.0.1** (vcpkg port `ffmpeg`, port-version 1) |
| Licence | LGPL v2.1 or later, as built here |
| Linkage | **Dynamic** — `avcodec-63.dll`, `avformat-63.dll`, `avutil-61.dll`, `swscale-10.dll`, `swresample-7.dll`, `avfilter-12.dll`, `avdevice-62.dll` |
| Used for | Demuxing, video and audio decoding, pixel format conversion, audio resampling |
| Source | https://ffmpeg.org/ , built from source by vcpkg |
| Pinned by | `builtin-baseline` `45f9f39362a4c52e2b1fbe57b7e649db7f3d96d4` in `vcpkg.json` |

**Features enabled** (`vcpkg.json`): `avcodec`, `avformat`, `swresample`,
`swscale`, `avdevice`, `ffmpeg`, `ffprobe`.

`avdevice` is present only to supply the `lavfi` input device used to generate
deterministic test fixtures; the application does not use it. `ffmpeg` and
`ffprobe` are development tools for generating and validating those fixtures and
are **not** redistributed with the application.

**GPL and nonfree components are disabled.** The vcpkg port is configured with
`--disable-gpl` and no `nonfree`, `x264`, `x265`, or `fdk-aac` feature is
requested. The port's configure line records this explicitly:

```
--disable-libx264 --disable-libx265 --disable-libfdk-aac
--disable-nonfree --disable-libvpx --disable-libmp3lame ...
```

ATK Player relies on FFmpeg's built-in LGPL decoders. Enabling any GPL or
nonfree feature would change the licence position of the whole distribution and
must not be done without a deliberate decision recorded here.

The Windows MSI ships only application-required `avcodec-63.dll`,
`avformat-63.dll`, `avutil-61.dll`, `swresample-7.dll` and `swscale-10.dll`.
The pinned vcpkg copyright/license bundle is installed as
`licenses/FFmpeg-LGPL-2.1.txt`; version, build constraints and source URLs are
recorded in `THIRD_PARTY_NOTICES.txt`.

---

## Fonts, icons and other assets

`assets/icons/ATK_Player_Icon.png` is the repository's canonical ATK Player
application artwork. M6 derives `ATK_Player_Icon.ico` from that committed image
for Windows executable, shortcut, installer and project-association identity.
The current master is 32×32; a future artwork pass should supply a genuine
multi-resolution master rather than upscaling it and claiming extra detail.

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
