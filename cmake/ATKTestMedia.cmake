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
    set(external32 "${ATK_TEST_MEDIA_DIR}/atk_external_32k.wav")
    set(export120 "${ATK_TEST_MEDIA_DIR}/atk_export_120f.mkv")
    set(export23976 "${ATK_TEST_MEDIA_DIR}/atk_export_23976_120f.mkv")

    add_custom_command(
        OUTPUT "${lossless}" "${lossy}" "${sync}" "${review}" "${compare30}" "${compare60}" "${external32}" "${export120}" "${export23976}"
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
                -f lavfi -i "sine=frequency=660:sample_rate=44100:duration=2"
                -c:v ffv1 -pix_fmt yuv420p
                -c:a pcm_s16le
                "${compare30}"

        COMMAND "${ATK_FFMPEG_EXECUTABLE}"
                -hide_banner -loglevel error -y
                -f lavfi -i "testsrc2=size=320x180:rate=60000/1001:duration=2"
                -c:v ffv1 -pix_fmt yuv420p
                "${compare60}"

        COMMAND "${ATK_FFMPEG_EXECUTABLE}"
                -hide_banner -loglevel error -y
                -f lavfi -i "sine=frequency=880:sample_rate=32000:duration=3"
                -c:a pcm_s16le
                "${external32}"

        # Compact export-timing fixtures with enough frames for the human
        # 45-75 regression and an exact fractional-rate counterpart.
        COMMAND "${ATK_FFMPEG_EXECUTABLE}"
                -hide_banner -loglevel error -y
                -f lavfi -i "testsrc2=size=320x180:rate=24"
                -f lavfi -i "sine=frequency=440:sample_rate=48000"
                -frames:v 120 -c:v ffv1 -pix_fmt yuv420p -c:a pcm_s16le -shortest
                "${export120}"

        COMMAND "${ATK_FFMPEG_EXECUTABLE}"
                -hide_banner -loglevel error -y
                -f lavfi -i "testsrc2=size=320x180:rate=24000/1001"
                -frames:v 120 -c:v ffv1 -pix_fmt yuv420p -an
                "${export23976}"

        COMMENT "Generating deterministic test media fixtures"
        VERBATIM
    )

    # Still-image fixtures. One picture each, from the same testsrc2 recipe:
    #   320x180 PNG   -- lossless, so decoded pixels can be compared exactly
    #   64x36 PNG     -- a second size, so two pictures can be told apart
    #   320x180 JPEG  -- the common lossy still path (mjpeg decoder)
    #   64x36 RGBA PNG, fully transparent -- proves alpha composites over black
    #   320x180 EXR   -- only when this FFmpeg build has the EXR encoder
    # Names avoid '%': cmd.exe may expand it in a build command line. The
    # pattern-name case is built by the tests inside a temporary directory.
    set(still_png   "${ATK_TEST_MEDIA_DIR}/atk_still_320x180.png")
    set(still_small "${ATK_TEST_MEDIA_DIR}/atk_still_64x36.png")
    set(still_jpg   "${ATK_TEST_MEDIA_DIR}/atk_still_320x180.jpg")
    set(still_alpha "${ATK_TEST_MEDIA_DIR}/atk_still_alpha_64x36.png")
    set(still_exr   "${ATK_TEST_MEDIA_DIR}/atk_still_320x180.exr")
    set(still_outputs "${still_png}" "${still_small}" "${still_jpg}" "${still_alpha}")
    set(still_commands
        COMMAND "${ATK_FFMPEG_EXECUTABLE}" -hide_banner -loglevel error -y
                -f lavfi -i "testsrc2=size=320x180:rate=24" -frames:v 1 -update 1
                -c:v png -pix_fmt rgb24 "${still_png}"
        COMMAND "${ATK_FFMPEG_EXECUTABLE}" -hide_banner -loglevel error -y
                -f lavfi -i "testsrc2=size=64x36:rate=24" -frames:v 1 -update 1
                -c:v png -pix_fmt rgb24 "${still_small}"
        COMMAND "${ATK_FFMPEG_EXECUTABLE}" -hide_banner -loglevel error -y
                -f lavfi -i "testsrc2=size=320x180:rate=24" -frames:v 1 -update 1
                -c:v mjpeg -pix_fmt yuvj420p -q:v 2 "${still_jpg}"
        COMMAND "${ATK_FFMPEG_EXECUTABLE}" -hide_banner -loglevel error -y
                -f lavfi -i "color=c=white@0.0:size=64x36:rate=24,format=rgba" -frames:v 1 -update 1
                -c:v png -pix_fmt rgba "${still_alpha}")

    # The EXR encoder exists only when FFmpeg was built with zlib. Asking the
    # tool, rather than assuming, keeps an older dependency tree configuring.
    set(ATK_TEST_MEDIA_HAS_EXR OFF)
    execute_process(
        COMMAND "${ATK_FFMPEG_EXECUTABLE}" -hide_banner -encoders
        OUTPUT_VARIABLE _atk_ffmpeg_encoders ERROR_QUIET RESULT_VARIABLE _atk_encoders_result)
    if(_atk_encoders_result EQUAL 0 AND _atk_ffmpeg_encoders MATCHES " V[A-Z.]+ exr ")
        set(ATK_TEST_MEDIA_HAS_EXR ON)
        list(APPEND still_outputs "${still_exr}")
        list(APPEND still_commands
            COMMAND "${ATK_FFMPEG_EXECUTABLE}" -hide_banner -loglevel error -y
                    -f lavfi -i "testsrc2=size=320x180:rate=24" -frames:v 1 -update 1
                    -c:v exr -pix_fmt gbrpf32le "${still_exr}")
    endif()
    set(ATK_TEST_MEDIA_HAS_EXR ${ATK_TEST_MEDIA_HAS_EXR} PARENT_SCOPE)

    add_custom_command(
        OUTPUT ${still_outputs}
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${ATK_TEST_MEDIA_DIR}"
        ${still_commands}
        COMMENT "Generating still-image test fixtures"
        VERBATIM
    )

    add_custom_target(atk_test_media DEPENDS "${lossless}" "${lossy}" "${sync}" "${review}" "${compare30}" "${compare60}" "${external32}" "${export120}" "${export23976}" ${still_outputs})
    set_target_properties(atk_test_media PROPERTIES FOLDER "Tests")

    # Validate what was produced rather than trusting the recipe. If a future
    # FFmpeg changes a default, the test suite should say so here rather than
    # failing somewhere confusing inside the decoder tests.
    if(ATK_FFPROBE_EXECUTABLE)
        add_test(NAME fixture_validation
            COMMAND "${CMAKE_COMMAND}"
                    -DFFPROBE=${ATK_FFPROBE_EXECUTABLE}
                    -DMEDIA_DIR=${ATK_TEST_MEDIA_DIR}
                    -DHAS_EXR=${ATK_TEST_MEDIA_HAS_EXR}
                    -P "${ATK_TEST_MEDIA_MODULE_DIR}/ValidateTestMedia.cmake"
        )
        set_tests_properties(fixture_validation PROPERTIES FIXTURES_SETUP atk_media)
    endif()
endfunction()
