# Annotation and Review-Notes PDF — Coding Plan

Status:
- **Phases A and B: done** (data model, undo store, project persistence).
- **Phase C: implemented, pending hardware acceptance.** Shipped so far:
  - **Brush engine:** `AnnotationRenderer`, `StrokeBuilder`, `AnnotationToolSettings`.
  - **Viewer:** drawing, display and tablet input in `ViewerWidget`.
  - **Toolbar:** `AnnotationToolbar`.
  - **Commands and wiring:** Draw on Frame (`D`), undo/redo, `[` / `]` brush size, clear frame,
    show drawings, next/previous drawn frame, plus `MainWindow` integration.
  - **Timeline:** markers on drawn frames.
  - **Tests:** `tst_annotationrenderer`, `tst_annotationviewer`, `tst_annotationworkflow`.
- **Phases D–G:** not started.

The project owner approved this as a new annotation/notes milestone (suggested name:
**M9 — Annotation and Review Notes**). It can land before or after M7/M8 because it has no
OS-specific code. `ROADMAP.md` still lists "Drawn annotations over frames" under
*Not scheduled*; update it when the milestone lands.

### Resume here (handoff, 2026-10-10)

- **Where the code is:** branch `claude/loving-cerf-57c6zt`, PR
  https://github.com/dshepstone/ATK_Player/pull/28 (phases A–C, area eraser, in-place
  text and Select tool). Not merged. See `git log` for the head.
- **Verified:**
  - Windows Debug and Release 34/34 at `42a51bb` (A–B), run by the owner.
  - Linux full build and `ctest` at the current head: 34/37. The same 3 tests fail without
    these changes in that environment: `tst_export` (no H.264 encoder),
    `tst_stillimage` (EXR decode) and `tst_audioreview` (Qt 6.4 testlib assert).
- **Waiting on the owner:**
  1. Windows Debug and Release build plus `ctest` at the current head.
  1a. The area eraser is accepted. Try the new Text and Select tools: click to type,
      drag a box, Ctrl+Enter / click away, Esc, edit by clicking, move, Delete,
      double-click to edit with the pen.
  1b. Possible follow-ups the owner may want: a text size separate from the brush
      size; a narrower toolbar (it overflows at 1200 px wide).
  2. Tablet acceptance on Wacom/Cintiq: pressure on Pencil/Pen/Brush, latency, the
     hover brush cursor, the stylus eraser end, barrel-button pan, and which pressure
     curve (Soft/Linear/Firm) should be the default.
  3. The basics: `D`, `Ctrl+Z`, `[` `]`, `Alt+Shift+←/→`, and save and reopen.
- **Open decisions:**
  - Stylus barrel button: eraser or colour picker? Barrel buttons currently fall
    through to mouse handling (pan).
  - Pre-frame cue in the annotated MP4: default on, or option only?
- **Next:** tune brushes from the tablet feedback, then Phase D (notes panel: bookmark
  status/author, "add note at frame", thumbnails), then E (MP4, play-through default),
  then F (PDF).
- **Practical notes:**
  - The owner's clone once had a corrupt loose ref `.git/refs/tags/v0.3.0` that broke
    `git fetch`. Removing that file fixed it.
  - A stale `build/<preset>/bin` with mixed FFmpeg DLLs caused a `tst_mediadecoder`
    segfault. Deleting `bin` fixed it.
  - Linux scratch builds need Qt 6.4 allowed, `qt_generate_deploy_app_script` removed,
    a pkg-config `FindFFMPEG.cmake`, and `-fpermissive` (because `int64_t` and
    `qint64` differ on Linux). Do not commit any of that.

### Phase C notes (as built)

- Freehand strokes are rendered per item into a layer, one segment at a time with
  Source composition. A stroke never darkens where it crosses itself (marker), yet
  width and opacity still follow pressure. Pencil grain comes from a seed stored with
  the item, so the viewer and every export draw identical grain.
