# Still-image sources

Plan and design record for opening single image files as review sources.
Branch: `feature/image-sources`.

## Scope

**Phase 1 (this branch):** a single image file — `png`, `jpg`, `jpeg`, `tif`,
`tiff`, `bmp`, `tga`, `webp`, `exr` — opens anywhere a video does: Open Media,
Add to Playlist, Relink, drag and drop, `.atkproj`, A/B comparison and every
export kind.

**Not in scope:** URLs and online video. Image sequences are phase 2, below.
Phase 1 never opens a numbered pattern by itself: a sequence is only ever
opened when the user explicitly chooses one.

## Prerequisite: FFmpeg zlib

The pinned vcpkg FFmpeg is configured with `--disable-autodetect` and, until
this branch, did not enable zlib. Without zlib FFmpeg has **no PNG decoder or
encoder, no EXR decoder**, and cannot inflate deflate-compressed TIFF. The
ffmpeg CLI therefore could not generate PNG fixtures either.

The branch adds the vcpkg `ffmpeg[zlib]` feature. zlib is under the permissive
zlib license. It is not GPL or nonfree, and it does not change FFmpeg's
LGPL-2.1 configuration. The consequences:

- `zlib1.dll` (Debug: `zlibd1.dll`) is a new runtime DLL next to the FFmpeg
  DLLs. It is installed by `src/app/CMakeLists.txt` and is expected by the
  packaging scripts.
- The zlib notice is added to `docs/THIRD_PARTY_LICENSES.md` and to the
  packaged third-party notices.
- CI keys its vcpkg cache on `hashFiles('vcpkg.json')`, so the cache rebuilds
  once on its own.

## Design

### One entry point: `MediaDecoder`

All five consumers open media through `MediaDecoder::open()`:

- `DecoderWorker` (playback, and the A/B B-lane through `CompareVideoLane`)
- `PlaylistProbeWorker` (playlist availability, and relink validation)
- `ExportRenderer`

Still images are handled inside `MediaDecoder`, so the playlist, A/B, export
and relink paths gain still support without their own image code. The only
change these callers see is one extra *value* argument, the hold options (see
below), because a still's extent is not a property of the file.

### Classification

`media/ImageSource.h` classifies by file extension, case-insensitively, against
one list. The decoder, the file dialogs, drag and drop and the project
serializer all use that list, so they cannot disagree about what is a still.
Classifying by extension is deliberate:

- It is decided before opening anything. Audio readers can refuse a still
  without a probe, and the serializer knows which entries carry hold data.
- It controls the most important choice: a still is opened with
  **`image2` and `pattern_type=none`**. With the default pattern type, image2
  treats `%d`-style text in a name as a sequence pattern, and auto-probing can
  route names such as `shot_%03d.png` or `100%.png` to the wrong file or fail.
  `pattern_type=none` means the path is exactly one file.

A file whose content does not match its extension fails to decode, with a
clear error. It is never guessed at.

### Decode once, hold N frames

`open()` demuxes and decodes the single picture, converts it to the display
format, composites any alpha over black (the same black canvas export already
uses), and then **releases every FFmpeg context**. A still keeps no demuxer
state, so it cannot be misused from another thread later, and the held
`QImage` is implicitly shared rather than copied per frame.

The synthesized stream has these properties:

| Field | Value |
|---|---|
| `frameRate` | the hold rate (default 24/1, rational) |
| `videoTimeBase` | `1 / frameRate`, so `ptsTicks == frameIndex` |
| `videoStartTime` | 0 |
| `frameCount` | the hold length N, exact |
| `frameCountSource` | new `FrameCountSource::Synthesized` (exact) |
| `durationUs` | `N` frames through the exact rational rate |
| `hasAudio` | false |
| `isStillImage` | true |

`nextVideoFrame()`, `frameAtIndex()` and `seekToFrameIndex()` serve the held
picture with exact indices and PTS. An index past the end clamps to `N - 1`,
which matches the "closest frame available" behaviour of video sources.
`nextVideoFrame()` returns `EndOfFile` after frame `N - 1`, so playback,
Loop off and Loop on, and playlist advance all behave as they do for a
video-only clip. `countFramesExactly()` returns N without decoding. No demuxer
seek is performed, so `seekOperationCount()` stays 0.

Frame stepping is still exact. Each index is a distinct presentation with its
own PTS, so stepping back from frame k lands on k-1 rather than on "time minus
1/fps".

### EXR

EXR is scene-linear. The FFmpeg `exr` decoder's `apply_trc` option is set to
sRGB, so float pixels are encoded for display rather than shown as raw linear
values (which look dark and clip). This is a display transform only. It is
not colour management, and OCIO is out of scope.

