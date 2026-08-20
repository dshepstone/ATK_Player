cmake_minimum_required(VERSION 3.24)

# ---------------------------------------------------------------------------
# ATK Player
# Cross-platform animation playback and review application.
#
# Version is defined once here and propagated to C++ via core/Version.h.in.
# ---------------------------------------------------------------------------
project(ATKPlayer
    VERSION 0.1.0
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

set(ATK_QT_COMPONENTS Core Gui Widgets)
if(ATK_BUILD_TESTS)
    list(APPEND ATK_QT_COMPONENTS Test)
endif()

find_package(Qt6 6.5 REQUIRED COMPONENTS ${ATK_QT_COMPONENTS})

message(STATUS "Qt ${Qt6_VERSION} found at ${Qt6_DIR}")

qt_standard_project_setup()

add_subdirectory(src)

if(ATK_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