- Pressure goes through the curve once, at capture. Changing the curve later never
  alters existing drawings.
- **Tablet input:** `QTabletEvent`s are accepted in Draw mode, so Qt does not also
  synthesise mouse events. The stylus eraser end erases. Barrel buttons are ignored,
  which lets them fall through to mouse handling (middle-drag pan). Hover moves drive
  the brush-size cursor.
- **Eraser:** two modes, with its own size (`eraserSizePixels`). Pen pressure scales it.
  - **Area (default):** `eraseDisc` removes only the ink under the eraser. A freehand
    stroke is resampled along its rendered path, and a sample goes when its *ink*
    reaches the disc, so no visible ink is left inside the eraser. The stroke is split
    into the remaining pieces, which keep pressure and pencil grain. Shapes and text
    are removed whole.
  - **Erase stroke:** removes anything it touches.
  - Discs are placed along the path between pointer samples, so a fast drag cannot
    jump a line.
  - `AnnotationStore::beginEditGroup` / `endEditGroup` make one swipe a single undo step.
- Owner acceptance on a Wacom Cintiq (2026-10-10): pressure works and the stylus eraser
  end works. Their request for a precise (area) eraser led to the eraser above. The
  owner has since confirmed the area eraser works well.
- **Text (Photoshop/Illustrator style):** the Text tool types in place, in a child
  `QTextEdit` laid over the picture at the click point. There is no dialog.
  - Click gives point text. Dragging a box gives paragraph text: a Text item with a
    second point, wrapping at the box width.
  - Enter adds a new line. Ctrl+Enter, clicking away or changing frame commits. Esc
    cancels. Committing empty text deletes the label being edited.
  - Clicking existing text with the Text tool edits it.
  - `AnnotationRenderer::layoutText` is the one layout (`QTextLayout`) used to paint,
    hit-test and size the editor.
  - The editor shows text at its final drawn size and colour. The app theme's
    `font-size` on every widget would override `setFont()`, so the editor carries its
    own stylesheet. Changing the size, colour or opacity while typing updates the text
    live.
  - A box keeps the size it was dragged out at (its second point stores both width
    and height). While typing, its frame has eight handles to resize it, and the border
    band between the frame and the text moves it. These are owner requests from
    Cintiq testing.
- **Select tool:** picks the topmost item, drags to move it (`translatedItem` clamps to
  the picture), Delete/Backspace removes it, and a double-click (mouse, or two quick pen
  taps) on text edits it. `AnnotationStore::replaceItem` keeps the id and order; each
  move or edit is one undo step.
- Entering Draw on Frame pauses playback, and a stroke always lands on the frame it
  started on. Draw mode is unavailable during A/B comparison and when no clip is open.
- New drawings store the displayed frame's decoded `ptsUs` as `mediaTimeUs`. The frame
  index stays authoritative.

## 1. Goal

1. Draw on top of video / animation / image-sequence frames during review
   (freehand pen, shapes, arrow, text), per frame, with undo/redo.
2. Attach frame notes (text) to frames, alongside the drawings.
3. Export an **annotated MP4** with each drawing and note shown on its proper frame.
4. Export a **Review Notes PDF**: one row per annotated frame — the frame with
   its drawings burned in on the left; frame number, timecode, status and note
   text on the right; header with project/clip names and a footer with page
   numbers (layout modelled on the supplied SyncSketch sample PDF).

## 2. What already exists and gets reused

