# ---------------------------------------------------------------------------
# atk_ui -- widgets.
#
# Depends on atk_core; nothing in atk_core depends on this target. The
# dependency runs in one direction only, so the models can be tested, and later
# driven by the external API, with no UI present.
# ---------------------------------------------------------------------------

set(ATK_UI_SOURCES
    AnnotationToolbar.cpp
    ApplicationSettings.cpp
    ArrangePlaylistDialog.cpp
    BatchRenameDialog.cpp
    BookmarkPanel.cpp
    BrushSizePill.cpp
    BurnInOptionsWidget.cpp
    CompareBar.cpp
    ComparisonCompositeWidget.cpp
    ExportDialog.cpp
    ExportUi.cpp
    FrameFieldStyle.cpp
    FrameNumberInput.cpp
    IconProvider.cpp
    MainWindow.cpp
    MediaInformationDialog.cpp
    NewReviewDialog.cpp
    NotesPanel.cpp
    PlaylistCardDelegate.cpp
    ReviewHeaderBar.cpp
    ReviewLibraryPage.cpp
    ReviewNotesDialog.cpp
    BatchNotesPdfDialog.cpp
    PreferencesDialog.cpp
    Resources.cpp
    SourcesPanel.cpp
    StatusInfoBar.cpp
    Theme.cpp
    TimelineWidget.cpp
    TimelineRangeSlider.cpp
    TransportControls.cpp
    ViewerTransform.cpp
    ViewerWidget.cpp
    VideoFullscreenWindow.cpp
    WelcomeDialog.cpp
    commands/CommandRegistry.cpp
    atk_resources.qrc
)

set(ATK_UI_HEADERS
    AnnotationToolbar.h
    ApplicationSettings.h
    ArrangePlaylistDialog.h
    BatchRenameDialog.h
    BookmarkPanel.h
    BrushSizePill.h
    BurnInOptionsWidget.h
    CompareBar.h
    ComparisonCompositeWidget.h
    ExportDialog.h
    ExportUi.h
    FrameFieldStyle.h
    FrameNumberInput.h
    IconProvider.h
    MainWindow.h
    MediaInformationDialog.h
    NewReviewDialog.h
    NotesPanel.h
    PlaylistCardDelegate.h
    ReviewHeaderBar.h
    ReviewLibraryPage.h
    ReviewNotesDialog.h
    BatchNotesPdfDialog.h
    PreferencesDialog.h
    Resources.h
    SourcesPanel.h
    StatusInfoBar.h
    Theme.h
    TimelineWidget.h
    TimelineRangeSlider.h
    TransportControls.h
    ViewerTransform.h
    ViewerWidget.h
    VideoFullscreenWindow.h
    WelcomeDialog.h
    commands/CommandRegistry.h
)

add_library(atk_ui STATIC ${ATK_UI_SOURCES} ${ATK_UI_HEADERS})
add_library(ATK::ui ALIAS atk_ui)

target_link_libraries(atk_ui
    PUBLIC
        ATK::core
        Qt6::Widgets
)

atk_set_compiler_warnings(atk_ui)

set_target_properties(atk_ui PROPERTIES
    AUTOMOC ON
    AUTORCC ON
    FOLDER "Libraries"
    POSITION_INDEPENDENT_CODE ON
)
