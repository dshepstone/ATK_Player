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
    Application.cpp
    Application.h
)

if(WIN32)
    set(ATK_WINDOWS_ICON "${PROJECT_SOURCE_DIR}/assets/icons/ATK_Player_Icon.ico")
    configure_file(ATKPlayer.rc.in "${CMAKE_CURRENT_BINARY_DIR}/ATKPlayer.rc" @ONLY)
    target_sources(ATKPlayer PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/ATKPlayer.rc")
endif()

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

    # FFmpeg is dynamically linked too, and windeployqt knows nothing about it.
    # Copying the DLLs beside the executable means the build tree is directly
    # runnable -- no global PATH edit, and no risk of an unrelated FFmpeg
    # already on PATH being picked up instead of the one this build was
    # compiled against.
    if(ATK_FFMPEG_RUNTIME_DIR)
        file(GLOB _atk_ffmpeg_dlls "${ATK_FFMPEG_RUNTIME_DIR}/*.dll")
        if(_atk_ffmpeg_dlls)
            add_custom_command(TARGET ATKPlayer POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                        ${_atk_ffmpeg_dlls}
                        "$<TARGET_FILE_DIR:ATKPlayer>"
                COMMENT "Copying the FFmpeg runtime next to ATKPlayer.exe"
                VERBATIM
            )
        endif()
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

if(WIN32)
    install(FILES "${ATK_WINDOWS_ICON}" DESTINATION .)

    # Install only the FFmpeg libraries the application uses. Development
    # tools and unrelated vcpkg DLLs deliberately stay out of the release.
    foreach(_atk_ffmpeg_runtime_name IN ITEMS
            avcodec avformat avutil swresample swscale)
        file(GLOB _atk_ffmpeg_runtime_dll
            "${ATK_FFMPEG_RUNTIME_DIR}/${_atk_ffmpeg_runtime_name}-*.dll")
        list(LENGTH _atk_ffmpeg_runtime_dll _atk_ffmpeg_runtime_count)
        if(NOT _atk_ffmpeg_runtime_count EQUAL 1)
            message(FATAL_ERROR
                "Expected exactly one ${_atk_ffmpeg_runtime_name} runtime DLL in "
                "${ATK_FFMPEG_RUNTIME_DIR}; found ${_atk_ffmpeg_runtime_count}")
        endif()
        install(FILES ${_atk_ffmpeg_runtime_dll} DESTINATION ${CMAKE_INSTALL_BINDIR})
    endforeach()

    install(FILES
        "${PROJECT_SOURCE_DIR}/LICENSE"
        DESTINATION licenses
        RENAME ATK-Player-MIT.txt)
    install(FILES
        "${PROJECT_SOURCE_DIR}/packaging/windows/licenses/Qt-LGPL-3.0.txt"
        "${PROJECT_SOURCE_DIR}/packaging/windows/licenses/FFmpeg-LGPL-2.1.txt"
        "${PROJECT_SOURCE_DIR}/packaging/windows/THIRD_PARTY_NOTICES.txt"
        DESTINATION licenses)
endif()

set(_atk_deploy_app_arguments)
if(WIN32)
    list(APPEND _atk_deploy_app_arguments
        NO_TRANSLATIONS
        NO_COMPILER_RUNTIME
        DEPLOY_TOOL_OPTIONS
            --no-system-d3d-compiler
            --no-system-dxc-compiler
            --no-opengl-sw
            --exclude-plugins ffmpegmediaplugin)
endif()

qt_generate_deploy_app_script(
    TARGET ATKPlayer
    OUTPUT_SCRIPT ATK_DEPLOY_SCRIPT
    NO_UNSUPPORTED_PLATFORM_ERROR
    ${_atk_deploy_app_arguments}
)
install(SCRIPT ${ATK_DEPLOY_SCRIPT})
