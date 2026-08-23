# Third-Party Licenses

This document records the major third-party components that ATK Player links, bundles, or distributes.

It is maintained alongside the source code so dependency versions, linkage choices, licence information, and distribution requirements remain documented as the project evolves.

> **Note**
>
> This document is provided for project and distribution documentation. It is not legal advice.

---

## Distribution Status

ATK Player 0.2.0 is released as open-source software under the MIT License.

The Windows installer and portable distribution include the applicable licence texts and third-party notices for redistributed runtime components.

The major redistributed components in ATK Player 0.2.0 are:

- ATK Player — MIT License
- Qt 6.9.3 — LGPL v3, dynamically linked
- FFmpeg 9.0.1 — LGPL v2.1 or later as built for ATK Player, dynamically linked

The Windows distribution also requires the Microsoft Visual C++ 2015–2022 x64 Redistributable, which is supplied separately by Microsoft.

The authoritative Windows distribution notice is:

`packaging/windows/THIRD_PARTY_NOTICES.txt`

---

## Qt 6

| | |
|---|---|
| Version | **6.9.3** |
| Licence | GNU Lesser General Public License v3 |
| Linkage | **Dynamic** |
| Used for | Application framework, widgets, event loop, JSON, networking, multimedia and UI infrastructure |
| Project | https://www.qt.io/ |
| Source | https://code.qt.io/ |
| Windows build | Official MSVC 2022 x64 Qt binaries |

ATK Player dynamically links Qt runtime libraries.

The Qt DLLs remain separate files in the Windows distribution rather than being statically incorporated into the ATK Player executable.

ATK Player uses the distributed Qt runtime libraries without modifying Qt itself.

The Windows installer and portable package include the LGPL v3 licence text as:

```text
licenses/Qt-LGPL-3.0.txt
```

Qt project and source references are also included in the distribution's third-party notices.

---

## FFmpeg

ATK Player uses FFmpeg for media decoding and processing.

| | |
|---|---|
| Version | **9.0.1** |
| vcpkg port revision | **1** |
| Licence | LGPL v2.1 or later, as built for ATK Player |
| Linkage | **Dynamic** |
| Used for | Demuxing, video decoding, audio decoding, pixel conversion and audio resampling |
| Project | https://ffmpeg.org/ |
| Source | https://ffmpeg.org/download.html |
| Package source | Microsoft vcpkg |
| vcpkg baseline | `45f9f39362a4c52e2b1fbe57b7e649db7f3d96d4` |

ATK Player dynamically links the application-required FFmpeg runtime libraries.

The Windows 0.2.0 distribution includes:

```text
avcodec-63.dll
avformat-63.dll
avutil-61.dll
swresample-7.dll
swscale-10.dll
```

Development dependencies may contain additional FFmpeg libraries and command-line tools, but those are not part of the ATK Player Windows runtime distribution.

### FFmpeg Build Configuration

The FFmpeg build used by ATK Player is configured without optional GPL or nonfree components.

The distributed build does not enable:

- x264
- x265
- fdk-aac
- FFmpeg nonfree components

ATK Player relies on FFmpeg functionality available under the LGPL configuration used by the project.

Changing the FFmpeg build configuration to enable GPL or nonfree components requires a deliberate dependency and distribution review before such a build is released.

### FFmpeg Tools

Development environments may include:

```text
ffmpeg.exe
ffprobe.exe
```

These utilities are used for development, testing, fixture generation, and validation.

They are **not distributed** in the ATK Player 0.2.0 Windows MSI or portable package.

### FFmpeg Licence Files

The Windows distribution includes the applicable FFmpeg licence and component information as:

```text
licenses/FFmpeg-LGPL-2.1.txt
```

The distribution also includes FFmpeg version, source, build provenance, and configuration information in the third-party notice file.

---

## Microsoft Visual C++ Runtime

ATK Player requires the:

**Microsoft Visual C++ 2015–2022 Redistributable (x64)**

This runtime is distributed separately by Microsoft and is not embedded as arbitrary Visual Studio runtime files inside the ATK Player MSI.

Microsoft provides the redistributable through its official Windows development channels.

Windows system libraries used by ATK Player are supplied as part of Microsoft Windows and are not redistributed as part of the ATK Player package.

---

## Fonts, Icons and Other Assets

### ATK Player Application Icon

The canonical ATK Player application artwork is stored at:

```text
assets/icons/ATK_Player_Icon.png
```

The Windows packaging process derives the required Windows icon resources from this repository-owned artwork.

The current canonical image is 32×32 pixels.

A future artwork revision may provide a native multi-resolution source for larger Windows icon sizes.

### UI Icons

Repository-owned UI icons under:

```text
assets/icons/
```

are distributed as part of ATK Player and are covered by the project's licensing terms unless otherwise noted.

Any future third-party artwork or icon library must have its licence and attribution requirements documented here before being distributed.

---

## ATK Player

ATK Player itself is released under the MIT License.

See:

```text
LICENSE
```

Copyright © 2026 David Shepstone.

---

## Distribution Licence Files

The Windows ATK Player distribution includes a `licenses` directory containing the applicable project and third-party licence information.

For ATK Player 0.2.0 this includes:

```text
ATK-Player-MIT.txt
Qt-LGPL-3.0.txt
FFmpeg-LGPL-2.1.txt
THIRD_PARTY_NOTICES.txt
```

These files should remain part of future Windows installer and portable distributions unless the dependency set or licensing configuration changes.

---

## Adding a New Dependency

Before merging a new distributable dependency, record:

1. Dependency name and version.
2. Official project and source URLs.
3. Exact licence.
4. Whether ATK Player links it statically or dynamically.
5. Which runtime files will be redistributed.
6. Which licence texts or notices must accompany the binary distribution.
7. Any optional features that materially change the dependency's licence configuration.
8. How the dependency is obtained and version-pinned.

A dependency should not be added to a public ATK Player binary distribution without documenting its distribution configuration here.
