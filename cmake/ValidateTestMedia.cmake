# ---------------------------------------------------------------------------
# ValidateTestMedia
#
# Run by CTest as the `fixture_validation` test. Confirms with ffprobe that the
# generated fixtures actually contain what the integration tests assume, so a
# change in FFmpeg's defaults is reported here rather than as a mysterious
# failure inside a decoder test.
#
# Invoked as:
#   cmake -DFFPROBE=... -DMEDIA_DIR=... -P ValidateTestMedia.cmake
# ---------------------------------------------------------------------------

if(NOT FFPROBE OR NOT MEDIA_DIR)
    message(FATAL_ERROR "FFPROBE and MEDIA_DIR must both be provided")
endif()

# Queries one ffprobe field and compares it against the expected value.
function(expect_field file stream_selector field expected)
    execute_process(
        COMMAND "${FFPROBE}" -v error
                -select_streams ${stream_selector}
                -show_entries ${field}
                -of default=noprint_wrappers=1:nokey=1
                "${file}"
        OUTPUT_VARIABLE actual
        ERROR_VARIABLE probe_error
        RESULT_VARIABLE probe_result
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )

    if(NOT probe_result EQUAL 0)
        message(FATAL_ERROR "ffprobe failed on ${file}: ${probe_error}")
    endif()

    if(NOT actual STREQUAL expected)
        message(FATAL_ERROR
            "${file}: ${field} is '${actual}', expected '${expected}'")
    endif()

    message(STATUS "  ${field} = ${actual}")
endfunction()

# --- Lossless fixture: FFV1 + PCM in Matroska ------------------------------
set(lossless "${MEDIA_DIR}/atk_fixture_48f.mkv")
if(NOT EXISTS "${lossless}")
    message(FATAL_ERROR "Missing fixture: ${lossless}")
endif()

message(STATUS "Validating ${lossless}")
expect_field("${lossless}" "v:0" "stream=codec_name" "ffv1")
expect_field("${lossless}" "v:0" "stream=width"      "640")
expect_field("${lossless}" "v:0" "stream=height"     "360")
expect_field("${lossless}" "v:0" "stream=r_frame_rate" "24/1")
expect_field("${lossless}" "a:0" "stream=codec_name" "pcm_s16le")
expect_field("${lossless}" "a:0" "stream=sample_rate" "48000")

# 48 frames is the number the frame-stepping and seek tests count on.
execute_process(
    COMMAND "${FFPROBE}" -v error -select_streams v:0
            -count_frames -show_entries stream=nb_read_frames
            -of default=noprint_wrappers=1:nokey=1
            "${lossless}"
    OUTPUT_VARIABLE frame_count
    RESULT_VARIABLE count_result
    OUTPUT_STRIP_TRAILING_WHITESPACE
)
if(NOT count_result EQUAL 0)
    message(FATAL_ERROR "ffprobe could not count frames in ${lossless}")
endif()
if(NOT frame_count STREQUAL "48")
    message(FATAL_ERROR
        "${lossless} contains ${frame_count} video frames, expected 48")
endif()
message(STATUS "  decoded frame count = ${frame_count}")

# --- Lossy fixture: MPEG-4 Part 2 + AAC in MP4 -----------------------------
set(lossy "${MEDIA_DIR}/atk_fixture_48f.mp4")
if(NOT EXISTS "${lossy}")
    message(FATAL_ERROR "Missing fixture: ${lossy}")
endif()

message(STATUS "Validating ${lossy}")
expect_field("${lossy}" "v:0" "stream=codec_name" "mpeg4")
expect_field("${lossy}" "v:0" "stream=width"      "640")
expect_field("${lossy}" "v:0" "stream=height"     "360")
expect_field("${lossy}" "a:0" "stream=codec_name" "aac")

message(STATUS "Test media fixtures validated")