| Existing piece | Reuse |
|---|---|
| `timeline::Bookmark` (frame, name, note, colour, range) | Frame notes. Do **not** invent a second note type; a bookmark *is* the note. Annotations link to a frame, bookmarks supply the text. |
| `project::SourceEntry` (per-source `bookmarks`, UUID, `frameOffset`) | Annotations also live per source, keyed by frame. |
| `ProjectSerializer` (versioned JSON, atomic save) | Annotations persisted in `.atkproj`. |
| `export::ExportRenderer::render()` + `ExportBurnInRenderer::apply()` | Offline, off-UI-thread decoding of an exact frame as `QImage`; extend with a vector-overlay pass. Single-frame PNG export gets "burn in annotations" for free. |
| `ViewerWidget` + `ViewerTransform` | Widget↔image coordinate mapping, zoom/pan, flip — the drawing overlay needs exactly this. |
| `CommandRegistry` / `ApplicationSettings` | Tool shortcuts, preferences. |
| Qt 6 `QPdfWriter` (in `Qt6::Gui`, already linked by `atk_core`) | PDF output. **No new dependency, no licensing question.** |

## 3. Architecture (respects the layer rules)

```
atk_core (no Widgets)                         atk_ui
─────────────────────                         ──────
annotation/                                   AnnotationOverlay   (paints + input; owned by ViewerWidget
  Stroke / Shape / TextItem  (value types)                        or a child widget — never decodes)
  FrameAnnotation   (items for one frame)     AnnotationToolbar   (tool, colour, width, undo/redo, clear)
  AnnotationStore   (per source, by frame,    NotesPanel          (extend BookmarkPanel: note + thumbnail)
        undo stack, signals)                  ReviewNotesExportDialog
  AnnotationRenderer (QPainter -> QImage;
        shared by viewer & export)
export/
  ReviewNotesSpec   (immutable job input)
  ReviewNotesPdfWriter (QPdfWriter layout)
  ReviewNotesJob    (off-thread, progress, cancel)
```

Rules that follow from AGENTS.md:

- **Decoding stays out of the UI.** The overlay only paints vector items over the
  frame the viewer already shows. The PDF job decodes through its own
  `ExportRenderer` instance on a worker thread (own FFmpeg context, single-thread
  ownership, same pattern as the existing export job). Shutdown: cancel → disconnect
  → join → destroy.
- **`AnnotationStore` is a plain `QObject` in `atk_core`** (like `TimelineModel`);
  the overlay observes it. API commands and Maya/Harmony can later call the same
  store. `AnnotationRenderer` uses only `QPainter`/`QImage` (Qt6::Gui), so tests
  can run it headless.
- **Frame identity is the zero-based source frame index, not `time + 1/fps`.**
  Store `frame` *and* `mediaTimeUs` (as `Bookmark` already does) so a re-timed
  source or variable-rate media can be re-resolved by timestamp. Timecode in the
  PDF is derived through the existing `timeline/Timecode` using the exact
  rational rate (24000/1001 etc.) — never an assumed 24 fps. For stepping while
  drawing, use the controller's presented-frame signal, not arithmetic.
- **Vector, normalised coordinates.** Points stored 0‥1 in *source image* space
  (after pixel-aspect handling, before flip/zoom). Annotations therefore survive
  zoom, pan, window resize, Flip Horizontal and are resolution-independent for
  export. Flip is presentation-only, so annotations are drawn in the same flipped
  space as the picture in the viewer and are **not** flipped in exports unless the
  user's export option says so (default: unflipped, matching the source).
- No GUI-thread blocking: stroke capture is O(points); PDF render is a job.

## 4. Data model

```cpp
enum class AnnotationTool { Pencil, Pen, Brush, Marker, Eraser, Line, Arrow, Rectangle, Ellipse, Text };

struct StrokePoint { float x, y;      // normalised image space, 0..1
                     float pressure;  // 0..1, 1.0 for mouse
                     float tiltX, tiltY; /* optional, degrees */ };

struct AnnotationItem {           // one undoable unit
    quint64 id;
    AnnotationTool kind;
    QVector<StrokePoint> points;  // shapes: 2 points
    QColor color;                 // actual RGBA — annotations are not themed
    qreal size;                   // brush diameter as fraction of image height
    qreal opacity;
    quint32 textureSeed;          // deterministic pencil grain (same pixels in export)
    QString text;                 // Text only
};
struct FrameAnnotation { int64_t frame; int64_t mediaTimeUs; QVector<AnnotationItem> items; };
```

