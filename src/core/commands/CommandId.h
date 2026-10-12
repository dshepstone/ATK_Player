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
    ShowReviewLibrary,
    NewReview,
    RenameFromRoster,
    OpenMedia,
    NewProject,
    AddMediaToPlaylist,
    OpenProject,
    SaveProject,
    SaveProjectAs,
    ExportReview,
    ExportCurrentFrame,
    ExportImageSequence,
    ExportReviewNotes,
    ExportNotesVideo,
    CloseSource,
    Quit,

    // Edit
    Preferences,

    // Drawing (Edit menu)
    ToggleAnnotateMode,
    UndoAnnotation,
    RedoAnnotation,
    ClearFrameAnnotations,
    IncreaseBrushSize,
    DecreaseBrushSize,
    ToggleAnnotationsVisible,
    ToggleTracingPaper,
    ToggleGhosting,
    NextAnnotatedFrame,
    PreviousAnnotatedFrame,

    // Playback transport
    PlayPause,
    Stop,
    PreviousFrame,
    NextFrame,
    FirstFrame,
    LastFrame,
    ToggleLoop,
    SkipBack10Seconds,
    SkipForward10Seconds,
    PreviousPlaylistItem,
    NextPlaylistItem,
    RemovePlaylistItem,
    MovePlaylistItemUp,
    MovePlaylistItemDown,
    RelinkMedia,

    // Range and annotation
    SetRangeIn,
    SetRangeOut,
    ClearRange,
    AddBookmark,
    AddRangeBookmark,
    NextBookmark,
    PreviousBookmark,
    DeleteBookmark,
    ToggleBookmarkSnap,

    // Audio
    ToggleMute,
    ToggleAudioScrub,
    ToggleFrameStepAudio,
    VolumeUp,
    VolumeDown,
    LoadExternalAudio,
    ClearExternalAudio,
    ExternalAudioOffsetBackOneFrame,
    ExternalAudioOffsetForwardOneFrame,
    ResetExternalAudioOffset,

    // View
    ZoomIn,
    ZoomOut,
    ZoomFit,
    ZoomActualSize,
    FlipHorizontal,
    MediaInformation,
    ToggleFullScreen,
    ToggleVideoFullScreen,
    ToggleComparison,
    CompareSideBySide,
    CompareStacked,
    CompareWipe,
    CompareBlend,
    CompareDifference,
    CompareBOffsetBackOneFrame,
    CompareBOffsetForwardOneFrame,
    ResetCompareBOffset,
    ToggleSourcesPanel,
    ToggleBookmarksPanel,
    ToggleNotesPanel,
    TimelineZoomIn,
    TimelineZoomOut,
    TimelineZoomFit,

    // Help
    ShowWelcome,
    About,
};

} // namespace atk::commands
