# Building ATK Player

Windows is the current development target. macOS and Linux are milestones M7 and
M8; the source tree and CMake configuration are already structured for them, but
neither has been built or tested yet.

---

## Prerequisites

| Requirement | Version | Notes |
|---|---|---|
| Visual Studio 2022 | 17.x | **Desktop development with C++** workload. Community edition is fine. |
| CMake | 3.25 or newer | Visual Studio bundles 3.27, which works — no separate install needed. |
| Ninja | any recent | Visual Studio bundles it. |
| Qt | 6.5 or newer, **msvc2022_64** | The MSVC build, not MinGW, not ARM. **The Multimedia module is required** (audio output). |
| vcpkg | any recent | Supplies FFmpeg. Cloned outside the repository. |
| Python | 3.8+ | Only needed to install Qt via `aqtinstall`. |
| Git | any | |

FFmpeg is built from source by vcpkg on first configure. That takes roughly ten
to twenty minutes once; afterwards it is cached.

### Where the bundled tools live

Visual Studio ships CMake and Ninja but does not put them on the system `PATH`.
Find your Visual Studio installation with `vswhere`, which is always at a fixed
location:

```bash
"C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe" -latest -property installationPath
```

Relative to that path:

| Tool | Relative path |
|---|---|
| CMake | `Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe` |
| Ninja | `Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe` |
| MSVC env | `VC\Auxiliary\Build\vcvars64.bat` |

You do not need to memorise these. A **Developer Command Prompt for VS 2022**
(Start menu) puts the compiler on `PATH`, and VS Code's CMake Tools extension
finds all of them by itself. The paths above are only for scripting.

### Installing Qt

Use `aqtinstall` — it fetches the same official binaries as the Qt installer,
with no Qt account and no GUI:

```bash
python -m pip install --upgrade aqtinstall
```

Check which versions are available before picking one:

```bash
python -m aqt list-qt windows desktop --spec "6.9"
```

Then install the newest, into `C:\Qt`:

```bash
python -m aqt install-qt windows desktop 6.9.3 win64_msvc2022_64 --outputdir C:/Qt
```

