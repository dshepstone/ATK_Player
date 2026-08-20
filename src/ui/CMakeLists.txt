# ---------------------------------------------------------------------------
# atk_ui -- widgets.
#
# Depends on atk_core; nothing in atk_core depends on this target. The
# dependency runs in one direction only, so the models can be tested, and later
# driven by the external API, with no UI present.
# ---------------------------------------------------------------------------

set(ATK_UI_SOURCES
    MainWindow.cpp
    SourcesPanel.cpp
    StatusInfoBar.cpp
    Theme.cpp
    TimelineWidget.cpp
    TransportBar.cpp
    ViewerWidget.cpp
    commands/CommandRegistry.cpp
)

set(ATK_UI_HEADERS
    MainWindow.h
    SourcesPanel.h
    StatusInfoBar.h
    Theme.h
    TimelineWidget.h
    TransportBar.h
    ViewerWidget.h
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
    FOLDER "Libraries"
    POSITION_INDEPENDENT_CODE ON
)