- Eraser: v1 = stroke-level erase (remove whole item under cursor; trivially
  undoable). Pixel erase is out of scope.
- Smoothing: Catmull-Rom / quadratic midpoint smoothing at paint time; store raw
  points decimated by a distance threshold so projects stay small.
- Persistence: new per-source `review.annotations` array in `.atkproj`, written
  only when non-empty. **Implemented as an additive v1 field (no version bump)**,
  the same way still/sequence options were added: older builds ignore it, newer
  builds read old files unchanged. Malformed data rejects the project rather than
  silently dropping drawings. No pixels are stored, only vectors.
- Scoped to **stills and sequences too** (they are sources with frame indices).
  A/B compare: annotations belong to the *active pane's source*; compositor modes
  (wipe/difference) get overlay disabled in v1.

## 5. Phased implementation

Each phase leaves the app building, tested and shippable.

### Phase A — Core model (no UI)
- `src/annotation/`: item types, `FrameAnnotation`, `AnnotationStore`
  (add / remove / clear-frame / clear-all, `QUndoStack`-style command history kept
  in core without QtWidgets — a small hand-rolled stack, since `QUndoStack` lives
  in Qt6::Gui in 6.x; verify and use it if available), change signals.
- `AnnotationRenderer::paint(QPainter&, QRect target, const FrameAnnotation&)`.
- Hang `QVector<FrameAnnotation>` off `SourceEntry`; mark project modified on edit.
- Tests: `tst_annotationstore.cpp` (undo/redo, per-frame isolation, source
  isolation), `tst_annotationrenderer.cpp` (pixel assertions on a small
  `QImage`: stroke lands at the right normalised place at two resolutions).

### Phase B — Persistence
- Serializer v2 write/read, v1 compatibility, malformed-input rejection
  (bad coords, NaN, huge counts → clamp/refuse, same defensive style as bookmarks).
- Relink / reorder / source removal keep annotations attached by source UUID.
- Tests extend `tst_project.cpp`.

### Phase C — Drawing engine, tablet input and tools
The quality bar is "feels like a real drawing app", so this is split into three
parts. Pressure and brush feel are the headline requirements.

**C1 — Brush engine (`AnnotationRenderer`, atk_core, headless-testable)**

| Tool | Look | Pressure drives |
|---|---|---|
| Pencil | Thin, slightly grainy, builds up with repeated passes | width + opacity |
| Pen (ink) | Smooth, crisp, variable-width line | width (tapered start/end) |
| Brush | Soft round, painterly | width + opacity (soft falloff) |
| Marker | Constant width, flat translucent, no build-up within a stroke | opacity only (optional) |
| Eraser | Removes whole strokes (v1); pixel erase later | – |

- Rendering: Pen = filled ribbon polygon from a smoothed centreline with per-point
  radius (no visible joins, round caps). Pencil / Brush = dabs stamped at
  ≤ 25 % of brush diameter spacing along the spline, so fast strokes stay
  continuous; pencil grain from a tileable noise texture keyed by `textureSeed`,
  so viewer and export render identical pixels.
- Smoothing: Catmull-Rom through decimated raw points plus an optional
  **stabiliser** (lag-follow, 0–100 %) for clean lines on mouse.
- Pressure → size/opacity through a **response curve** (preference: soft / linear /
  firm, plus a small custom curve), with minimum size so a light touch still marks.
- Size is stored relative to image height so a 6 px stroke at 1080p looks the same
  in a 4K export. Adjustable range roughly 1–200 px (viewer pixels at 100 %).
- Mouse fallback: constant pressure, or optional velocity-based taper for Pen.