### Hold options and where they live

```
struct ImageSourceOptions { int64_t holdFrames; FrameRate frameRate; };
```

- **Minimum 10 frames**, which is `TimelineViewport`'s minimum span. Shorter
  holds would leave the review range unable to satisfy its own invariant.
  Maximum 100 000.
- **Default 48 frames at 24/1** (two seconds).
- **The preference is the default for newly added stills.**
  `ApplicationSettings::stillImageHoldFrames()` is edited in Preferences →
  Review. Changing it never rewrites existing sources.
- **The hold is stored per source.** `MediaSource` carries its
  `ImageSourceOptions`. `.atkproj` writes an optional `"still"` object for
  still-image sources:

  ```json
  "still": { "holdFrames": 48, "frameRate": { "numerator": 24, "denominator": 1 } }
  ```

  **Why per source:** bookmarks and saved review ranges are frame indices.
  If the extent followed a global preference, lowering the preference would
  silently discard bookmarks past the new end (relink-style clamping) the next
  time an old project opened. Storing the hold with the source keeps saved
  review state valid indefinitely. This also matches the usual NLE convention
  that the default still duration applies at import.

- The format stays **version 1**. The field is additive, and v1 readers ignore
  unknown fields. On load, a missing `"still"` object on a still path uses the
  current preference. A present but malformed object (hold below 10, invalid
  rate) rejects the project, consistent with how malformed bookmarks are
  treated.
- **Relink** keeps the existing source's hold, so relinking a still to a
  re-rendered still preserves bookmarks exactly.

The options reach each opener as a value:

| Caller | Source of options |
|---|---|
| `PlaybackController::openMedia(path, options)` → `DecoderWorker::openMedia` | `entry.source->imageOptions()` |
| `PlaylistProbeWorker::probe(..., options)` | same; relink passes the relinked entry's |
| `CompareVideoLane::open(id, source, …)` | `source->imageOptions()` |
| `ExportSource::still` → `ExportRenderer` | snapshotted with the rest of `ExportSpec` |

Every parameter defaults to `ImageSourceOptions{}`, so video-only callers and
existing tests are unchanged.

### Audio paths

A still has no audio stream. `AudioSourceReader::open()` refuses a still path
before calling FFmpeg, with the same "The file has no audio stream." it already
returns for silent video. That one guard covers every audio consumer. The
checks below confirm each one already treats that refusal as "no audio" and
fails quietly:

- **Waveform** → `analysisUnavailable`. No peaks are drawn and nothing is shown
  to the user.
- **Scrub and frame-step audio** are requested only when `metadata.hasAudio`.
- **Compare audio (Source B = still)** contributes silence.
- **Export audio** contributes silence, and the audio summary reads "None".

### UI

- One shared filter builder in `MainWindow` produces *Media Files* (video plus
  images), *Video Files*, *Image Files* and *All Files* for Open Media, Add to
  Playlist and Relink.
- **Drag and drop:** there was no external file drop in the application. It
  appears in the M1 roadmap but was never implemented. This branch adds the
  minimal form: dropping supported local files onto the main window adds them
  to the playlist, the same as Add to Playlist. Dropped files are never
  activated destructively and never trigger the discard prompt.
- **Media Information** gains a *Type* row, "Video" or "Still image".
- The **sources panel description** reads e.g.
  `1920x1080 • Still image • 48 frames @ 24 fps`.

## Known limitations (phase 1)

- **EXIF orientation is not applied.** Video display rotation isn't applied
  either: `MediaMetadata::rotationDegrees` is read but nothing uses it. A
  portrait phone JPEG therefore shows as stored. Fixing this belongs with
  rotation support in general.
- **No colour management.** The EXR sRGB transfer is a display convenience.
  ICC profiles in PNG, JPEG or TIFF are ignored.
- **Large images** are held as one RGB32 buffer. An 8K × 8K still costs about
  256 MB, shared across all held frames.
- **The hold rate is fixed at 24/1 in the UI.** The rate is already stored
  per source and is honoured everywhere, so exposing it later needs no format
  change.
- **Animated WebP and APNG** are treated as stills: only the first decoded
  picture is used, and animation isn't supported. GIF isn't in the list.

## Tests

Fixtures are generated by `cmake/ATKTestMedia.cmake` from the same `testsrc2`
recipe and validated by `ValidateTestMedia.cmake` (codec and size):

- `atk_still_320x180.png`: lossless, so a decoded pixel can be asserted on
- `atk_still_320x180.jpg`
- `atk_still_64x36.png`: a second size, so two images can be told apart
- `atk_still_320x180.exr`, if the rebuilt FFmpeg has the EXR encoder

