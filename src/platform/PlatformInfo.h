#pragma once

#include <QString>

// ---------------------------------------------------------------------------
// Platform abstraction.
//
// Core and UI code calls only the declarations below. Exactly one
// implementation is compiled in, chosen by CMake:
//
//     src/platform/windows/PlatformInfo_Windows.cpp
//     src/platform/macos/PlatformInfo_Mac.cpp
//     src/platform/linux/PlatformInfo_Linux.cpp
//
// No <windows.h>, no Cocoa and no X11/Wayland headers may appear outside those
// directories. When a new capability needs OS-specific behaviour, declare it
// here first and add one implementation per platform.
// ---------------------------------------------------------------------------

namespace atk::platform {

class PlatformInfo {
public:
    /// Human-readable OS description for the log banner and About dialog,
    /// e.g. "Windows 11 Home (10.0.26200)".
    static QString operatingSystemDescription();

    /// Directory for application-managed data (caches, recent-file lists).
    /// Created on first use if it does not exist.
    static QString applicationDataDirectory();

    /// Ask the OS to keep the display awake during playback. Returns false when
    /// the platform has no implementation yet; callers must tolerate that.
    ///
    /// TODO(M1): implement per platform once continuous playback exists.
    static bool setDisplaySleepInhibited(bool inhibited);
};

} // namespace atk::platform