**C2 — Tablet input (`AnnotationOverlay`, atk_ui)**
- Handle `QTabletEvent` (Qt 6 `tabletEvent`): `pressure()`, `xTilt()/yTilt()`,
  `pointerType()` (Pen / **Eraser end of the stylus** switches to Eraser
  automatically), `button` for barrel buttons (e.g. hold = eraser or colour
  picker), proximity enter/leave for the brush-size cursor. **Accept** tablet
  events so Qt does not also synthesise mouse events (prevents double points).
- Works for Wacom Intuos/Cintiq and other Windows Ink devices via Qt 6's
  Windows Ink backend. Note for docs/Troubleshooting: Wacom driver "Use Windows
  Ink" should stay enabled; Qt 6 does not use WinTab by default. High-DPI and
  Cintiq screen mapping come from Qt's device-pixel coordinates (use the same
  widget→image mapping as the mouse path).
- Sample at full tablet rate: no repaint per event (coalesce paint to ~60 Hz,
  keep every sample in the stroke); draw only the **dirty rect** of the newest
  segment while a stroke is in progress for low latency. Stroke is committed to
  the store (one undo step) on release.
- Ignore touch/palm while the pen is in proximity; touch gestures stay free for
  pan/zoom.
- Cursor: circular brush outline showing real size at current zoom; hidden
  while drawing.

**C3 — Tools UI and review workflow**
- **Annotate mode toggle** (e.g. `D`): left-drag/pen draws; middle-drag pan and
  wheel zoom still work; transport keys (Space, ←/→) keep working so the
  reviewer can step frame to frame while drawing.
- `AnnotationToolbar` (docked or floating): tool buttons, colour swatches +
  picker + eyedropper, **size slider and `[` / `]` hotkeys**, opacity slider,
  pressure toggle, stabiliser slider, undo/redo (Ctrl+Z / Ctrl+Shift+Z),
  clear frame. Per-tool remembered size/colour.
- Annotated frames get a distinct marker on the timeline; next/previous annotated
  frame commands. Optional low-opacity onion skin of the previous annotated frame.
- Commands in `CommandRegistry`; shortcuts configurable in Preferences; new
  Preferences page "Drawing" (pressure curve, default tool sizes, stabiliser,
  eraser behaviour).
- Tests: `tst_annotationinteraction.cpp` with **synthesised `QTabletEvent`s**
  (pressure ramp → monotonically increasing stroke width; stylus eraser switches
  tool; no duplicate mouse-synthesised points); coordinate round-trip under
  zoom/pan/flip; renderer pixel tests for each brush.
- **Human acceptance on real hardware is required** (Wacom Intuos and Cintiq on
  Windows 11): pressure feel, latency, tilt, eraser end, barrel buttons, Cintiq
  screen mapping at high DPI. CI cannot prove this and I will not claim it
  from unit tests alone.

### Phase D — Frame notes UI
- Extend `BookmarkPanel` / new `NotesPanel`: "Add note at current frame" creates a
  point bookmark if none exists on that frame; list shows frame, timecode, text,
  tiny annotation thumbnail, open/resolved status.
- Add `status` (Open / Resolved) and `author` fields to `Bookmark`
  (serializer v2, defaults to Open / empty). Author defaults to a Preferences
  "Reviewer name" setting.
- Clicking a note seeks exactly (existing bookmark navigation).

### Phase E — Annotated MP4 ("Video with Notes") and burn-in export
Builds on the existing H.264/AAC MP4 export (`ExportJob` + `FFmpegExporter`,
exact CFR timing, active-range, off-UI-thread, cancellable). No new codec or
FFmpeg feature is needed, so the LGPL-only build is unaffected.

- Extend `ExportSpec` with `ExportAnnotations`: `include` flag, an immutable
  snapshot of the per-frame annotations and notes (copied at job start so later
  edits cannot race the worker), timing mode, hold length, caption options.
- Per output frame, `ExportBurnInRenderer::apply()` calls `AnnotationRenderer`
  using the **source frame index** that frame was decoded from, then draws the
  note caption through the existing bookmark/note burn-in code. Same code path
  as the viewer, so what you see is what you export.
