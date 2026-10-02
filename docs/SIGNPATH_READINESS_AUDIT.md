# ATK Player — SignPath Readiness Audit

Audit date: 2026-09-28. Branch: `signpath-readiness`, based on `d96d2d5`.
Scope: Windows x64 distribution readiness. The original September 28 audit was local only. On September 30 the maintainer authorized publishing and merging the readiness changes and updating public release-page policy links. Signing integration and application submission remain separate steps.

## 0.3.0 Addendum (2026-10-01)

This audit was performed for **0.2.1**. Its sections below still describe that
release and were **not** re-performed in full for 0.3.0. The changes that
matter for distribution since then are:

- **Version.** The root CMake version is 0.3.0. It feeds the EXE, the MSI
  ProductVersion and the artifact names (`ATK-Player-0.3.0-Windows-x64.msi`).
- **New runtime dependency: zlib 1.3.2** (vcpkg `zlib` 1.3.2#2, zlib license,
  permissive, not GPL/nonfree).
  - It is enabled through the vcpkg `ffmpeg[zlib]` feature, so FFmpeg can
    decode PNG, EXR and deflate TIFF still images.
  - The distribution now contains **six** FFmpeg-side DLLs: the five below
    plus `z.dll`.
  - Its licence ships as `licenses/zlib.txt` and in `THIRD_PARTY_NOTICES.txt`.
    The zlib licence does not require source distribution.
  - `build-installer.ps1` and `verify-installer.ps1` require `z.dll` and
    `zlib.txt` in the stage and the MSI.
- **FFmpeg configuration.** It now includes `--enable-zlib`. No other feature
  changed. GPL, nonfree, version3, x264, x265 and fdk-aac remain excluded.
- **Not yet covered.**
  - `FFmpeg-BUILD-CONFIGURATION.json` records the five FFmpeg libraries only;
    `z.dll` is not in that record.
  - The SignPath-specific checks in sections 11–13 have not been repeated
    for 0.3.0.

Local 0.3.0 package evidence: `build-installer.ps1 -VersionSuffix ""` passed.
That run covered configure, build, the full CTest suite (32/32), the
dependency-material tests, the clean-PATH smoke test and MSI verification.

## Overall Status

**READY WITH ACTIONS** — the identified dependency notice and corresponding-source gaps are addressed by the new packaging path. Icon ownership and GitHub MFA were confirmed by David Shepstone on September 30. Public policy links are included in the authorized publication follow-up. Foundation approval/account controls, clean-install acceptance and official package CI provenance remain outstanding. Previously published packages are not retroactively corrected.

