cmake_minimum_required(VERSION 3.24)

# ---------------------------------------------------------------------------
# ATK Player
# Cross-platform animation playback and review application.
#
# Version is defined once here and propagated to C++ via core/Version.h.in.
# ---------------------------------------------------------------------------
project(ATKPlayer
    VERSION 0.2.0
    DESCRIPTION "Animation playback and review application"
    HOMEPAGE_URL "https://github.com/dshepstone/ATK_Player"
    LANGUAGES CXX
)

# Pre-release suffix. Cleared for tagged releases.
set(ATK_VERSION_SUFFIX "dev" CACHE STRING "Pre-release version suffix (e.g. dev, rc1). Empty for final releases.")

if(ATK_VERSION_SUFFIX STREQUAL "")
    set(ATK_VERSION_STRING "${PROJECT_VERSION}")
else()
    set(ATK_VERSION_STRING "${PROJECT_VERSION}-${ATK_VERSION_SUFFIX}")
endif()

message(STATUS "ATK Player version: ${ATK_VERSION_STRING}")

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

# ---------------------------------------------------------------------------
# Global project settings
#
# These are deliberately limited to language level and output layout. Include
# directories, compile definitions and warning flags are attached to targets,
# never globally (see cmake/ATKCompilerWarnings.cmake).
# ---------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set_property(GLOBAL PROPERTY USE_FOLDERS ON)

# Keep the executable and its runtime dependencies side by side so the app can
# be launched directly from the build tree on every platform.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")

# Generated for clangd / VS Code C++ tooling.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(ATK_BUILD_TESTS "Build ATK Player unit tests" ON)

include(ATKCompilerWarnings)

# ---------------------------------------------------------------------------
# Dependencies
#
# Qt 6 only for now. FFmpeg is introduced in milestone M1 and will be linked
# dynamically; see docs/ARCHITECTURE.md.
# ---------------------------------------------------------------------------
# ---------------------------------------------------------------------------
# Qt discovery
#
# The tracked CMakePresets.json must not contain a machine-specific path, so
# the Qt prefix arrives one of three ways, in order of precedence:
#
#   1. -DATK_QT_ROOT=... , which the presets populate from the QT_ROOT
#      environment variable
#   2. the QT_ROOT environment variable directly
#   3. anything CMake already knows -- a CMAKE_PREFIX_PATH environment
#      variable, Qt6_DIR, or a CMakeUserPresets.json override
#
# Nothing is appended when none is set, so option 3 keeps working.
# ---------------------------------------------------------------------------
set(ATK_QT_ROOT "" CACHE PATH "Qt 6 installation prefix, e.g. C:/Qt/6.9.3/msvc2022_64")

if(ATK_QT_ROOT)
    list(APPEND CMAKE_PREFIX_PATH "${ATK_QT_ROOT}")
    message(STATUS "Qt prefix from ATK_QT_ROOT: ${ATK_QT_ROOT}")
elseif(DEFINED ENV{QT_ROOT} AND NOT "$ENV{QT_ROOT}" STREQUAL "")
    list(APPEND CMAKE_PREFIX_PATH "$ENV{QT_ROOT}")
    message(STATUS "Qt prefix from QT_ROOT environment variable: $ENV{QT_ROOT}")
endif()

set(ATK_QT_COMPONENTS Core Gui Widgets Multimedia)
if(ATK_BUILD_TESTS)
    list(APPEND ATK_QT_COMPONENTS Test)
endif()

find_package(Qt6 6.5 REQUIRED COMPONENTS ${ATK_QT_COMPONENTS} Network)

message(STATUS "Qt ${Qt6_VERSION} found at ${Qt6_DIR}")