- **Timing modes** (the "proper frames" requirement). **Default = play-through,
  matching how SyncSketch exports behave for this user's workflow:** the video
  runs at its normal rate and length, the drawing and note appear on exactly the
  frame they were made on, and the reviewer steps frame by frame to that frame
  for detail. Nothing pauses and the duration never changes.
  1. *Play-through, authored frame only* (**default**): frame-accurate, same
     frame count and duration as the source range, audio untouched (no gap
     handling needed). A/V sync is trivially preserved.
  2. *Hold N frames/seconds after the authored frame* (optional): drawing
     persists over the following frames, stopping early at the next annotated
     frame or range end. Duration unchanged.
  3. *Freeze on annotated frame for N seconds* (optional, off by default): pauses
     on each annotated frame, then resumes; duration grows by N per annotated
     frame. Kept for users who want a presentation-style review video.
  Because mode 1 is one video frame long, a **frame-marker** option is on by
  default: a small marker (and the note text, if "show note caption" is on) is
  shown for the authored frame only, plus an optional subtle cue a few frames
  before the annotated frame so it is findable while stepping, and an optional
  sidecar notes list (CSV/JSON, or the Review Notes PDF) giving the frame numbers
  to step to.
- **Freeze-mode correctness requirements** (only for optional mode 3; the risky part):
  - Insert repeated frames with **new, strictly monotonic CFR timestamps**
    (`pts` advances per output frame; no duplicated pts), so the exact
    frame-count/duration verification the exporter already has still holds.
  - Audio: pad with silence for the freeze duration (or optionally keep audio
    looping off) so A/V stay in sync after every freeze. The audio writer needs a
    "gap insertion" step keyed to output time; unit-test sync drift after 20
    freezes at 23.976 and 29.97.
  - Output frame ↔ source frame mapping lives in one pure, testable function
    (`outputFrameToSourceFrame`) so the progress bar, frame count, and encoder agree.
- Options in the export dialog: timing mode, hold length, show note caption
  (bottom bar, wrapped, auto-size), include timecode/frame number (existing
  burn-ins), caption position, **write a sidecar CSV/JSON of notes** (optional).
- Scope in v1: single-source export. A/B layouts annotate the Source A frames
  only; wipe/difference composites show no annotations.
- Single-frame PNG and PNG sequence get "include annotations" through the same flag.
- Tests (`tst_export.cpp`, FFmpeg lavfi fixtures like the existing export tests):
  - annotated pixels present on frame N and absent on N-1 / N+1 in mode 1;
  - hold mode ends at the next annotation;
  - play-through mode: frame count/duration identical to the source range, audio byte-identical;
  - freeze mode: decoded frame count = source frames + N·freezes, strictly
    increasing pts, audio duration == video duration (±1 audio frame);
  - rational rates 24000/1001 and 30000/1001, active-range with offset start;
  - cancel mid-export leaves no partial file (atomic temp replace, as today).

### Phase F — Review Notes PDF (the headline deliverable)
- `ReviewNotesSpec`: source, list of `(frame, mediaTimeUs, notes[], status)`,
  rational frame rate, project/clip titles, reviewer, date, page size (A4 default,
  Letter option), filters (all / open only / annotated frames only).