That produces `C:\Qt\6.9.3\msvc2022_64`. Confirm it is a development SDK and not
just a runtime — it must contain `bin\`, `include\`, `lib\`, and in particular:

```
C:\Qt\6.9.3\msvc2022_64\lib\cmake\Qt6\Qt6Config.cmake
```

> The Qt DLLs that ship inside Maya and Substance 3D Painter are **runtime
> libraries only**. They have no headers and no CMake package files, and must not
> be used for development.

The Qt GUI installer works too — select **Qt 6.9.x → MSVC 2022 64-bit**. It just
needs a free Qt account.

---

## Telling CMake where Qt and vcpkg are

`CMakePresets.json` is committed, so it must not contain a path that exists only
on one machine. Two environment variables supply them:

```bash
setx QT_ROOT     C:/Qt/6.9.3/msvc2022_64
setx VCPKG_ROOT  C:/dev/vcpkg
```

Install vcpkg once, outside the repository:

```bash
git clone https://github.com/microsoft/vcpkg C:/dev/vcpkg
```

```bash
C:/dev/vcpkg/bootstrap-vcpkg.bat -disableMetrics
```

The manifest in `vcpkg.json` pins the dependency set, so `cmake --preset` builds
FFmpeg itself on first configure. Nothing needs to be installed by hand and
vcpkg is never added to the global `PATH`.

### If your checkout path contains a space

FFmpeg's `configure` does not quote the `-libpath:` flag it passes to `link.exe`.
A vcpkg installed-tree path containing a space is therefore split in two and the
FFmpeg build fails with `LNK1181: cannot open input file`. This is a limitation
of FFmpeg's build system, not of vcpkg or of this project.

The workaround is to place the installed tree somewhere without a space:

```bash
setx ATK_VCPKG_INSTALLED_DIR C:/dev/atk-vcpkg-installed
```

Leave it unset if your checkout path has no spaces; the default
(`build/<preset>/vcpkg_installed`) is then used.

---

## Telling CMake where Qt is

`CMakePresets.json` is committed to the repository, so it must not contain a path
that only exists on one machine. Qt is located through the **`QT_ROOT`**
environment variable instead:

```bash
set QT_ROOT=C:/Qt/6.9.3/msvc2022_64
```

Set it permanently so you do not have to repeat it:

```bash
setx QT_ROOT C:/Qt/6.9.3/msvc2022_64
```

Configure prints which mechanism it used, so a misconfiguration is visible
immediately rather than three errors later:

```
-- Qt prefix from ATK_QT_ROOT: C:/Qt/6.9.3/msvc2022_64
-- Qt 6.9.3 found at C:/Qt/6.9.3/msvc2022_64/lib/cmake/Qt6
```

**Three ways to supply it**, in order of precedence:

1. `-DATK_QT_ROOT=C:/Qt/6.9.3/msvc2022_64` on the command line. The presets
   populate this from `QT_ROOT`.
2. The `QT_ROOT` environment variable.
3. Anything CMake already knows — a `CMAKE_PREFIX_PATH` environment variable, or
   `Qt6_DIR`.

If you would rather not use an environment variable, create a
**`CMakeUserPresets.json`** beside `CMakePresets.json`. It is git-ignored
precisely so it can hold your local path:

```json
{
  "version": 6,
  "configurePresets": [
    {
      "name": "my-debug",
      "inherits": "windows-debug",
      "cacheVariables": {
        "ATK_QT_ROOT": "C:/Qt/6.9.3/msvc2022_64"
      }
    }
  ]
}
```

Then use `--preset my-debug` everywhere below.

---

## Building from the command line

From a **Developer Command Prompt for VS 2022**, with `QT_ROOT` set:

```bash
cmake --preset windows-debug
```

```bash
cmake --build --preset windows-debug
```

```bash
ctest --preset windows-debug
```

The executable is written to:

```
build/windows-debug/bin/ATKPlayer.exe
```

`windeployqt` runs automatically after linking, so the Qt DLLs and the platform
plugin are already beside the executable and it can be launched by
double-clicking it — no Qt on `PATH` required.

### If you are not in a Developer Command Prompt

The Ninja presets need `cl.exe` on `PATH`. Use the Visual Studio generator preset
instead; it locates MSVC by itself:

```bash
cmake --preset windows-vs2022
```

```bash
cmake --build --preset windows-vs2022-debug
```

This is a multi-config generator, so the build type is chosen at build time
rather than at configure time.

---

## Available presets

| Preset | Generator | Build type |
|---|---|---|
| `windows-debug` | Ninja | Debug |
| `windows-release` | Ninja | RelWithDebInfo |
| `windows-vs2022` | Visual Studio 17 2022 | Multi-config (`-debug` / `-release` build presets) |

Build output goes to `build/<preset-name>/`, which `.gitignore` excludes.

---

## Building in VS Code

This is the intended day-to-day workflow.

1. Install the recommended extensions — VS Code offers them automatically from
   `.vscode/extensions.json`: **CMake Tools** and **C/C++**.
2. Make sure `QT_ROOT` is set (see above). Setting it with `setx` and then
   restarting VS Code is the least troublesome route, because VS Code inherits
   the environment it was launched with.
3. Command palette → **CMake: Select Configure Preset** → **Windows x64 Debug**.
4. **F7** builds. **F5** debugs. **Ctrl+Shift+P → Tasks: Run Task** offers
   configure, build, test and run.

`.vscode/tasks.json` and `.vscode/launch.json` are committed because they only
reference preset names and `${workspaceFolder}`. `.vscode/settings.json` is
git-ignored — that is where a machine-specific Qt path would end up, so if you
prefer configuring Qt there instead of via `QT_ROOT`:

```json
{
  "cmake.environment": {
    "QT_ROOT": "C:/Qt/6.9.3/msvc2022_64"
  }
}
```

CMake Tools supplies the MSVC environment itself, so no Developer Command Prompt
is needed inside VS Code.

`compile_commands.json` is generated into the build directory, so clangd and
IntelliSense resolve includes with no extra configuration.

---

## Running the tests

```bash
ctest --preset windows-debug
```

| Suite | Covers |
|---|---|
| `fixture_validation` | ffprobe-checks the generated test media before anything decodes it |
| `tst_timecode` | Frame to SMPTE conversion, including 23.976 / 29.97 |
| `tst_framecache` | LRU eviction, byte budget, source-generation identity |
| `tst_timelinemodel` | Extent, playhead clamping, ranges, bookmarks |
| `tst_playbackcontroller` | Transport state, frame stepping, clamping, loop |
| `tst_playbackrange` | Inclusive range arithmetic |
| `tst_bookmark` | Bookmark fields, equality, palette bounds |
| `tst_commanddefinitions` | Command table integrity and required shortcuts |
| `tst_apicommands` | API dispatch, numeric validation, error reporting |
| `tst_ffmpegutil` | FFmpeg error translation and rational time-base arithmetic |
| `tst_mediadecoder` | **Real decoding**: metadata, frame accuracy, seeking, audio resampling |

### Generated test media

`tst_mediadecoder` decodes fixtures the build generates with the `ffmpeg` tool
vcpkg produced -- nothing binary is committed. They land in
`build/<preset>/test-media/` and are validated with `ffprobe` before the decoder
tests run, so a change in FFmpeg's defaults is reported directly rather than
surfacing as a confusing decode failure.

Both fixtures are 640x360, exactly 24 fps, exactly 2.0 s -- **48 frames** --
with 48 kHz audio:

| File | Video | Audio | Why |
|---|---|---|---|
| `atk_fixture_48f.mkv` | FFV1 (lossless, all-intra) | PCM s16le | Deterministic pixels and exact frame counting |
| `atk_fixture_48f.mp4` | MPEG-4 Part 2 | AAC | Real inter-frame dependencies, so non-keyframe seeks are genuinely exercised |

Regenerate them with:

```bash
cmake --build --preset windows-debug --target atk_test_media
```

To run one suite directly with more detail:

```bash
build/windows-debug/bin/tst_playbackcontroller.exe -v2
```

The widgets are not covered by automated tests in Phase 0. GUI tests arrive with
M2, once there is behaviour worth asserting beyond construction.

---

## Seeing log output

`ATKPlayer.exe` is built as a Windows GUI application, so it has no console. Log
output goes to the debugger, where VS Code's debug console shows it.

To get logs in a terminal instead:

```bash
cmake --preset windows-debug -DATK_CONSOLE_LOGGING=ON
```

Runtime filtering uses Qt's standard mechanism:

```bash
set QT_LOGGING_RULES=atk.*.debug=true
```

Categories are `atk.app`, `atk.ui`, `atk.media`, `atk.playback`, `atk.timeline`,
`atk.project`, `atk.api` and `atk.platform`.

---

## Build options

| Option | Default | Effect |
|---|---|---|
| `ATK_QT_ROOT` | *(from `QT_ROOT`)* | Qt installation prefix. |
| `ATK_BUILD_TESTS` | `ON` | Build the unit tests and register them with CTest. |
| `ATK_CONSOLE_LOGGING` | `OFF` | Build as a console application so logs appear in the terminal. |
| `ATK_RUN_WINDEPLOYQT` | `ON` (Windows) | Copy the Qt runtime next to the executable after linking. |
| `ATK_VERSION_SUFFIX` | `dev` | Pre-release suffix. Set to `""` for a final release. |

---

## Deployment (groundwork only — packaging is M6)

`windeployqt.exe` lives inside the Qt SDK:

```
C:\Qt\6.9.3\msvc2022_64\bin\windeployqt.exe
```

Nothing needs to reference that path by hand. CMake imports it as the
`Qt6::windeployqt` target, and `src/app/CMakeLists.txt` invokes that target as a
post-build step — which is why the build tree is directly runnable. Installing:

```bash
cmake --install build/windows-debug
```

runs `qt_generate_deploy_app_script()`, producing a complete runtime layout under
`install/windows-debug/`.

The MSI installer (milestone M6) builds on that layout rather than assembling its
own file list. See [../packaging/windows/README.md](../packaging/windows/README.md).

---

## Troubleshooting

**`Could not find a package configuration file provided by "Qt6"`**

`QT_ROOT` is unset or wrong. It must be the directory containing
`lib\cmake\Qt6\Qt6Config.cmake` — for example `C:/Qt/6.9.3/msvc2022_64`, not
`C:/Qt` and not the `bin` directory. Re-run configure and check the
`-- Qt prefix from ...` line.

**`No CMAKE_CXX_COMPILER could be found`**

The Ninja presets need `cl.exe` on `PATH`. Either build from a Developer Command
Prompt for VS 2022, or use the `windows-vs2022` preset.

**The executable starts and immediately exits, or reports a missing DLL**

The Qt runtime is not beside the executable. Confirm `ATK_RUN_WINDEPLOYQT` is
`ON` and rebuild; the build log should show `windeployqt` copying `Qt6Cored.dll`
and friends into `build/<preset>/bin/`.

**Qt was found but linking fails with unresolved symbols**

You are almost certainly using a MinGW Qt build with MSVC. The two are not
compatible — install the `msvc2022_64` package.

**A Debug build cannot load Qt, or crashes on startup**

Debug and Release Qt libraries cannot be mixed with the opposite build of the
application on Windows. `windeployqt` picks the matching set automatically, so
this usually means stale DLLs — delete `build/<preset>/bin/` and rebuild.

**Changing `QT_ROOT` had no effect**

`ATK_QT_ROOT` is cached after the first configure. Either pass
`-DATK_QT_ROOT=...` explicitly, or delete the build directory:

```bash
cmake -E rm -rf build/windows-debug
```
## Building a Windows release

Normal development remains `0.2.0-dev` through the tracked presets:

```powershell
cmake --preset windows-release
cmake --build --preset windows-release
```

The M6 release entry point requires an x64 Visual Studio 2022 developer
environment, Ninja, Qt 6.9.3, and the vcpkg baseline pinned by `vcpkg.json`.
Set `QT_ROOT`, `VCPKG_ROOT`, and (when using a shared binary tree)
`ATK_VCPKG_INSTALLED_DIR`, then build RC1 with:

```powershell
.\packaging\windows\build-installer.ps1
```

That script explicitly configures `ATK_VERSION_SUFFIX=rc1`; it does not change
the normal `dev` default. It bootstraps pinned developer-local WiX tooling when
needed, runs the full Release tests, stages through `cmake --install`, verifies
and smoke-tests the package, and creates MSI/SHA-256 artifacts under
`build/package/windows`.

Only after RC acceptance, use the same architecture for suffix-free final:

```powershell
.\packaging\windows\build-installer.ps1 -VersionSuffix ""
```

See [Windows packaging](../packaging/windows/README.md) for installer identity,
signing inputs, prerequisites, install/uninstall and upgrade acceptance.
