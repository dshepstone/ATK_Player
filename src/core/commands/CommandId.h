#pragma once

namespace atk::commands {

/// Every user-invokable action in ATK Player.
///
/// UI widgets never handle key presses directly; they trigger a CommandId. The
/// mapping from key sequence to command lives in one table
/// (CommandDefinitions.cpp) so that user-configurable hotkeys can be added in
/// M2 without touching any widget.
enum class CommandId {
    // File
    OpenMedia,
    OpenProject,
    SaveProject,
    SaveProjectAs,
    CloseSource,
    Quit,

    // Playback transport
    PlayPause,
    Stop,
    PreviousFrame,
    NextFrame,
    FirstFrame,
    LastFrame,
    ToggleLoop,

    // Range and annotation
    SetRangeIn,
    SetRangeOut,
    ClearRange,
    AddBookmark,
    NextBookmark,
    PreviousBookmark,

    // Audio
    ToggleMute,
    ToggleAudioScrub,
    ToggleFrameStepAudio,
    VolumeUp,
    VolumeDown,

    // View
    ZoomIn,
    ZoomOut,
    ZoomFit,
    ZoomActualSize,
    ToggleFullScreen,
    ToggleSourcesPanel,
    TimelineZoomIn,
    TimelineZoomOut,
    TimelineZoomFit,

    // Help
    About,
};

} // namespace atk::commands
