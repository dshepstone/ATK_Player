# ---------------------------------------------------------------------------
# ATKTestMedia
#
# Generates deterministic media fixtures for the integration tests using the
# ffmpeg command-line tool that vcpkg built alongside the libraries.
#
# Generating them beats committing sample videos: binary blobs bloat the
# repository forever, and a checked-in file cannot be verified to still contain
# what the tests assume. These are produced from an exact recipe and validated
# with ffprobe, so a test failure means the player changed, not the fixture.
#
# Output goes to ${CMAKE_BINARY_DIR}/test-media, which .gitignore excludes.
# ---------------------------------------------------------------------------

# The vcpkg ffmpeg port installs its tools under tools/ffmpeg.
find_program(ATK_FFMPEG_EXECUTABLE
    NAMES ffmpeg
    PATHS "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/tools/ffmpeg"
          "${CMAKE_BINARY_DIR}/vcpkg_installed/${VCPKG_TARGET_TRIPLET}/tools/ffmpeg"
    PATH_SUFFIXES bin
    DOC "ffmpeg command-line tool, used to generate test fixtures"
)

find_program(ATK_FFPROBE_EXECUTABLE
    NAMES ffprobe
    PATHS "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/tools/ffmpeg"
          "${CMAKE_BINARY_DIR}/vcpkg_installed/${VCPKG_TARGET_TRIPLET}/tools/ffmpeg"
    PATH_SUFFIXES bin
    DOC "ffprobe command-line tool, used to validate test fixtures"
)

set(ATK_TEST_MEDIA_DIR "${CMAKE_BINARY_DIR}/test-media")

# ---------------------------------------------------------------------------
# atk_add_test_media()
#
# Defines the `atk_test_media` target that produces the fixtures. Tests that
# need them should depend on it.
#
# The clips are deliberately tiny and exact:
#   2.0 seconds at 24 fps  ->  exactly 48 video frames
#   640x360, so decoding is fast but the frame is big enough to be meaningful
#   48 kHz audio, the rate most devices run at natively
#
# Codecs are chosen to need no GPL or external encoder:
#   FFV1 + PCM in Matroska  -- lossless, so a decoded pixel can be asserted on
#   MPEG-4 Part 2 + AAC in MP4 -- a lossy, B-frame-free container path
# ---------------------------------------------------------------------------
function(atk_add_test_media)
    if(NOT ATK_FFMPEG_EXECUTABLE)
        message(WARNING
            "ffmpeg tool not found; media integration tests will be skipped. "
            "It is provided by the vcpkg ffmpeg port's 'ffmpeg' feature.")
        return()
    endif()

    set(lossless "${ATK_TEST_MEDIA_DIR}/atk_fixture_48f.mkv")
    set(lossy    "${ATK_TEST_MEDIA_DIR}/atk_fixture_48f.mp4")

    add_custom_command(
        OUTPUT "${lossless}" "${lossy}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${ATK_TEST_MEDIA_DIR}"

        # Lossless fixture: FFV1 video, PCM audio, Matroska.
        COMMAND "${ATK_FFMPEG_EXECUTABLE}"
                -hide_banner -loglevel error -y
                -f lavfi -i "testsrc2=size=640x360:rate=24:duration=2"
                -f lavfi -i "sine=frequency=440:sample_rate=48000:duration=2"
                -c:v ffv1 -pix_fmt yuv420p
                -c:a pcm_s16le
                "${lossless}"

        # Lossy fixture: MPEG-4 Part 2 video, AAC audio, MP4.
        COMMAND "${ATK_FFMPEG_EXECUTABLE}"
                -hide_banner -loglevel error -y
                -f lavfi -i "testsrc2=size=640x360:rate=24:duration=2"
                -f lavfi -i "sine=frequency=440:sample_rate=48000:duration=2"
                -c:v mpeg4 -pix_fmt yuv420p -q:v 3
                -c:a aac -b:a 96k
                "${lossy}"

        COMMENT "Generating deterministic test media fixtures"
        VERBATIM
    )

    add_custom_target(atk_test_media DEPENDS "${lossless}" "${lossy}")
    set_target_properties(atk_test_media PROPERTIES FOLDER "Tests")

    # Validate what was produced rather than trusting the recipe. If a future
    # FFmpeg changes a default, the test suite should say so here rather than
    # failing somewhere confusing inside the decoder tests.
    if(ATK_FFPROBE_EXECUTABLE)
        add_test(NAME fixture_validation
            COMMAND "${CMAKE_COMMAND}"
                    -DFFPROBE=${ATK_FFPROBE_EXECUTABLE}
                    -DMEDIA_DIR=${ATK_TEST_MEDIA_DIR}
                    -P "${CMAKE_CURRENT_LIST_DIR}/ValidateTestMedia.cmake"
        )
        set_tests_properties(fixture_validation PROPERTIES FIXTURES_SETUP atk_media)
    endif()
endfunction()