The hostile-name case is built inside a temporary directory at test time:
`frame_%03d.png` is a copy of the 320×180 PNG, and beside it `frame_001.png`
is the 64×36 PNG. Opening the literal `%03d` name must produce 320×180. A
sequence interpretation would resolve to `frame_001.png` instead. Copying at
test time keeps `%` out of the build-system command lines, where `cmd.exe`
could expand it.

New cases:

- **Decoder:** exact count and PTS for each index, EOF after N, clamp past the
  end, backward step, rational hold rate (24000/1001), hostile filename, JPEG,
  no audio, hold below 10 normalized, EXR decode.
- **Probe:** a still reports `Synthesized` with the requested hold.
- **Project:** round-trip of `"still"`, missing-object default, rejection of a
  malformed object, relink preserving the hold.
- **A/B:** a still as Source B maps to the held picture with no audio.
- **Export:** a still exports N frames to MP4 and PNG with no audio track.
- **Audio:** `AudioSourceReader` refuses a still quietly.
- **Settings:** the preference default, clamping and persistence.
- **Media Information:** the Type row.

Debug and Release are both configured, built and fully tested before the
branch is reported complete.

## Phase 2: image sequences (built on `feature/sequences-and-flip`)

A sequence is **one source with one path**: a printf-style pattern plus an
explicit inclusive range and its own rate. It is never stored as the first
file alone.

```json
"path": "renders/shot010/shot010.%04d.exr",
"sequence": { "firstFrame": 1001, "lastFrame": 1096,
              "frameRate": { "numerator": 24, "denominator": 1 } }
```

### Opening

- **Detection** (`media::detectImageSequence`). The frame number is the *last*
  run of digits before the extension, so `shot010_v2.1001.exr` is numbered by
  1001. Neighbours must share the prefix, the suffix and the extension.
  - A leading zero means fixed width (`%04d`). Otherwise the width that matches
    more files wins, and unpadded runs (`1..120`) use `%d`.
  - A lone numbered file is not a sequence.
- **Prompt.** When Open Media, Add to Playlist or drag and drop meets a
  numbered image with neighbours, ATK asks: *Image Sequence*, *Single Image*
  or *Cancel*. It shows the range, the frame count, any missing frames and
  the rate it will use.
  - A batch asks once per sequence, so dropping all 96 frames gives one
    prompt and one source.
  - The local API never prompts and always adds single files, so automation
    cannot block.
- **Rate.** Preferences → Review → *Image sequence and still rate* (presets
  23.976, 24, 25, 29.97, 30, 48, 50, 59.94 and 60; default 24) seeds new
  sources.
  - Each source keeps its own rate, stored with the source.
  - Sources panel → right-click → *Frame Rate…* re-times a sequence or still.
    Frame indices, and therefore bookmarks and the review range, are
    unchanged; only media times move. A playing source or comparison Source B
    reopens.

### Decoding

`MediaDecoder` opens each sequence frame as its own single image, with the same
`image2` / `pattern_type=none` helper that stills use. It does not use image2's
own sequence demuxer. As a result:

- Seeking and stepping are exact by construction: frame N is file
  `first + N`, with `ptsTicks == N` in a `1/rate` time base.
- No demuxer state is held between frames.
- A missing number is handled per frame, not as an early end of stream. It
  holds the nearest earlier frame that exists; a leading gap holds the first
  frame. A frame that becomes unreadable after opening holds the last good
  picture.
- The present frame numbers are listed once at open (one directory listing),
  so playback does not stat the filesystem per frame.

### Availability, relink, export, audio

- `media::imageSourceExists(path, options)` replaces `QFileInfo::exists` for
  sources. A sequence is available when at least one frame in its range is on
  disk. The probe, project load and export validation all use it.
- **Relink** to any frame of another sequence relinks to that whole sequence,
  with its detected range and the original rate.
- **Exports** are named from the pattern without its token
  (`shot.%04d.exr` → `shot`).
- **Audio** readers refuse the pattern like any image, so every audio path
  stays quiet.

### Display

- The sources panel and title show `shot.[1001-1096].exr`.
- Media Information shows *Image sequence* and the missing-frame count.

### Not yet done

- Missing frames are counted (description and Media Information) but not
  marked per frame on the timeline. Availability has no *Partial* state.
- Timeline labels show 1-based indices, not the source numbers (1001…).
- The range is fixed when the sequence is opened. A render that is still
  writing frames needs a relink to pick up new ones.
- Very large EXR sequences decode one frame per file open. That is fine for
  review-sized frames; heavy 4K EXR playback may not reach real time.
