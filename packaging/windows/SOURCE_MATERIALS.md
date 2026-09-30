# Dependency notices and corresponding source

This Windows distribution includes the source and license materials locally under
`licenses/dependencies`. No purchase, account, or source-request email is needed.
`SHA256SUMS.txt` identifies the files; `DEPENDENCY-SOURCE-LOCK.json` records upstream
URLs, versions, revisions and archive digests. Sources are not application plugins
and their inclusion does not enable any additional codec or Qt module.

## Qt 6.9.3

`sources/qtbase-6.9.3-source.zip`, `qtmultimedia-6.9.3.tar.xz` and
`qtsvg-6.9.3.tar.xz` contain the unmodified upstream source code, including embedded
third-party code and build scripts. Their `.tag` files identify the revisions.
Installed-kit SPDX inventories and `config_qt*.opt` / `.summary` files accompany
the sources. The notices retain upstream attribution records, linked license and
copyright files, and source copyright/license comment blocks. This is a superset:
build tools, tests and unselected implementations are also described.

ATK Player uses the LGPL v3 option for its deployed Qt libraries. LGPL v3 and the
GPL v3 text it incorporates are supplied. Qt's static Windows entry-point code
uses its BSD-3-Clause option; copyright and terms are retained in the qtbase
notices and source. Third-party code retains its own terms, including FreeType's
permissive FTL option. A GPL alternative in the reference corpus does not mean
the corresponding GPL-only implementation is in the application.

To rebuild Qt, extract the archives as sibling `qtbase`, `qtmultimedia` and
`qtsvg` source directories. Use Qt's Windows MSVC/Ninja source-build instructions,
the supplied configuration summaries/options, and the open-source LGPL selection.
Build/install qtbase first, then configure each extra module with the installed
Qt's `qt-configure-module`. Replace the separate compatible Qt DLLs/plugins in a
writable portable directory. The installer imposes no signature check on replaced
libraries. For Qt's static entry point, rebuild/relink ATK Player from the included
MIT application source against your rebuilt Qt; no proprietary object files are
required. Use the same ABI/architecture and the appropriate Debug/Release kit.

## FFmpeg 9.0.1, vcpkg port revision 1

`sources/FFmpeg-9.0.1-patched-source.zip` contains upstream `n9.0.1` with the
14 patches from the pinned vcpkg port applied in port order.
`sources/FFmpeg-vcpkg-port.zip` preserves that port and all its patches.
`sources/vcpkg-build-source.zip` includes the pinned build system and helper ports.
`FFmpeg-BUILD-CONFIGURATION.json` records the actual distributed five DLL hashes,
configuration strings and LGPL license results. Absolute paths in configure
strings describe the original build environment and must be adjusted locally.

For the standard automated build, clone the public Microsoft vcpkg repository
and check out the exact baseline recorded in DEPENDENCY-SOURCE-LOCK.json; manifest
version resolution needs its Git history. The included source-only vcpkg archive
preserves the corresponding build scripts/ports but is not a Git checkout.
Rebuild using that baseline and ATK `vcpkg.json` with the dynamic
`x64-windows` triplet in an x64 MSVC environment, then configure/build ATK using
its supplied CMake presets. vcpkg bootstrapping may download host build tools.
Alternatively build the supplied patched FFmpeg source using the recorded
configure line, adjusting host paths and providing the MSVC/Windows SDK/MSYS
build tools referenced by the port. The manifest's default features remain disabled and no GPL/nonfree/x264/x265/
fdk-aac features may be selected for this distribution. Replace the five matching
FFmpeg DLLs in a writable portable directory or rebuild ATK against a changed ABI.
Full upstream licenses and source-wide comment notices are retained; notices for
unselected source files do not change the configured LGPL runtime's license.

## WiX Toolset 4.0.6

`sources/WiX-4.0.6-source.zip` contains the matching upstream source, build scripts,
utility/UI custom actions, native helper libraries and UI resources. `WiX-MS-RL.txt`
and `WiX-NOTICES.txt` retain its license and contributor notices. These components
are embedded in the MSI; upstream WiX DLLs are not signed with an ATK certificate.
Follow that source tree's build instructions to rebuild the native UI/utility
custom actions; ATK's
`Package.wxs` and packaging script describe how to rebuild the installer. No WiX
source-code modifications are made by ATK. Full WiX tooling rebuilds may need
upstream tool/dependency restoration; prebuilt Microsoft merge-module DLLs and
test binaries are deliberately not part of this source-only distribution.

## Source-only archive exclusions

The qtbase, WiX and vcpkg archives are source-only repacks of checksum-verified
upstream archives. `SOURCE-EXCLUSIONS.json` lists every omitted member. Prebuilt
binaries (including unrelated Microsoft DIFxApp/mergemod DLLs in WiX), unused font
fixtures and test signing/certificate material are excluded. No deployed runtime
file or C/C++ build source is removed or modified. Qt Windows library builds do
not require the excluded documentation/WebAssembly/test fonts; disable tests and
examples when rebuilding these runtime libraries. This keeps unused proprietary
or unresolved binary fixtures out of the new materials. Original archive hashes
and source revisions remain recorded in the source lock.

## ATK Player and release handling

`sources/ATK-Player-source.zip` snapshots the tracked current working source used
by the packaging build, including any reviewed edits not yet committed. It is
provided under the project's MIT License. Refer to `docs/BUILDING.md`; package
builds use `-DATK_VERSION_SUFFIX=<selected suffix>` and `RelWithDebInfo`.
Record the public commit and CI run separately when publishing an official release.

Source-bearing package builds reject `-SkipBuild`: the packaging entry point must
build the executable against the current source before creating its source archive.
Dependency materials are regenerated in a fresh directory and replace the prior
set only after preparation succeeds, so removed or renamed materials are not carried
into later installers.

These materials travel inside the MSI and any portable archive made from the same
install stage, so source availability does not depend on publishing an additional
release asset. The notice corpus must be regenerated and reviewed when dependency
versions/features change. This does not assert that previously published packages
already included these materials, or that SignPath has approved the project.