Target: [current SignPath Foundation terms](https://signpath.org/terms.html), checked during the audit. The `/terms` URL returned HTTP 503; the official `.html` page was accessible and labels the code of conduct a draft. Primary license/source evidence: [Qt source archives](https://download.qt.io/archive/qt/6.9/6.9.3/submodules/), [FFmpeg legal guidance](https://ffmpeg.org/legal.html), and [WiX 4.0.6 license](https://github.com/wixtoolset/wix/blob/v4.0.6/LICENSE.TXT). Repository, toolchain, source/archive and local artifact evidence are distinguished below.

## 1. Project Eligibility

ATK is MIT-licensed, publicly sourced, maintained and documented as a Windows animation/media review application. David Shepstone is the copyright holder and maintainer. No commercial-only Qt module or proprietary project runtime was found. Microsoft runtime/API dependencies remain system prerequisites.

`PRIVACY.md` covers local storage/media and the optional loopback API. `ApiServer.cpp` binds to localhost; the API defaults off. User-triggered links are external interactions. This audit does not establish network behavior through observation or certify the absence of malware. Foundation acceptance/reputation and account security require personal verification.

## 2. Windows Distribution Contents

Classification: A = project MIT source/output; B = upstream OSS; C = Windows/system/runtime; D = unresolved provenance; E = confirmed proprietary non-system component.

Install rules retain the original EXE/icon, five FFmpeg DLLs, six Qt DLLs, 12 Qt plugins, `qt.conf` and four base license/notice files. Packaging now additionally installs `licenses/dependencies`: source archives, GPL v3/MS-RL, component notices, configuration/SBOM records, source lock/exclusions and SHA-256 manifest. WiX harvests every staged file. A portable ZIP made from that stage contains the same files.

| Component | Version | Source | License | Redistributed | License/Notice Present | SignPath Status |
|---|---|---|---|---|---|---|
| A: ATKPlayer.exe and ATK core/UI | 0.2.1 | Repository | MIT | MSI / portable | MIT; current tracked source snapshot | PASS |
| A: project icon, embedded PNG/SVG, installer BMP/RTF | Repository revision | assets/icons and packaging/windows/assets | Documented as project MIT; rasterized font text | App / installer | MIT; source assets | Icon ownership confirmed by David Shepstone, September 30, 2026; AI-assisted designs |
| A: qt.conf | Generated | Qt deployment | Project configuration | MSI / portable | Configuration included | PASS |
| B: Qt6Core/Gui/Widgets/Network/Multimedia/Svg.dll | 6.9.3 | Official Qt MSVC x64 binaries | LGPL-3.0 option plus embedded licenses | MSI / portable | LGPL/GPL texts, source-wide notices, matching source and build/SBOM records | PASS local coverage |
| B: plugins/platforms/qwindows.dll | 6.9.3 | Qt base | LGPL-3.0 and permissive Wintab header | MSI / portable | Qt corpus/source | PASS |
| B: plugins/multimedia/windowsmediaplugin.dll | 6.9.3 | Qt Multimedia | LGPL-3.0 option | MSI / portable | Qt corpus/source | PASS |
| B: plugins/imageformats/qgif.dll, qico.dll, qjpeg.dll, qsvg.dll | 6.9.3 | Qt base/SVG | LGPL-3.0 plus embedded JPEG/SVG terms | MSI / portable | Qt corpus/source, IJG/FreeType acknowledgement | PASS |
| B: plugins/iconengines/qsvgicon.dll | 6.9.3 | Qt SVG | LGPL-3.0 option | MSI / portable | Qt corpus/source | PASS |
| B: plugins/generic/qtuiotouchplugin.dll | 6.9.3 | Qt base | LGPL-3.0 option | MSI / portable | Qt corpus/source | PASS |
| B: plugins/networkinformation/qnetworklistmanager.dll | 6.9.3 | Qt Network | LGPL-3.0 option | MSI / portable | Qt corpus/source | PASS |
| B: plugins/styles/qmodernwindowsstyle.dll | 6.9.3 | Qt Widgets | LGPL-3.0 option | MSI / portable | Qt corpus/source | PASS |
| B: plugins/tls/qcertonlybackend.dll and qschannelbackend.dll | 6.9.3 | Qt Network | LGPL-3.0 option | MSI / portable | Qt corpus/source | PASS; Windows TLS |
| B: avcodec-63, avformat-63, avutil-61, swresample-7, swscale-10 DLLs | FFmpeg 9.0.1, port #1 | Pinned patched vcpkg build | LGPL-2.1-or-later | MSI / portable | LGPL, source-wide notices, actual DLL config/license/hash, matching patched source/port | PASS local coverage |
| B: Qt static EntryPointPrivate code | 6.9.3 | Qt entry-point library | BSD-3-Clause option | Embedded EXE | Copyright, BSD text, Qt/ATK rebuild sources | PASS |
| B: WiX utility/UI custom actions and resources | 4.0.6 | NuGet upstream extensions | MS-RL | Embedded MSI | MS-RL, contributor notices, native helper/CA/UI source | PASS local coverage |
| B: Qt embedded third-party code | See supplemental table | Matching Qt source | Component-specific permissive/copyleft alternatives | Inside selected Qt modules | Complete source attribution/license corpus; candidate inventory below | PASS preservation; not every source candidate is linked |
| B: vcpkg source/build helper ports | Pinned baseline; bin2c 9.0.1, CMake helpers 2025-08-07/2025-05-29/2024-04-03, pkgconf 3.0.3, Meson 1.9.0 | microsoft/vcpkg | MIT/helpers; Meson Apache-2.0; upstream pkgconf terms | Source only; no helper executable | Matching build-system source and license files | PASS source distribution |
| C: VC++ runtime and Windows API libraries | Installed platform-dependent | Microsoft / OS | System/runtime terms | Imported, not copied into runtime payload | Prerequisite documented | PASS |
| C/B: MSVC/SDK/CMake/Ninja/Python/.NET/WiX CLI | Host versions | Toolchain/bootstrap | Respective tool terms | No tool executables | Build-only; WiX embedded portions treated above | PASS |
| A: Maya/Python/Harmony integrations | Repository revision | integrations/ | MIT | Not installed as runtime integration files; available in ATK source snapshot | Root MIT | PASS; no Maya/Harmony host runtime |
| C/B: Segoe UI / DejaVu font files | Host-dependent | Bitmap generator inputs | Respective font terms | No font files in runtime or new source-only materials; rasterized text in installer | Generator identifies font inputs | Font terms remain separate from confirmed icon ownership |

No extra runtime codec, OpenSSL DLL, MSVC redistributable installer, Qt QML/translations/Test module, avfilter/avdevice DLL, ffmpeg/ffprobe tool, test EXE or debug/development artifact is deployed. Qt's own FFmpeg 7.1.1 backend remains excluded.

## 3. Proprietary/Unknown Component Audit

No confirmed proprietary non-system component is in the deployed application. David Shepstone confirmed that the project icons are his own designs made with AI assistance on September 30, 2026; installer branding is derived by the repository bitmap generator. Preparing source delivery revealed **unused Microsoft DIFxApp and mergemod binaries inside WiX's full upstream archive**, plus unrelated test binaries, font fixtures and test signing material in upstream source trees. These are not part of ATK's runtime or native installer custom actions. They are deliberately excluded from the newly added source-only repacks, with every omitted path recorded in `SOURCE-EXCLUSIONS.json`; no existing deployed file is silently removed. Original checksummed archives remain build-cache inputs only.

Qt/FFmpeg source-wide notices deliberately preserve a superset, including test/build-only/unselected source. GPL license alternatives/notices in that corpus do not enable GPL code or change the runtime configuration. Complete source, upstream attribution records and linked copyright/license text are included. No license names/attributions were invented.

## 4. FFmpeg Configuration Audit

Baseline `45f9f39362a4c52e2b1fbe57b7e649db7f3d96d4` resolves FFmpeg 9.0.1 port revision 1. Upstream source `n9.0.1` is verified against the pinned port's SHA-512 and the source lock's SHA-256. The generator applies all **14** pinned patches in order. The resulting **10,422 source files match the actual local vcpkg source tree byte-for-byte**, with no extra local files.

Selected features remain avcodec, avformat, swresample, swscale, avdevice, ffmpeg and ffprobe with defaults disabled. ffmpeg transitively enables avfilter for tests; avfilter/avdevice/tools are not installed. `--disable-autodetect` prevents ambient dependency detection. Actual queries on all five distributed libraries report LGPL 2.1-or-later, exclude enable-gpl/nonfree/version3, and explicitly disable libx264/libx265/libfdk-aac. Packaging records their hashes/configuration/license and fails on prohibited configuration. No dependency version or feature was changed.

Matching patched source, complete port/patches, baseline build-system source and rebuild instructions accompany the binaries locally. FFmpeg notices preserve upstream license documents and source-wide copyright/license comments. Media Foundation H.264/AAC support does not imply x264. Patent questions are separate from this license/source audit.

## 5. Qt Distribution Audit

Qt 6.9.3 source archives were verified against Qt's published SHA-256 values. `.tag` revisions are qtbase `be09b211db70e1b0155d05c18668ad76e7e3df51`, qtmultimedia `0bbd3bc533aae9079e94d9c903a509239514aea7`, and qtsvg `84a5bad7715ac1a05043014a0f81f636117290da`. qtbase/SVG revisions match the installed SPDX source identity; Multimedia's installed inventory identifies version 6.9.3. Installed-kit SBOM/options/summaries are preserved.

The kit uses bundled zlib, PCRE2, FreeType, HarfBuzz, JPEG and PNG; ICU is disabled. The new corpus contains upstream attribution records, referenced copyright/license files, module license texts and source comments, including BSD entry-point code. LGPL v3 and its incorporated GPL v3 text are included. IJG and FreeType acknowledgements are explicit. FreeType uses its permissive FTL alternative. Rebuild/relink instructions and actual ATK MIT source are supplied, including the static entry point.

Install deployment flags/module selection are unchanged: no translations, compiler runtime, system D3D/DXC compiler, software OpenGL or ffmpegmediaplugin. Packaging now rejects a different Qt version or additional Qt runtime module without a new source/notice audit. Original Windows runtime libraries/plugins remain dynamically replaceable. Source-only Qtbase packaging excludes unused font/binary/signing fixtures, not library source code; all exclusions are recorded.

## 6. EXE Metadata

| Field | Expected / locally verified value |
|---|---|
| Executable / OriginalFilename | ATKPlayer.exe |
| ProductName / FileDescription | ATK Player |
| InternalName | ATKPlayer |
| CompanyName | David Shepstone |
| LegalCopyright | Copyright (c) 2026 David Shepstone |
| FileVersion / fixed file version | 0.2.1.0 / 0,2,1,0 |
| ProductVersion / fixed product version | 0.2.1 / 0,2,1,0 |
| Resource language/codepage | English US 0409 / Unicode 1200 |

The original audit corrected the ProductVersion string to numeric CMake version so it matches MSI. Dev/RC suffixes still appear in application display/log metadata. This is the only runtime resource metadata correction; no functionality, public version, QSettings identity or UI resource was changed.

## 7. MSI Metadata

Name/display name ATK Player; Manufacturer David Shepstone; ProductVersion 0.2.1; x64; perMachine; language 1033; Program Files/ATK Player. UpgradeCode remains `{6E41AAE8-13C4-4D46-AB5B-7F04E92E9B76}`; ProductCode is automatically generated per build. Embedded cabinet/high compression and same-numeric-version upgrade behavior remain unchanged. Description/comments/filenames may carry the suffix.

Start Menu, Installed Apps registration, .atkproj association and standard uninstall behavior are unchanged. The existing MIT license dialog now discloses the dependency licenses/source location and LGPL modification rights. This changes installer text, not application UI or the installation flow. VC++ remains a separately installed prerequisite.

## 8. Version Consistency

Root CMake `project(ATKPlayer VERSION 0.2.1)` is authoritative. Version.h, application display and EXE resources derive from it. Packaging derives MSI/display/checksum names, checks the vcpkg label, tag, configured suffix and staged EXE product/publisher/version. Mismatched numeric tag and cached suffix are rejected. The public version stays 0.2.1.

No portable creation step was added to tracked CI: a ZIP made from the stage now includes all materials, and a local audit ZIP is verified. Official portable build/provenance automation remains an action if that artifact is claimed as CI-produced. Release workflow was not changed or dispatched; no GitHub Release was created.

## 9. Code Signing Policy

The policy accurately states pending application/signing status, prospective attribution, David's Author/Committer/Reviewer/Approver roles, AI-tool responsibility, MFA/manual approval and privacy/security links. Only project artifacts are eligible for future signing; upstream binaries must not be signed with the project subscription. Account/approval/origin enforcement remains future work after Foundation acceptance.

## 10. README Requirement

README links [Code signing policy](../CODE_SIGNING_POLICY.md) beside the preserved unsigned-build warning and states pending approval/signed releases. It links this audit and the license documentation. The maintainer authorized adding the same policy link and pending-signing notice to the existing public GitHub Release pages on September 30; release binaries and checksums are preserved.

## 11. SignPath Requirements Checklist

| Requirement | Status | Evidence / action |
|---|---|---|
| OSS licensing / dependency notices/source | PASS | New materials; actual local artifacts checked |
| No proprietary project component | PASS for reviewed inventory | No confirmed proprietary runtime component; maintainer-confirmed project icons; documented source-fixture exclusions |
| Maintained/released/documented | PASS | Public source, README and existing artifacts |
| Project control/reputation | MANUAL VERIFICATION | David / Foundation assessment |
| No malware/exploitation tooling | PASS | Audited project purpose/build paths, not certification |
| Privacy / system-change disclosure / uninstall | MANUAL VERIFICATION | Existing policy/MSI; network and clean-install observation outstanding |
| Repository MFA | PASS — maintainer confirmation | David confirmed MFA enabled with passkeys or another second factor on September 30; the API token does not expose this account setting |
| SignPath MFA | ACTION REQUIRED | Account setup after acceptance |
| Named roles / policy / attribution / privacy link | PASS | README + policy; prospective attribution |
| Release-page policy link | Publication follow-up | Authorized update adds the policy link and pending-signing notice to v0.2.0 and v0.2.1 release descriptions |
| Consistent metadata | PASS | Numeric version/resource and package guards |
| Artifact restrictions / trusted build origin | ACTION REQUIRED | Configure/verify with Foundation later |
| Manual signing approval enforcement | ACTION REQUIRED | Policy present; account controls later |
| Violation cooperation | PASS | Maintainer commitment |

## 12. Remaining Manual Steps

David confirmed icon ownership and GitHub MFA and authorized publishing/merging these changes and adding release-page policy links on September 30. After publication, submit the application; configure SignPath MFA/roles/metadata/trusted-origin/manual approval if accepted; personally approve future signing requests. Confirm the larger package's clean-install/upgrade/uninstall and network behavior, then record an official CI run/payload before publication. Publication and merge were explicitly authorized by the maintainer on September 30; this audit does not authorize application submission or signing.

## 13. Recommendation

Submit the application with this audit: local dependency notice/source coverage is now implemented and checked. The documented icon ownership and GitHub MFA confirmations resolve the personal prerequisites for the application. After the public policy links and readiness changes are published, proceed with the application; Foundation acceptance and signing/release controls remain separate actions. This does not claim retroactive compliance for old downloads or SignPath approval.

### September 30 application preparation

The maintainer confirmed ownership of his AI-assisted icon designs and enabled GitHub MFA. The September 30 review configured and built Windows Debug and Release: all 31 tests passed in each (102.94 and 78.94 seconds respectively). All six dependency-material regression tests passed. The existing local MSI passed metadata checks; its extracted payload matched all 59 staged files and dependency-material checksums validated. No new binary release is implied by publishing these source changes.

### Original application-change verification and local validation

September 30 PR review follow-up: source-bearing packaging now rejects `-SkipBuild`
to prevent bundling current source with an older executable. Materials are prepared
in a fresh directory and replace the previous set only after successful preparation;
stale outputs cannot enter the new manifest. Nine dependency-material tests pass,
including stale-output removal, preservation after failed preparation, and output
directory safety. These tests also run in both Windows CI jobs.

Comparison against origin/main shows **no application C++ file, UI asset/resource, core/root CMake configuration, dependency feature, integration or runtime linkage change**. The only source-tree differences are src/app/ATKPlayer.rc.in (numeric product-version correction) and the install-only materials block in src/app/CMakeLists.txt. Packaging adds source/notices and guards; installer license text discloses third-party ownership. Existing unrelated untracked files were left untouched and excluded from the source snapshot.

The follow-up reconfigured/built Debug and Release. Release passed 31/31 (102.80 seconds). Debug initially failed one comparison assertion during parallel validation: offsetControlsUseExactTransientTimeAndReanchor reported frame 0 instead of 29. Logs were preserved; **the complete isolated Debug rerun passed 31/31 (102.79 seconds) without code or test changes**. The failure's cause was not established. These automated results support the source comparison; David's manual UI/media verification is still useful.

Dependency-material regression tests cover corrupt-cache rejection, path traversal, deterministic source-byte preservation, notice text retention and recorded binary/font/key exclusions. All six dependency-material regression tests passed. The final packaging configure/build/complete CTest run passed 31/31 (82.43 seconds). No installed system version was replaced. The local development executable is build/windows-debug/bin/ATKPlayer.exe (0.2.1-dev display version). It was opened visibly and remained running after the startup check (PID 97472); visual/media acceptance remains David's manual check.


### Final artifact and unchanged-runtime evidence

| Verification | Actual result |
|---|---|
| Source/assets/integrations/runtime config/workflows vs origin/main | 163 files byte-identical; only two intended source-tree changes |
| Existing stage EXE/DLLs/plugins/icon/qt.conf vs previous audited build | All 26 files byte-identical |
| Generated dependency materials | 29 files installed; every SHA256SUMS entry verified |
| Final MSI extraction | All 59 paths and SHA-256 values match stage |
| New local portable ZIP | All 59 files match stage; independent archive SHA-256 verified |
| Source-only archive exclusions | 245 Qtbase, 49 WiX, 2 vcpkg members documented; omitted binary/font/signing fixtures absent; XML .lib build templates retained |
| FFmpeg staged-library hash/config/license | Matches build evidence for all five; GPL/nonfree/x264/x265/fdk-aac remain excluded |
| ATK source snapshot | App source files match working source; unrelated untracked files excluded |
| EXE / MSI signatures | NotSigned; no signing or publication |
| Script parsing / git diff --check | PASS |

Local MSI size: 136,970,240 bytes (larger because source is bundled). SHA-256: `d5047554206c6e662571776160c7f073db4e252ad78a617d370aeb09ae203ab8`. Local audit portable SHA-256: `1ddc244c7672c6a3473a6e173dc9cce8540df9f612c3b33eee13113ad8bad869`. These are audit outputs, not published release checksums.

The new Qt guard was corrected to normalize Windows resource `6.9.3.0` to the locked `6.9.3`; no Qt binary was changed. Existing WiX WIX1077 for WixShellExecTarget remains and needs clean-install acceptance. No GUI smoke launch of the packaged release, clean-machine install, network capture, official CI dispatch or Foundation verification was performed. Detailed local evidence is ignored output under build/signpath-audit. No merge/commit/push or installed system app replacement was performed.

### Supplemental Qt embedded-component inventory

The following inventory is derived from the installed official Qt 6.9.3 binary SPDX files, filtered to deployed module attribution leads and bundled libraries. Source/version fields are upstream SBOM values, including unknown versions and non-release revision identifiers. Entries are **B**, except unresolved source/license details that remain **D** for confirmation. Each enters through upstream Qt binaries; candidate inclusion must be reconciled with actual build/link configuration. No separate copies of these libraries are installed. Individual attributions, referenced license/copyright files and source-wide notices are now supplied in licenses/dependencies. The corpus includes unselected/build-only code, so this remains a candidate inventory, not a claim that every row is linked.

| Component | Version | Source | License | Redistributed | License/Notice Present | SignPath Status |
|---|---|---|---|---|---|---|
| BundledPcre2_Attribution_pcre2-sljit | 10.46 | https://github.com/PCRE2Project/pcre2/releases/download/pcre2-10.46/pcre2-10.46.tar.bz2 | BSD-2-Clause | Within Qt / candidate | Included in Qt corpus | PASS preservation; candidate |
| BundledPcre2 | 10.46 | https://github.com/PCRE2Project/pcre2/releases/download/pcre2-10.46/pcre2-10.46.tar.bz2 | LicenseRef-BSD-3-Clause-with-PCRE2-Binary-Like-Packages-Exception | Within Qt / candidate | Included in Qt corpus | PASS preservation; candidate |
| BundledZLIB | 1.3.1 | https://github.com/madler/zlib/releases/download/v1.3.1/zlib-1.3.1.tar.gz | Zlib | Within Qt / candidate | Included in Qt corpus | PASS preservation; candidate |
| Core_Attribution_unicode-character-database | 34 | https://www.unicode.org/ucd/ | Unicode-3.0 | Within Qt / candidate | Included in Qt corpus | PASS preservation; candidate |
| Core_Attribution_unicode-cldr | v47 | https://cldr.unicode.org/ | Unicode-3.0 | Within Qt / candidate | Included in Qt corpus | PASS preservation; candidate |
| Core_Attribution_siphash | unknown | https://raw.githubusercontent.com/veorq/SipHash/adcbf09b1684a718f594faa650ffc56bacdb0777/siphash24.c | CC0-1.0 | Within Qt / candidate | Included in Qt corpus | PASS preservation; candidate |
| Core_Attribution_blake2 | ed1974ea83433eba7b2d95c5dcd9ac33cb847913 | https://github.com/BLAKE2/BLAKE2/tree/ed1974ea83433eba7b2d95c5dcd9ac33cb847913 | CC0-1.0 OR Apache-2.0 | Within Qt / candidate | Included in Qt corpus | PASS preservation; candidate |
| Core_Attribution_md4 | unknown | Qt source attribution; origin needs confirmation | CC0-1.0 | Within Qt / candidate | Included in Qt corpus | PASS preservation; candidate |
| Core_Attribution_md5 | unknown | Qt source attribution; origin needs confirmation | CC0-1.0 | Within Qt / candidate | Included in Qt corpus | PASS preservation; candidate |
| Core_Attribution_sha1 | unknown | http://www.dominik-reichl.de/projects/csha1/ | LicenseRef-SHA1-Public-Domain | Within Qt / candidate | Included in Qt corpus | PASS preservation; candidate |
| Core_Attribution_sha3_endian | 4b9e13ead2c5b5e41ca27c65de4dd69ae0bac228 | Qt source attribution; origin needs confirmation | BSD-2-Clause | Within Qt / candidate | Included in Qt corpus | PASS preservation; candidate |
| Core_Attribution_sha3_keccak | 3.2 | Qt source attribution; origin needs confirmation | CC0-1.0 | Within Qt / candidate | Included in Qt corpus | PASS preservation; candidate |
| Core_Attribution_rfc6234 | unknown | Qt source attribution; origin needs confirmation | BSD-3-Clause | Within Qt / candidate | Included in Qt corpus | PASS preservation; candidate |
| Core_Attribution_tinycbor | 0.6.1 | https://github.com/intel/tinycbor/archive/v0.6.1/tinycbor-0.6.1.tar.gz | MIT | Within Qt / candidate | Included in Qt corpus | PASS preservation; candidate |
| Core_Attribution_doubleconversion | 3.3.1 | https://github.com/google/double-conversion/releases/tag/v3.3.1 | BSD-3-Clause | Within Qt / candidate | Included in Qt corpus | PASS preservation; candidate |
| Core_Attribution_easing | unknown | http://robertpenner.com/easing/ | BSD-3-Clause | Within Qt / candidate | Included in Qt corpus | PASS preservation; candidate |
| Core_Attribution_tika-mimetypes | 5101bc7fb090ed7deffe56837d7633c9485a1e5d | https://github.com/apache/tika/blob/5101bc7fb090ed7deffe56837d7633c9485a1e5d/tika-core/src/main/resources/org/apache/tika/mime/tika-mimetypes.xml | Apache-2.0 | Within Qt / candidate | Included in Qt corpus | PASS preservation; candidate |
| BundledLibpng | 1.6.50 | https://download.sourceforge.net/libpng/libpng-1.6.50.tar.xz | Libpng AND libpng-2.0 | Within Qt / candidate | Included in Qt corpus | PASS preservation; candidate |
| BundledLibjpeg16bits | 3.1.2 | https://github.com/libjpeg-turbo/libjpeg-turbo/releases/download/3.1.2/libjpeg-turbo-3.1.2.tar.gz | IJG AND BSD-3-Clause | Within Qt / candidate | Included in Qt corpus | PASS preservation; candidate |
| BundledLibjpeg12bits | 3.1.2 | https://github.com/libjpeg-turbo/libjpeg-turbo/releases/download/3.1.2/libjpeg-turbo-3.1.2.tar.gz | IJG AND BSD-3-Clause | Within Qt / candidate | Included in Qt corpus | PASS preservation; candidate |
| BundledLibjpeg | 3.1.2 | https://github.com/libjpeg-turbo/libjpeg-turbo/releases/download/3.1.2/libjpeg-turbo-3.1.2.tar.gz | IJG AND BSD-3-Clause | Within Qt / candidate | Included in Qt corpus | PASS preservation; candidate |
| BundledFreetype_Attribution_freetype-zlib | 2.14.1 | https://download.savannah.gnu.org/releases/freetype/freetype-2.14.1.tar.gz | Zlib | Within Qt / candidate | Included in Qt corpus | PASS preservation; candidate |
| BundledFreetype_Attribution_freetype-bdf | 2.14.1 | https://download.savannah.gnu.org/releases/freetype/freetype-2.14.1.tar.gz | MIT | Within Qt / candidate | Included in Qt corpus | PASS preservation; candidate |
| BundledFreetype_Attribution_freetype-pcf | 2.14.1 | https://download.savannah.gnu.org/releases/freetype/freetype-2.14.1.tar.gz | MIT AND MIT-open-group | Within Qt / candidate | Included in Qt corpus | PASS preservation; candidate |
| BundledFreetype | 2.14.1 | https://download.savannah.gnu.org/releases/freetype/freetype-2.14.1.tar.gz | FTL OR GPL-2.0-only | Within Qt / candidate | Included in Qt corpus | PASS preservation; candidate |
| BundledHarfbuzz | 11.5.0 | https://github.com/harfbuzz/harfbuzz/releases/tag/11.5.0 | MIT | Within Qt / candidate | Included in Qt corpus | PASS preservation; candidate |
| Network_Attribution_zlib | 1.3.1 | https://github.com/madler/zlib/releases/download/v1.3.1/zlib-1.3.1.tar.gz | Zlib | Within Qt / candidate | Included in Qt corpus | PASS preservation; candidate |
| Network_Attribution_psl-data | 2025-06-16_09-45-02_UTC | https://publicsuffix.org/list/public_suffix_list.dat | MPL-2.0 | Within Qt / candidate | Included in Qt corpus | PASS preservation; candidate |
| Network_Attribution_libpsl | 664f3dc85259ec65e30248a61fa1c45b7b0e4c3f | https://github.com/rockdaboot/libpsl | BSD-3-Clause | Within Qt / candidate | Included in Qt corpus | PASS preservation; candidate |
| Gui_Attribution_rhi-miniengine-d3d12-mipmap | 0aa79bad78992da0b6a8279ddb9002c1753cb849 | https://github.com/microsoft/DirectX-Graphics-Samples | MIT | Within Qt / candidate | Included in Qt corpus | PASS preservation; candidate |
| Gui_Attribution_opengl-headers | Revision 27684 | https://www.khronos.org/ | MIT | Within Qt / candidate | Included in Qt corpus | PASS preservation; candidate |
| Gui_Attribution_opengl-es2-headers | Revision 27673 | https://www.khronos.org/ | MIT | Within Qt / candidate | Included in Qt corpus | PASS preservation; candidate |
| Gui_Attribution_grayraster | unknown | http://www.freetype.org | FTL OR GPL-2.0-only | Within Qt / candidate | Included in Qt corpus | PASS preservation; candidate |
| Gui_Attribution_smooth-scaling-algorithm | unknown | Qt source attribution; origin needs confirmation | BSD-2-Clause AND Imlib2 | Within Qt / candidate | Included in Qt corpus | PASS preservation; candidate |
| Gui_Attribution_xserverhelper | unknown | https://www.x.org/ | X11 AND HPND | Within Qt / candidate | Included in Qt corpus | PASS preservation; candidate |
| Gui_Attribution_aglfn | 1.7 | https://github.com/adobe-type-tools/agl-aglfn | BSD-3-Clause | Within Qt / candidate | Included in Qt corpus | PASS preservation; candidate |
| Gui_Attribution_vulkanmemoryallocator | 3.1.0 | https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator | MIT | Within Qt / candidate | Included in Qt corpus | PASS preservation; candidate |
| Gui_Attribution_webgradients | unknown | https://webgradients.com/ | MIT | Within Qt / candidate | Included in Qt corpus | PASS preservation; candidate |
| Gui_Attribution_icc-srgb-color-profile | unknown | http://www.color.org/ | LicenseRef-ICC-License | Within Qt / candidate | Included in Qt corpus | PASS preservation; candidate |
| Gui_Attribution_d3d12memoryallocator | f128d39b7a95b4235bd228d231646278dc6c24b2 | https://github.com/GPUOpen-LibrariesAndSDKs/D3D12MemoryAllocator | MIT | Within Qt / candidate | Included in Qt corpus | PASS preservation; candidate |
| Gui_Attribution_md4c | 0.5.2 | https://github.com/mity/md4c/releases/tag/release-0.5.2 | MIT | Within Qt / candidate | Included in Qt corpus | PASS preservation; candidate |
| Gui_Attribution_zlib | 1.3.1 | https://github.com/madler/zlib/releases/download/v1.3.1/zlib-1.3.1.tar.gz | Zlib | Within Qt / candidate | Included in Qt corpus | PASS preservation; candidate |
| Gui_Attribution_vulkan-xml-spec | 1.3.223 | https://www.khronos.org/ | Apache-2.0 OR MIT | Within Qt / candidate | Included in Qt corpus | PASS preservation; candidate |
| QWindowsIntegrationPlugin_Attribution_wintab | unknown | Qt source attribution; origin needs confirmation | LicenseRef-Lcs-Telegraphics | Within Qt / candidate | Included in Qt corpus | PASS preservation; candidate |
| Svg_Attribution_xsvg | unknown | Qt source attribution; origin needs confirmation | HPND-sell-variant | Within Qt / candidate | Included in Qt corpus | PASS preservation; candidate |
| BundledResonanceAudio_Attribution_pffft | 02fe7715a5bf8bfd914681c53429600f94e0f536 | https://bitbucket.org/jpommier/pffft/get/02fe7715a5bf.zip | BSD-3-Clause | Within Qt / candidate | Included in Qt corpus | PASS preservation; candidate |
| BundledResonanceAudio_Attribution_eigen | 3.4.0 | https://gitlab.com/libeigen/eigen/-/archive/3.4.0/eigen-3.4.0.tar.bz2 | MPL-2.0 AND BSD-3-Clause | Within Qt / candidate | Included in Qt corpus | PASS preservation; candidate |
