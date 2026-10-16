# ---------------------------------------------------------------------------
# ATKPlayer -- the application executable.
#
# Contains only main.cpp. Everything else lives in atk_core / atk_ui so it can
# be linked into tests and, later, into other front ends.
# ---------------------------------------------------------------------------

option(ATK_CONSOLE_LOGGING
    "Build ATKPlayer as a console application so log output appears in the terminal (Windows)."
    OFF)

# Reverse-DNS on a domain the publisher controls. It identifies the app to
# Gatekeeper, notarisation, Launch Services and the .atkproj file type, so it
# must not change between releases once one has shipped.
set(ATK_MACOS_BUNDLE_ID "ca.shepstone.atkplayer" CACHE STRING
    "macOS CFBundleIdentifier for ATKPlayer.app.")

qt_add_executable(ATKPlayer
    main.cpp
    Application.cpp
    Application.h
)

if(WIN32)
    set(ATK_WINDOWS_ICON "${PROJECT_SOURCE_DIR}/assets/icons/ATK_Player_Icon.ico")
    configure_file(ATKPlayer.rc.in "${CMAKE_CURRENT_BINARY_DIR}/ATKPlayer.rc" @ONLY)
    target_sources(ATKPlayer PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/ATKPlayer.rc")
elseif(APPLE)
    # Finder icon. The committed artwork is 32x32 (see assets/icons/README.md),
    # so the .icns carries only the sizes that really exist; macOS scales it.
    set(ATK_MACOS_ICON "${CMAKE_CURRENT_BINARY_DIR}/ATKPlayer.icns")
    set(_atk_iconset "${CMAKE_CURRENT_BINARY_DIR}/ATKPlayer.iconset")
    set(_atk_icon_source "${PROJECT_SOURCE_DIR}/assets/icons/ATK_Player_Icon.png")
    add_custom_command(
        OUTPUT "${ATK_MACOS_ICON}"
        COMMAND "${CMAKE_COMMAND}" -E rm -rf "${_atk_iconset}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${_atk_iconset}"
        COMMAND sips -z 16 16 "${_atk_icon_source}" --out "${_atk_iconset}/icon_16x16.png"
        COMMAND sips -z 32 32 "${_atk_icon_source}" --out "${_atk_iconset}/icon_16x16@2x.png"
        COMMAND sips -z 32 32 "${_atk_icon_source}" --out "${_atk_iconset}/icon_32x32.png"
        COMMAND iconutil -c icns "${_atk_iconset}" -o "${ATK_MACOS_ICON}"
        DEPENDS "${_atk_icon_source}"
        COMMENT "Building ATKPlayer.icns"
        VERBATIM
    )
    set_source_files_properties("${ATK_MACOS_ICON}" PROPERTIES
        MACOSX_PACKAGE_LOCATION Resources)
    target_sources(ATKPlayer PRIVATE "${ATK_MACOS_ICON}")
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
    MACOSX_BUNDLE_GUI_IDENTIFIER "${ATK_MACOS_BUNDLE_ID}"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
    MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
    MACOSX_BUNDLE_ICON_FILE "ATKPlayer.icns"
    MACOSX_BUNDLE_COPYRIGHT "Copyright ${ATK_RELEASE_PUBLISHER}"
    MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/Info.plist.in"
)

if(APPLE)
    # The installed bundle finds Qt and FFmpeg inside itself. The build tree
    # keeps CMake's build RPATH (absolute paths to Qt and vcpkg), so it runs
    # without deployment.
    set_target_properties(ATKPlayer PROPERTIES
        INSTALL_RPATH "@executable_path/../Frameworks")