# ---------------------------------------------------------------------------
# FFmpeg
#
# Supplied by vcpkg in manifest mode; see vcpkg.json for the pinned baseline and
# the feature set. The vcpkg port ships a FindFFMPEG module, so the usual
# find_package works and no path into vcpkg_installed is hard-coded here.
# ---------------------------------------------------------------------------
# Qt 6.9 ships its own lib/cmake/Qt6/FindFFmpeg.cmake for its multimedia
# backend. Windows filesystems are case-insensitive, so that file satisfies a
# find_package(FFMPEG) lookup and shadows vcpkg's FindFFMPEG.cmake -- and Qt's
# module reports "not found" without failing the configure, which would then
# break at link time with no obvious cause.
#
# Naming vcpkg's module directory first makes the resolution deterministic
# rather than dependent on the order two prefixes happened to be appended in.
foreach(_candidate IN ITEMS
        "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/share/ffmpeg"
        "${_VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/share/ffmpeg"
        "${CMAKE_BINARY_DIR}/vcpkg_installed/${VCPKG_TARGET_TRIPLET}/share/ffmpeg")
    if(EXISTS "${_candidate}/FindFFMPEG.cmake")
        list(PREPEND CMAKE_MODULE_PATH "${_candidate}")
        message(STATUS "FFmpeg module:    ${_candidate}")
        break()
    endif()
endforeach()

find_package(FFMPEG REQUIRED)

# find_package(REQUIRED) is not enough on its own here: Qt's module can satisfy
# the call and still leave these empty. Check what actually came back.
if(NOT FFMPEG_LIBRARIES OR NOT FFMPEG_INCLUDE_DIRS)
    message(FATAL_ERROR
        "FFmpeg was located but reported no libraries or include directories. "
        "This usually means Qt's FindFFmpeg.cmake was used instead of vcpkg's. "
        "Check that VCPKG_INSTALLED_DIR points at a tree containing "
        "share/ffmpeg/FindFFMPEG.cmake.")
endif()

message(STATUS "FFmpeg libraries: ${FFMPEG_LIBRARIES}")
message(STATUS "FFmpeg includes:  ${FFMPEG_INCLUDE_DIRS}")

# ---------------------------------------------------------------------------
# FFmpeg runtime location
#
# FFmpeg is dynamically linked, so the DLLs must be findable at run time.
# Rather than asking every developer to put vcpkg's bin directory on their
# global PATH -- which then silently affects every other program on the machine
# -- the directory is recorded here and used two ways: copied next to the
# executable after linking, and prepended to PATH for the tests only.
# ---------------------------------------------------------------------------
if(WIN32)
    # vcpkg keeps debug DLLs in a separate debug/bin tree, and the debug import
    # libraries this build links against expect them. Copying the release DLLs
    # next to a Debug build produces a mismatch that only shows up as a failure
    # to start, so the build type decides which tree is used.
    set(_atk_vcpkg_bin_suffix "bin")
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        set(_atk_vcpkg_bin_suffix "debug/bin")
    endif()

    foreach(_candidate IN ITEMS
            "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/${_atk_vcpkg_bin_suffix}"
            "${_VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/${_atk_vcpkg_bin_suffix}"
            "${CMAKE_BINARY_DIR}/vcpkg_installed/${VCPKG_TARGET_TRIPLET}/${_atk_vcpkg_bin_suffix}")
        if(EXISTS "${_candidate}")
            # Deliberately not CACHE: this depends on CMAKE_BUILD_TYPE, and a
            # cached value from a previous configure would keep pointing at the
            # other build's DLLs.
            set(ATK_FFMPEG_RUNTIME_DIR "${_candidate}")
            break()
        endif()
    endforeach()

    if(ATK_FFMPEG_RUNTIME_DIR)
        message(STATUS "FFmpeg runtime:   ${ATK_FFMPEG_RUNTIME_DIR}")
    else()
        message(WARNING
            "FFmpeg runtime directory not found; ATKPlayer.exe may not start. "
            "Expected it under the vcpkg installed tree.")
    endif()
endif()

qt_standard_project_setup()

add_subdirectory(src)

if(ATK_BUILD_TESTS)
    enable_testing()

    # Deterministic media fixtures for the integration tests, generated by the
    # ffmpeg tool vcpkg built. See cmake/ATKTestMedia.cmake.
    include(ATKTestMedia)
    atk_add_test_media()

    add_subdirectory(tests)
endif()
