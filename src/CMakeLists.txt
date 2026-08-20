# ---------------------------------------------------------------------------
# atk_core -- everything with behaviour and no widgets.
#
# Media, playback, timeline, project, export, comparison, the API dispatcher and
# the platform abstraction. It links Qt Core and Qt Gui (for QImage and QColor)
# but NOT Qt Widgets, which is what mechanically enforces the rule that core
# logic cannot reach into the UI.
# ---------------------------------------------------------------------------

configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/core/Version.h.in"
    "${CMAKE_CURRENT_BINARY_DIR}/generated/core/Version.h"
    @ONLY
)

set(ATK_CORE_SOURCES
    core/Logging.cpp
    core/commands/CommandDefinitions.cpp

    media/MediaMetadata.cpp
    media/MediaSource.cpp
    media/FrameCache.cpp
    media/decoders/FFmpegDecoder.cpp

    playback/PlaybackClock.cpp
    playback/PlaybackController.cpp

    timeline/Bookmark.cpp
    timeline/Timecode.cpp
    timeline/TimelineModel.cpp

    project/Project.cpp
    project/ProjectSerializer.cpp

    export/FFmpegExporter.cpp

    api/ApiCommands.cpp
    api/ApiServer.cpp

    playback/CompareSession.cpp
)

set(ATK_CORE_HEADERS
    core/Logging.h
    core/commands/CommandId.h
    core/commands/CommandDefinitions.h

    media/MediaMetadata.h
    media/MediaSource.h
    media/VideoFrame.h
    media/FrameCache.h
    media/decoders/IDecoder.h
    media/decoders/FFmpegDecoder.h

    playback/PlaybackClock.h
    playback/PlaybackController.h

    timeline/Bookmark.h
    timeline/PlaybackRange.h
    timeline/Timecode.h
    timeline/TimelineModel.h

    project/Project.h
    project/ProjectSerializer.h

    export/FFmpegExporter.h

    api/ApiCommands.h
    api/ApiServer.h

    playback/CompareSession.h

    platform/PlatformInfo.h
)

# Exactly one platform implementation is compiled. Adding a platform means
# adding a file here and nothing else.
if(WIN32)
    list(APPEND ATK_CORE_SOURCES platform/windows/PlatformInfo_Windows.cpp)
elseif(APPLE)
    list(APPEND ATK_CORE_SOURCES platform/macos/PlatformInfo_Mac.cpp)
elseif(UNIX)
    list(APPEND ATK_CORE_SOURCES platform/linux/PlatformInfo_Linux.cpp)
else()
    message(FATAL_ERROR "No platform implementation for this system.")
endif()

add_library(atk_core STATIC ${ATK_CORE_SOURCES} ${ATK_CORE_HEADERS})
add_library(ATK::core ALIAS atk_core)

target_include_directories(atk_core
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}"
        "${CMAKE_CURRENT_BINARY_DIR}/generated"
)

target_link_libraries(atk_core
    PUBLIC
        Qt6::Core
        Qt6::Gui
)

atk_set_compiler_warnings(atk_core)

set_target_properties(atk_core PROPERTIES
    AUTOMOC ON
    FOLDER "Libraries"
    POSITION_INDEPENDENT_CODE ON
)

add_subdirectory(ui)
add_subdirectory(app)
