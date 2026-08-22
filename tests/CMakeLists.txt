# ---------------------------------------------------------------------------
# Unit tests.
#
# One executable per test class, which is what Qt Test's QTEST_*_MAIN macros
# expect and what keeps a crash in one suite from hiding the others.
#
# Only atk_core is under test here. The widgets are covered by manual
# verification in Phase 0; GUI tests arrive with M2, once there is behaviour
# worth asserting on beyond construction.
# ---------------------------------------------------------------------------

set(ATK_TEST_SOURCES
    tst_timecode.cpp
    tst_framecache.cpp
    tst_playbackqueue.cpp
    tst_waveformdata.cpp
    tst_timelinemodel.cpp
    tst_playbackcontroller.cpp
    tst_playbackrange.cpp
    tst_bookmark.cpp
    tst_commanddefinitions.cpp
    tst_apicommands.cpp
    tst_project.cpp
)

# Suites that include FFmpeg-facing headers.
#
# atk_core links FFmpeg PRIVATE so the UI cannot reach libav*, which means these
# need the include directories spelled out. Keeping them in a separate list is
# also the reminder that a test appearing here crossed that boundary
# deliberately.
set(ATK_FFMPEG_TEST_SOURCES
    tst_ffmpegutil.cpp
    tst_mediadecoder.cpp
    tst_audioreview.cpp
    tst_scrubaudio.cpp
    tst_playbackinteraction.cpp
    tst_playlistprobe.cpp
    tst_export.cpp
)

set(ATK_UI_TEST_SOURCES
    tst_settings.cpp
    tst_bookmarkeditor.cpp
    tst_timelinewidgets.cpp
    tst_viewertransform.cpp
    tst_playlistend.cpp
    tst_videofullscreen.cpp
    tst_comparison.cpp
)

foreach(test_source IN LISTS ATK_TEST_SOURCES ATK_FFMPEG_TEST_SOURCES ATK_UI_TEST_SOURCES)
    get_filename_component(test_name "${test_source}" NAME_WE)

    qt_add_executable(${test_name} ${test_source})

    # Every executable shares the build-tree bin directory. On Windows,
    # windeployqt writes Qt DLLs there after linking ATKPlayer while vcpkg's
    # applocal step scans the same DLLs after linking each test. Serialize that
    # deployment boundary so parallel Ninja builds cannot open a DLL while it
    # is being replaced.
    if(WIN32 AND TARGET ATKPlayer)
        add_dependencies(${test_name} ATKPlayer)
    endif()

    target_link_libraries(${test_name}
        PRIVATE
            ATK::core
            Qt6::Test
    )
    if(test_source IN_LIST ATK_UI_TEST_SOURCES)
        target_link_libraries(${test_name} PRIVATE ATK::ui Qt6::Widgets)
    endif()

    atk_set_compiler_warnings(${test_name})

    if(test_source IN_LIST ATK_FFMPEG_TEST_SOURCES)
        target_include_directories(${test_name} PRIVATE ${FFMPEG_INCLUDE_DIRS})
        target_link_directories(${test_name} PRIVATE ${FFMPEG_LIBRARY_DIRS})
        target_link_libraries(${test_name} PRIVATE ${FFMPEG_LIBRARIES})

        # Where the generated fixtures live. Passed in rather than discovered at
        # run time, so a test cannot silently look in the wrong place and pass
        # by skipping.
        target_compile_definitions(${test_name}
            PRIVATE ATK_TEST_MEDIA_DIR="${ATK_TEST_MEDIA_DIR}")

        if(TARGET atk_test_media)
            add_dependencies(${test_name} atk_test_media)
        endif()
    endif()

    set_target_properties(${test_name} PROPERTIES
        WIN32_EXECUTABLE FALSE
        MACOSX_BUNDLE FALSE
        FOLDER "Tests"
    )

    if(test_name STREQUAL "tst_playbackinteraction" OR test_name STREQUAL "tst_bookmarkeditor"
       OR test_name STREQUAL "tst_playlistend" OR test_name STREQUAL "tst_videofullscreen"
       OR test_name STREQUAL "tst_comparison" OR test_name STREQUAL "tst_export")
        # This GUI-linked Windows test has no inherited console handle on the
        # hosted runner, so CTest cannot show Qt Test's assertion text. Keep a
        # text report beside the executable; the always-uploaded CI artifact
        # then preserves the exact failing case without weakening assertions.
        add_test(NAME ${test_name} COMMAND ${test_name}
            -o "$<TARGET_FILE_DIR:${test_name}>/${test_name}-results.txt,txt")
    else()
        add_test(NAME ${test_name} COMMAND ${test_name})
    endif()

    if(test_name STREQUAL "tst_playlistend" OR test_name STREQUAL "tst_videofullscreen"
       OR test_name STREQUAL "tst_comparison")
        target_compile_definitions(${test_name}
            PRIVATE ATK_TEST_MEDIA_DIR="${ATK_TEST_MEDIA_DIR}"
                    ATK_VERSION_STRING="${ATK_VERSION_STRING}")
        if(TARGET atk_test_media)
            add_dependencies(${test_name} atk_test_media)
        endif()
    endif()

    # On Windows the test executables need the Qt and FFmpeg DLLs on PATH at run
    # time. Set per-test rather than by touching the developer's global PATH.
    if(WIN32)
        set(test_path_entries
            "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")
        if(ATK_FFMPEG_RUNTIME_DIR)
            list(APPEND test_path_entries
                "PATH=path_list_prepend:${ATK_FFMPEG_RUNTIME_DIR}")
        endif()

        set_tests_properties(${test_name} PROPERTIES
            ENVIRONMENT_MODIFICATION "${test_path_entries}"
        )
    endif()
endforeach()