endif()

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

    # FFmpeg's zlib (PNG/EXR/deflate-TIFF stills). The pinned vcpkg zlib
    # (1.3.2) names it z.dll, or zd.dll in the debug tree; older ports used
    # zlib1.dll / zlibd1.dll, so either naming is accepted.
    file(GLOB _atk_zlib_runtime_dll
        "${ATK_FFMPEG_RUNTIME_DIR}/z.dll" "${ATK_FFMPEG_RUNTIME_DIR}/zd.dll"
        "${ATK_FFMPEG_RUNTIME_DIR}/zlib1.dll" "${ATK_FFMPEG_RUNTIME_DIR}/zlibd1.dll")
    list(LENGTH _atk_zlib_runtime_dll _atk_zlib_runtime_count)
    if(NOT _atk_zlib_runtime_count EQUAL 1)
        message(FATAL_ERROR
            "Expected exactly one zlib runtime DLL in ${ATK_FFMPEG_RUNTIME_DIR}; "
            "found ${_atk_zlib_runtime_count}")
    endif()
    install(FILES ${_atk_zlib_runtime_dll} DESTINATION ${CMAKE_INSTALL_BINDIR})

    install(FILES
        "${PROJECT_SOURCE_DIR}/LICENSE"
        DESTINATION licenses
        RENAME ATK-Player-MIT.txt)
    install(FILES
        "${PROJECT_SOURCE_DIR}/packaging/windows/licenses/Qt-LGPL-3.0.txt"
        "${PROJECT_SOURCE_DIR}/packaging/windows/licenses/FFmpeg-LGPL-2.1.txt"
        "${PROJECT_SOURCE_DIR}/packaging/windows/licenses/zlib.txt"
        "${PROJECT_SOURCE_DIR}/packaging/windows/THIRD_PARTY_NOTICES.txt"
        DESTINATION licenses)

    # Packaging supplies notices and matching source. Ordinary dev builds do
    # not download sources or change runtime compilation/linkage.
    set(ATK_DEPENDENCY_MATERIALS_DIR "" CACHE PATH
        "Generated dependency notices and corresponding-source directory for Windows packaging.")
    if(ATK_DEPENDENCY_MATERIALS_DIR)
        if(NOT EXISTS "${ATK_DEPENDENCY_MATERIALS_DIR}/SHA256SUMS.txt")
            message(FATAL_ERROR "Dependency materials are incomplete; run prepare-dependency-materials.py.")
        endif()
        install(DIRECTORY "${ATK_DEPENDENCY_MATERIALS_DIR}/" DESTINATION licenses/dependencies)
    endif()
elseif(APPLE)
    # Licence texts travel inside the bundle. The licence texts are the same
    # for both platforms and shared; the notice is macOS's own, because the
    # Windows one describes DLLs, the MSI and its source materials.
    set(_atk_bundle_licenses "ATKPlayer.app/Contents/Resources/licenses")
    install(FILES
        "${PROJECT_SOURCE_DIR}/LICENSE"
        DESTINATION ${_atk_bundle_licenses}
        RENAME ATK-Player-MIT.txt)
    install(FILES
        "${PROJECT_SOURCE_DIR}/packaging/windows/licenses/Qt-LGPL-3.0.txt"
        "${PROJECT_SOURCE_DIR}/packaging/windows/licenses/FFmpeg-LGPL-2.1.txt"
        "${PROJECT_SOURCE_DIR}/packaging/windows/licenses/zlib.txt"
        "${PROJECT_SOURCE_DIR}/packaging/macos/THIRD_PARTY_NOTICES.txt"
        DESTINATION ${_atk_bundle_licenses})
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
elseif(APPLE AND ATK_FFMPEG_RUNTIME_DIR)
    # ATKPlayer links @rpath/libav*.dylib; tell macdeployqt where those are so
    # it copies them into Contents/Frameworks. packaging/macos/build-dmg.sh
    # then removes Qt's own FFmpeg media plugin and checks the result.
    list(APPEND _atk_deploy_app_arguments
        DEPLOY_TOOL_OPTIONS
            "-libpath=${ATK_FFMPEG_RUNTIME_DIR}")
endif()

qt_generate_deploy_app_script(
    TARGET ATKPlayer
    OUTPUT_SCRIPT ATK_DEPLOY_SCRIPT
    NO_UNSUPPORTED_PLATFORM_ERROR
    ${_atk_deploy_app_arguments}
)
install(SCRIPT ${ATK_DEPLOY_SCRIPT})
