# Annotation and Review-Notes PDF — Coding Plan

Status: **proposal, not started.** "Drawn annotations over frames" is currently
under *Not scheduled* in `ROADMAP.md`; per AGENTS.md this plan does not authorise
work until it is accepted as a milestone (suggested name: **M9 — Annotation and
Review Notes**, which can land before or after M7/M8 because it has no OS-specific
code).

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
enum class AnnotationTool { Pen, Marker, Eraser, Line, Arrow, Rectangle, Ellipse, Text };

struct AnnotationItem {           // one undoable unit
    quint64 id;
    AnnotationTool kind;
    QVector<QPointF> points;      // normalised; 2 points for shapes
    QColor color;                 // actual RGBA — annotations are not themed
    qreal width;                  // fraction of image height (resolution independent)
    qreal opacity;
    QString text;                 // Text only
};
struct FrameAnnotation { int64_t frame; int64_t mediaTimeUs; QVector<AnnotationItem> items; };
```

- Eraser: v1 = stroke-level erase (remove whole item under cursor; trivially
  undoable). Pixel erase is out of scope.
- Smoothing: Catmull-Rom / quadratic midpoint smoothing at paint time; store raw
  points decimated by a distance threshold so projects stay small.
- Persistence: new per-source `"annotations"` array in `.atkproj`. **Bump
  `kVersion` 1 → 2**; the reader accepts v1 (no annotations) and refuses newer, as
  today. Round-trip + v1-compat tests required. No pixels are stored, only vectors.
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

### Phase C — Viewer overlay and tools
- `AnnotationOverlay` (transparent child of the viewer's display area): maps
  widget↔normalised image coords through `ViewerTransform`; handles pen/shape
  input with tablet pressure (`QTabletEvent`) optional-but-planned for width.
- **Annotate mode toggle** (e.g. `D`): while on, left-drag draws; middle-drag pan
  and wheel zoom still work; transport keys (Space, ←/→) keep working so the
  reviewer can step frame to frame while drawing.
- `AnnotationToolbar`: tool buttons, colour (reuse a fixed review palette +
  custom), width slider, opacity, Undo/Redo (Ctrl+Z / Ctrl+Shift+Z), Clear Frame.
- Drawn frames get a distinct marker on the timeline (small tick under the
  bookmark strip); next/previous annotated frame commands.
- Optional per-frame **ghosting** (show previous annotated frame at low opacity)
  — defer unless requested.
- Commands registered in `CommandRegistry`; shortcuts configurable in Preferences.
- Tests: `tst_annotationinteraction.cpp` (synthesised mouse events → store
  contents, coordinate round-trip under zoom/pan/flip).

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
- **Timing modes** (the "proper frames" requirement):
  1. *On authored frame only* (default): the drawing and its note appear on
     exactly the frame they were made on. Frame-accurate; very short at 24 fps,
     so it is best paired with mode 2 or 3.
  2. *Hold N frames/seconds after the authored frame*: the drawing persists over
     the following frames (stops early at the next annotated frame or range end).
     Video keeps playing; duration unchanged.
  3. *Freeze on annotated frame for N seconds* (like SyncSketch's "Video with
     Sketches"): playback pauses on the annotated frame while the drawing and note
     are shown, then continues. Duration grows by N per annotated frame.
- **Freeze-mode correctness requirements** (the risky part):
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

1. **Milestone placement**: approve adding M9 (or slot it before M7)?
2. **Notes vs. annotations**: OK that a frame note *is* a `Bookmark` (extended with
   status/author) rather than a separate type? (Recommended.)
3. **Qt PDF dependency**: `QPdfWriter` needs nothing new. Reading a PDF back in
   tests would need `Qt6::Pdf`; I propose test-only structural checks instead.
4. **Export flip behaviour**: unflipped by default OK?
5. **Per-frame hold in video export** (how many frames an annotation displays) —
   v1 shows it only on its authored frame; want a hold option in Phase E?

## 7. Risks

| Risk | Mitigation |
|---|---|
| Overlay/viewer coordinate drift under zoom, pan, flip, non-square pixels | Single mapping function through `ViewerTransform`; round-trip tests at several zoom/flip combos. |
| Frame index mismatch between viewer, A/B offset and export | Always key by source-local zero-based frame (+ `mediaTimeUs`); export uses the same index the status bar shows; tests with 23.976 fixtures. |
| Large PDFs / many frames | Stream rows to `QPdfWriter` one at a time; JPEG embed; downscale to print resolution (≈150–200 dpi at thumbnail size). |
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
