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
    tst_timelinemodel.cpp
    tst_playbackrange.cpp
    tst_commanddefinitions.cpp
    tst_apicommands.cpp
)

foreach(test_source IN LISTS ATK_TEST_SOURCES)
    get_filename_component(test_name "${test_source}" NAME_WE)

    qt_add_executable(${test_name} ${test_source})

    target_link_libraries(${test_name}
        PRIVATE
            ATK::core
            Qt6::Test
    )

    atk_set_compiler_warnings(${test_name})

    set_target_properties(${test_name} PROPERTIES
        WIN32_EXECUTABLE FALSE
        MACOSX_BUNDLE FALSE
        FOLDER "Tests"
    )

    add_test(NAME ${test_name} COMMAND ${test_name})

    # On Windows the test executables need the Qt DLLs on PATH at run time.
    if(WIN32)
        set_tests_properties(${test_name} PROPERTIES
            ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>"
        )
    endif()
endforeach()
