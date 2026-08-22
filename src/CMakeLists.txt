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
    media/PlaybackQueue.cpp
    media/AudioSourceReader.cpp
    media/WaveformData.cpp
    media/WaveformWorker.cpp
    media/ScrubAudioWorker.cpp
    media/CompareAudioWorker.cpp
    audio/ScrubAudioEngine.cpp
    media/MediaDecoder.cpp
    media/DecoderWorker.cpp
    media/PlaylistProbeWorker.cpp
    media/ffmpeg/FFmpegUtil.cpp

    audio/AudioRingBuffer.cpp
    audio/AudioOutput.cpp

    playback/PlaybackClock.cpp
    playback/PlaybackController.cpp
    playback/ComparisonCompositor.cpp

    timeline/Bookmark.cpp
    timeline/Timecode.cpp
    timeline/TimelineModel.cpp
    timeline/TimelineViewport.cpp

    project/Project.cpp
    project/ProjectSerializer.cpp

    export/ExportSpec.cpp
    export/ExportRenderer.cpp
    export/ExportJob.cpp
    export/FFmpegExporter.cpp

    api/ApiCommands.cpp
    api/ApiServer.cpp

    playback/CompareSession.cpp
    playback/CompareVideoLane.cpp
)

set(ATK_CORE_HEADERS
    core/Logging.h
    core/commands/CommandId.h
    core/commands/CommandDefinitions.h

    media/MediaMetadata.h
    media/MediaSource.h
    media/VideoFrame.h
    media/AudioBuffer.h
    media/MediaLibraryInfo.h
    media/DecodeGeneration.h
    media/FrameCache.h
    media/PlaybackQueue.h
    media/AudioSourceReader.h
    media/WaveformData.h
    media/WaveformWorker.h
    media/ScrubAudioWorker.h
    media/CompareAudioWorker.h
    audio/ScrubAudioEngine.h
    media/MediaDecoder.h
    media/DecoderWorker.h
    media/PlaylistProbeWorker.h
    media/ffmpeg/FFmpegUtil.h
    media/ffmpeg/FFmpegRaii.h

    audio/AudioRingBuffer.h
    audio/AudioOutput.h

    playback/PlaybackClock.h
    playback/PlaybackController.h
    playback/ComparisonCompositor.h

    timeline/Bookmark.h
    timeline/PlaybackRange.h
    timeline/Timecode.h
    timeline/TimelineModel.h
    timeline/TimelineViewport.h

    project/Project.h
    project/ProjectSerializer.h

    export/ExportSpec.h
    export/ExportRenderer.h
    export/ExportJob.h
    export/FFmpegExporter.h

    api/ApiCommands.h
    api/ApiServer.h

    playback/CompareSession.h
    playback/CompareVideoLane.h

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
        # Audio output only. Still no Qt6::Widgets, so core code remains unable
        # to include a widget header.
        Qt6::Multimedia
    PRIVATE
        ${FFMPEG_LIBRARIES}
)

# PRIVATE: FFmpeg headers are an implementation detail of the media layer. The
# UI links atk_core but must not be able to include libav* -- that boundary is
# what keeps decoding out of paint events.
target_include_directories(atk_core PUBLIC ${FFMPEG_INCLUDE_DIRS})
target_link_directories(atk_core PRIVATE ${FFMPEG_LIBRARY_DIRS})

atk_set_compiler_warnings(atk_core)

set_target_properties(atk_core PROPERTIES
    AUTOMOC ON
    FOLDER "Libraries"
    POSITION_INDEPENDENT_CODE ON
)

add_subdirectory(ui)
add_subdirectory(app)
