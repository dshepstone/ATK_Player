# ---------------------------------------------------------------------------
# ATKPlayer -- the application executable.
#
# Contains only main.cpp. Everything else lives in atk_core / atk_ui so it can
# be linked into tests and, later, into other front ends.
# ---------------------------------------------------------------------------

option(ATK_CONSOLE_LOGGING
    "Build ATKPlayer as a console application so log output appears in the terminal (Windows)."
    OFF)

qt_add_executable(ATKPlayer
    main.cpp
)

target_link_libraries(ATKPlayer
    PRIVATE
        ATK::ui
)

atk_set_compiler_warnings(ATKPlayer)

set_target_properties(ATKPlayer PROPERTIES
    OUTPUT_NAME "ATKPlayer"
    FOLDER "Application"
    # A GUI application on Windows, an app bundle on macOS.
    WIN32_EXECUTABLE $<NOT:$<BOOL:${ATK_CONSOLE_LOGGING}>>
    MACOSX_BUNDLE TRUE
    MACOSX_BUNDLE_BUNDLE_NAME "ATK Player"
    MACOSX_BUNDLE_GUI_IDENTIFIER "local.atkplayer.ATKPlayer"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
)

# ---------------------------------------------------------------------------
# Running from the build tree
#
# On Windows the executable needs the Qt DLLs and the platform plugin beside it.
# windeployqt copies exactly what this build references, so the app can be
# launched by double-clicking it without Qt on PATH.
# ---------------------------------------------------------------------------
if(WIN32)
    option(ATK_RUN_WINDEPLOYQT
        "Copy the Qt runtime next to ATKPlayer.exe after building, so it can run from the build tree."
        ON)

    if(ATK_RUN_WINDEPLOYQT AND TARGET Qt6::windeployqt)
        add_custom_command(TARGET ATKPlayer POST_BUILD
            COMMAND Qt6::windeployqt
                    --no-compiler-runtime
                    --no-translations
                    --no-system-d3d-compiler
                    "$<TARGET_FILE:ATKPlayer>"
            COMMENT "Deploying the Qt runtime next to ATKPlayer.exe"
            VERBATIM
        )
    endif()
endif()

# ---------------------------------------------------------------------------
# Install rules. Packaging (milestone M6+) builds on these rather than
# reimplementing the layout.
# ---------------------------------------------------------------------------
include(GNUInstallDirs)

install(TARGETS ATKPlayer
    BUNDLE  DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

qt_generate_deploy_app_script(
    TARGET ATKPlayer
    OUTPUT_SCRIPT ATK_DEPLOY_SCRIPT
    NO_UNSUPPORTED_PLATFORM_ERROR
)
install(SCRIPT ${ATK_DEPLOY_SCRIPT})
