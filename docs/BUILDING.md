# Building ATK Player

Windows is the current development target. macOS and Linux are milestones M7 and
M8; the source tree and CMake configuration are already structured for them, but
neither has been built or tested yet.

---

## Prerequisites

| Requirement | Version | Notes |
|---|---|---|
| Visual Studio 2022 | 17.x | The **Desktop development with C++** workload. Community edition is fine. |
| CMake | 3.25 or newer | Visual Studio bundles 3.27, which works. |
| Ninja | any recent | Visual Studio bundles it. |
| Qt | 6.5 or newer, **msvc2022_64** | The MSVC build, not MinGW. |
| Git | any | |

FFmpeg is **not** required. It arrives in milestone M1.

### Installing Qt

Either method works; both give the same result.

**Official installer** — download the Qt Online Installer from
[qt.io](https://www.qt.io/download-qt-installer), which requires a free Qt
account. Select **Qt 6.9.x → MSVC 2022 64-bit**. The Qt Creator IDE and the
mobile/WebAssembly targets are not needed.

**Command line** — `aqtinstall` fetches the same official binaries with no
account:

```bash
pip install aqtinstall
```

```bash
python -m aqt install-qt windows desktop 6.9.3 win64_msvc2022_64 --outputdir C:/Qt
```

Either way you end up with a directory such as `C:\Qt\6.9.3\msvc2022_64`. That
path is what CMake needs to find Qt.

---

## Building from the command line

CMake finds Qt through `CMAKE_PREFIX_PATH`. Point it at your Qt directory, and
build from a **Developer Command Prompt for VS 2022** so `cl.exe` is on `PATH`:

```bash
set CMAKE_PREFIX_PATH=C:\Qt\6.9.3\msvc2022_64
```

```bash
cmake --preset windows-msvc-debug
```

```bash
cmake --build --preset windows-msvc-debug
```

```bash
ctest --preset windows-msvc-debug
```

The executable is written to:

```
build/windows-msvc-debug/bin/ATKPlayer.exe
```

`windeployqt` runs automatically after linking and copies the Qt DLLs and the
platform plugin next to the executable, so it can be launched directly without
Qt on `PATH`.

### If you are not in a Developer Command Prompt

The `windows-msvc-debug` preset uses Ninja, which needs `cl.exe` already on
`PATH`. If starting a Developer Command Prompt is inconvenient, use the Visual
Studio generator preset instead — it locates MSVC by itself:

```bash
cmake --preset windows-vs2022
```

```bash
cmake --build --preset windows-vs2022-debug
```

This is a multi-config generator, so the build type is chosen at build time
rather than configure time.

---

## Available presets

| Preset | Generator | Build type |
|---|---|---|
| `windows-msvc-debug` | Ninja | Debug |
| `windows-msvc-release` | Ninja | RelWithDebInfo |
| `windows-msvc-debug-vcpkg` | Ninja | Debug, dependencies via the vcpkg manifest |
| `windows-vs2022` | Visual Studio 17 2022 | Multi-config |

The `-vcpkg` variant needs the `VCPKG_ROOT` environment variable set. It is not
needed until FFmpeg is introduced in M1 — see `vcpkg.json`.

Build output goes to `build/<preset-name>/`, which `.gitignore` excludes.

---

## Building in VS Code

1. Install the **CMake Tools** and **C/C++** extensions (`.vscode/extensions.json`
   recommends both).
2. Set `CMAKE_PREFIX_PATH` to your Qt directory, either as a system environment
   variable or in your own `.vscode/settings.json`:

   ```json
   {
     "cmake.environment": {
       "CMAKE_PREFIX_PATH": "C:/Qt/6.9.3/msvc2022_64"
     }
   }
   ```

   Do not commit that file — it contains a path specific to your machine, and
   `.gitignore` excludes it for that reason.
3. Open the command palette → **CMake: Select Configure Preset** → **Windows x64
   Debug**.
4. Build with **F7**, run with **Shift+F5**, debug with **F5**.

CMake Tools picks up the MSVC environment itself, so no Developer Command Prompt
is needed.

`compile_commands.json` is generated into the build directory, so clangd and
IntelliSense resolve includes without extra configuration.

---

## Running the tests

```bash
ctest --preset windows-msvc-debug
```

Six suites cover the core library: timecode conversion, the frame cache, the
timeline model, playback ranges, the command table and the API dispatcher.

To run one suite directly with more detail:

```bash
build/windows-msvc-debug/bin/tst_timelinemodel.exe -v2
```

The widgets are not covered by automated tests in Phase 0 — GUI tests arrive with
M2, once there is behaviour worth asserting beyond construction.

---

## Seeing log output

`ATKPlayer.exe` is built as a Windows GUI application, so it has no console. Log
output goes to the debugger, where VS Code's debug console shows it.

To get logs in a terminal instead, configure with:

```bash
cmake --preset windows-msvc-debug -DATK_CONSOLE_LOGGING=ON
```

Runtime filtering uses Qt's standard mechanism. To turn on debug output for
every subsystem:

```bash
set QT_LOGGING_RULES=atk.*.debug=true
```

Categories are `atk.app`, `atk.ui`, `atk.media`, `atk.playback`, `atk.timeline`,
`atk.project`, `atk.api` and `atk.platform`.

---

## Build options

| Option | Default | Effect |
|---|---|---|
| `ATK_BUILD_TESTS` | `ON` | Build the unit tests and register them with CTest. |
| `ATK_CONSOLE_LOGGING` | `OFF` | Build as a console application so logs appear in the terminal. |
| `ATK_RUN_WINDEPLOYQT` | `ON` (Windows) | Copy the Qt runtime next to the executable after linking. |
| `ATK_VERSION_SUFFIX` | `dev` | Pre-release suffix. Set to `""` for a final release. |

---

## Troubleshooting

**`Could not find a package configuration file provided by "Qt6"`**

`CMAKE_PREFIX_PATH` is not pointing at your Qt installation. It must be the
directory containing `lib/cmake/Qt6/`, for example `C:\Qt\6.9.3\msvc2022_64` —
not `C:\Qt` and not the `bin` directory.

**`No CMAKE_CXX_COMPILER could be found`**

The Ninja presets need `cl.exe` on `PATH`. Either build from a Developer Command
Prompt for VS 2022, or use the `windows-vs2022` preset.

**The executable starts and immediately exits, or reports a missing DLL**

The Qt runtime is not beside the executable. Confirm `ATK_RUN_WINDEPLOYQT` is
`ON` and rebuild; the build log should show `windeployqt` copying `Qt6Core.dll`
and friends into `build/<preset>/bin/`.

**Qt was found but linking fails with unresolved symbols**

You are almost certainly using a MinGW Qt build with MSVC. The two are not
compatible — install the `msvc2022_64` package.

**A Debug build cannot load Qt, or crashes on startup**

Debug and Release Qt libraries cannot be mixed with the opposite build of the
application on Windows. Match them: a Debug build of ATK Player needs the debug
Qt DLLs (`Qt6Cored.dll`), which `windeployqt` selects automatically.