- Selection rule: every frame that has an annotation **or** a bookmark note is a
  row, ordered by frame; multiple notes on one frame are stacked (as in the
  sample's frame 133).
- `ReviewNotesJob` on a worker thread: for each row `ExportRenderer::render(frame)`
  → `AnnotationRenderer` composite → downscale to the thumbnail width →
  `QPdfWriter` + `QPainter` layout:
  - Header: "REVIEW NOTES", generated timestamp, project name, clip title bar.
  - Row: image left (~45% width, aspect preserved), right column with `FRAME n`,
    `TIMECODE hh:mm:ss:ff`, status, note text (wrapped, multi-note), page-break
    when the next row will not fit; never split a row across pages.
  - Footer: product name, `Page N`, URL/blank.
  - Embed images as JPEG (quality setting) to keep the file small; PNG for
    "lossless" option. Set PDF metadata (title, creator "ATK Player <version>").
- Timecode from `timeline/Timecode` with the exact rate; drop-frame handling for
  29.97/59.94 follows whatever `Timecode` already does (document it).
- Progress + cancel in the UI, temporary file + atomic replace (same as the MP4
  export job). Cancel → stop worker → join → delete partial.
- Entry point: **File → Export → Review Notes (PDF)…** plus a `ReviewNotesExportDialog`
  (range: whole clip / active range; which frames; page size; image quality;
  include resolved?). Also expose via API (`export.reviewNotes`) so Maya/Harmony
  pipelines can trigger it — only after the UI path is done.
- Tests: `tst_reviewnotes.cpp` — generate PDF from a fixture, assert it opens
  (`%PDF` header, page count via `QPdfDocument` if Qt6::Pdf is acceptable to add,
  otherwise count `/Type /Page` objects), row→page pagination logic unit-tested
  separately from painting (pure function: heights in → pages out).

### Phase G — Polish and hardening
- Large-annotation performance (cap items/points per frame, decimation).
- Accessibility: tool tooltips with current shortcuts; keyboard-only operation.
- Docs: README, `ARCHITECTURE.md` (annotation section), `API.md`, release notes;
  roadmap entry marked complete only after human acceptance.
- Optional follow-ups: export/import annotations as sidecar JSON, annotation
  sequence PNGs with alpha, "Video with sketches" MP4 (a distinct burn-in mode
  with hold durations), CSV notes export, Maya Grease Pencil export.

## 6. Decisions I need from you

1. ~~Milestone placement~~ — **decided: new annotation/notes feature (M9).**
2. **Notes vs. annotations**: OK that a frame note *is* a `Bookmark` (extended with
   status/author) rather than a separate type? (Recommended.)
3. **Qt PDF dependency**: `QPdfWriter` needs nothing new. Reading a PDF back in
   tests would need `Qt6::Pdf`; I propose test-only structural checks instead.
4. **Export flip behaviour**: unflipped by default OK?
5. ~~Video export default~~ — **decided: play-through at the source rate, annotation on its authored frame only (freeze/hold optional).**
6. Pressure-curve default and whether the stylus barrel button should map to eraser or colour picker (proposal: eraser; configurable).

## 7. Risks

| Risk | Mitigation |
|---|---|
| Overlay/viewer coordinate drift under zoom, pan, flip, non-square pixels | Single mapping function through `ViewerTransform`; round-trip tests at several zoom/flip combos. |
| Frame index mismatch between viewer, A/B offset and export | Always key by source-local zero-based frame (+ `mediaTimeUs`); export uses the same index the status bar shows; tests with 23.976 fixtures. |
| Large PDFs / many frames | Stream rows to `QPdfWriter` one at a time; JPEG embed; downscale to print resolution (≈150–200 dpi at thumbnail size). |
| Tablet behaviour differs by driver / Windows Ink setting | Document driver setting; log device name, pointer type and pressure range at stroke start (`atk.annotation` category); hardware acceptance checklist. |
| Project schema break | v2 reader accepts v1; v1 readers get the existing "newer version" message. |
| Verification gap | Per AGENTS.md: configure, build and run the full suite on Windows Debug and Release before claiming each phase done. |

## 8. Suggested PR sequence

1. Phase A+B (model, persistence) — no user-visible change.
2. Phase C (overlay + tools) — usable drawing.
3. Phase D (notes UI).
4. Phase E (annotated MP4 + burn-in export).
5. Phase F (PDF) — headline feature.
6. Phase G (polish, docs).

Estimated size: A+B ≈ 1.5k LOC with tests, C ≈ 2k, D ≈ 0.8k, E ≈ 1.2k, F ≈ 1.5k, G ≈ 0.5k.
