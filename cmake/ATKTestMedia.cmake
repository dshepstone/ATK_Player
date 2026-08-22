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

# Captured here, at include time. Inside a function() CMAKE_CURRENT_LIST_DIR
# refers to the *calling* file, which would resolve the validation script
# against the project root and fail to find it.
set(ATK_TEST_MEDIA_MODULE_DIR "${CMAKE_CURRENT_LIST_DIR}")

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
    set(sync     "${ATK_TEST_MEDIA_DIR}/atk_sync_10s.mkv")
    set(review   "${ATK_TEST_MEDIA_DIR}/atk_review_10s.mkv")
    set(compare30 "${ATK_TEST_MEDIA_DIR}/atk_compare_30fps.mkv")
    set(compare60 "${ATK_TEST_MEDIA_DIR}/atk_compare_5994fps.mkv")

    add_custom_command(
        OUTPUT "${lossless}" "${lossy}" "${sync}" "${review}" "${compare30}" "${compare60}"
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

        COMMAND "${ATK_FFMPEG_EXECUTABLE}"
                -hide_banner -loglevel error -y
                -f lavfi -i "testsrc2=size=1920x1080:rate=24:duration=10,drawbox=color=white:t=fill:enable='lt(mod(t,1),0.08)'"
                -f lavfi -i "sine=frequency=1:beep_factor=1000:sample_rate=48000:duration=10"
                -c:v ffv1 -pix_fmt yuv420p
                -c:a pcm_s16le
                "${sync}"

        # Audio-review fixture: known amplitude segments at known media times.
        #
        # Waveform rendering and scrub-audio accuracy both need audio whose
        # content at a given moment is known in advance. Alternating silence and
        # tone on a fixed grid lets a test assert "at 1.5 s this is loud, at
        # 2.5 s it is silent" without depending on what an encoder chose.
        #
        #   0.5-1.0  quiet tone      1.5-2.0  loud tone
        #   3.0-3.5  quiet tone      8.0-8.5  loud tone
        #   everything else silent
        COMMAND "${ATK_FFMPEG_EXECUTABLE}"
                -hide_banner -loglevel error -y
                -f lavfi -i "testsrc2=size=320x180:rate=24:duration=10"
                -f lavfi -i "sine=frequency=440:sample_rate=48000:duration=10"
                -filter_complex "[1:a]volume=0:enable='not(between(t,0.5,1.0)+between(t,1.5,2.0)+between(t,3.0,3.5)+between(t,8.0,8.5))',volume=0.3:enable='between(t,0.5,1.0)+between(t,3.0,3.5)'[a]"
                -map 0:v -map "[a]"
                -c:v ffv1 -pix_fmt yuv420p
                -c:a pcm_s16le
                "${review}"

        # Short unequal-rate comparison fixtures. Video-only B is intentional:
        # comparison audio must remain exclusively on the authoritative A lane.
        COMMAND "${ATK_FFMPEG_EXECUTABLE}"
                -hide_banner -loglevel error -y
                -f lavfi -i "testsrc2=size=320x180:rate=30:duration=2"
                -c:v ffv1 -pix_fmt yuv420p
                "${compare30}"

        COMMAND "${ATK_FFMPEG_EXECUTABLE}"
                -hide_banner -loglevel error -y
                -f lavfi -i "testsrc2=size=320x180:rate=60000/1001:duration=2"
                -c:v ffv1 -pix_fmt yuv420p
                "${compare60}"

        COMMENT "Generating deterministic test media fixtures"
        VERBATIM
    )

    add_custom_target(atk_test_media DEPENDS "${lossless}" "${lossy}" "${sync}" "${review}" "${compare30}" "${compare60}")
    set_target_properties(atk_test_media PROPERTIES FOLDER "Tests")

    # Validate what was produced rather than trusting the recipe. If a future
    # FFmpeg changes a default, the test suite should say so here rather than
    # failing somewhere confusing inside the decoder tests.
    if(ATK_FFPROBE_EXECUTABLE)
        add_test(NAME fixture_validation
            COMMAND "${CMAKE_COMMAND}"
                    -DFFPROBE=${ATK_FFPROBE_EXECUTABLE}
                    -DMEDIA_DIR=${ATK_TEST_MEDIA_DIR}
                    -P "${ATK_TEST_MEDIA_MODULE_DIR}/ValidateTestMedia.cmake"
        )
        set_tests_properties(fixture_validation PROPERTIES FIXTURES_SETUP atk_media)
    endif()
endfunction()
