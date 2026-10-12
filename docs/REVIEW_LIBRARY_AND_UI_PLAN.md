# Review Library and Interface Refresh — Proposed Plan

Status: **Approved by the owner (2026-10-12) with the recommended decisions in §5,
plus two additions: launch routing (§2.7) and an images-only option for the
Notes PDF (§2.8).** Work proceeds phase by phase.

| Phase | State |
|---|---|
| 1. Visual system | **Implemented** (PR dshepstone/ATK_Player#31): teal accent and new tokens, `ui/IconProvider` with Lucide icons, stylesheet refresh, transport/playlist/command icons. Windows Debug/Release run pending. |
| 2. Drawing tools | **Implemented** (same PR): icon row, Tool options pop-up, floating size/opacity pill, undo/redo/clear on the row; pen-up cost now flat (cache paints only the new stroke). Trace/Ghost stay on the drawing row as icon buttons until Phase 4 builds the bottom view group. Windows run pending. |
| 3. Playlist cards | **Implemented** (same PR): `PlaylistCardDelegate` cards (number, 16:9 thumbnail with frame count, name, details, note and drawn-frame badges, accent bar on the loaded clip, missing clips marked), `media::ThumbnailWorker` on its own thread with a PNG disk cache, drop hint, 300 px default panel width. Built on the existing `QListWidget` rather than a new `PlaylistModel`, so drag-reorder, activation and the context menu keep their tested behaviour. The status chip arrives with clip statuses in Phase 4. Windows run pending. |
| 4. Header, notes, transport, PDF option | **Implemented** (same PR): review header row (project › clip, clip status, Export menu, show folder); clip status saved per entry, version 3 when present, chip on playlist cards; notes search and sort; bottom bar grouped as Draw | in, out, loop | transport | trace, ghost, flip, show drawings | fit, video full screen, preferences, volume; "Include text notes" switch for an images-only Notes PDF. Not done here: a "clip note" (not tied to a frame) needs a data-model change; status labels are fixed, not editable in Preferences; timeline restyle and its duplicate second labels (pre-existing) are left for Phase 7. Windows run pending. |
| 5. Review Library | **Implemented** (same PR): `src/review` (`ReviewLibrary` index as JSON in app data, written with `QSaveFile`; `scanFolder` with our own natural sort, image sequences grouped, hidden files skipped, subfolders optional); Review Library start page (All / Starred / Archived, search, sort, thumbnail strips, star, archive, remove, show folder, Open Project / Open Clips); New Review dialog (folder scanned on a worker thread, tick and drag to order, name checked as a file name); reviews saved as `<folder>/<name>.atkproj` and opened from the library, offering clips added to the folder since; launch routing per §2.7 (plain launch shows the library unless turned off in Preferences; any file, project or API open goes to the player); File › Review Library (Ctrl+Shift+H), File › New Review from Folder, and a house button in the header. Differences from the plan: review metadata (folder, subfolders, star, archive) lives in the library index rather than a `review` block in the project, so a copied review folder still opens as a plain project; the new-clip check when opening a review scans the folder on the GUI thread (file listing only, no decoding); single-instance hand-off (D9) is not built. Windows run pending. |
| 6–7 | Not started |
Per `AGENTS.md`, this is new milestone work. Suggested name:
**M10 — Review Library and Interface Refresh**. It has no OS-specific code
apart from "show in Explorer/Finder" and can land before or after M6–M8.

The three goals:

1. Give the player, playlist and drawing tools a cleaner, icon-led look in the
   style of SyncSketch (shown in the owner's reference screenshots). Copy the
   *layout ideas*, not their branding, logo or assets.
2. Add a **Review Library** start screen. A review is a list of clips from a
   folder on disk (Windows, macOS or Linux), in a chosen order, that opens in
   the player for notes and drawings.
3. Add a **roster import and batch rename** tool. It matches the clip names in a
   folder to a class list or staff list (CSV or Excel), so each clip carries the
   right person's name and the review plays in roster order.

---

## 1. What the reference shows (and where ATK differs today)

| Area | SyncSketch reference | ATK Player today |
|---|---|---|
| Header | Breadcrumb (course › review), clip name, **status** dropdown, download, share | Menu bar plus window title |
| Drawing tools | One row of **icon-only** buttons above the picture: active tool filled with the accent colour; colour swatch; undo/redo/clear at the end | `AnnotationToolbar` with **text** buttons, plus size, opacity, pressure, curve and stabiliser controls in the same row |
| Size / opacity | Floating **vertical slider pill** beside the picture (size number on top, opacity % below) | Horizontal slider and spin box in the toolbar |
| Playlist | Large 16:9 **thumbnail cards**: index number, title, owner, notes-count badge, active card highlighted with an accent bar, "Drop new files here" | `SourcesPanel`: plain text `QListWidget` rows with a ▶ marker |
| Notes | Right panel: "All Notes" filter, search, sort, empty state, composer pinned to the bottom with a "Frame" toggle and Submit | `NotesPanel`: functionally similar, plainer layout |
| Timeline / transport | Thin full-width track, current-frame box at the right; one icon row below, grouped (transport · edit · view · zoom/settings) | Wider timeline and a text-plus-icon transport |
| Home | Review list: expandable rows with a thumbnail strip, item count, Add Media; left navigation (All / Starred / Archived) | `WelcomeDialog` only (thank-you screen) |
| Accent | Teal green | Blue (`theme::accent()` `#4C9EE0`) |

Existing pieces we build on rather than replace:

- `theme::` palette and `styleSheet()` (`src/ui/Theme.*`). Colours are already
  defined once.
- `Project` / `SourceEntry` (`src/project`) already hold an ordered playlist,
  `displayName`, bookmarks and annotations, and save to `.atkproj` versions 1 and 2.
- The command table (`src/ui/commands`). Every button keeps its existing command
  and shortcut, so the restyle changes no behaviour.

---

## 2. Proposed design

### 2.1 Visual system (Phase 1)

- **Accent:** add a teal accent token (for example `#1FB58F`, final value to
  be tuned for contrast), used for the active tool, selected playlist card,
  primary buttons and the playhead's frame box. The viewer surround stays
  neutral dark grey and the picture background stays pure black, as `Theme.h`
  already argues.
- **Tokens:** add the missing ones (`cardBackground`, `cardSelected`,
  `badgeBackground`, `statusApproved/NeedsWork/…`, corner radius, spacing
  scale), so widgets never hard-code values.
- **Icons:** one consistent outline icon set as SVG in `assets/icons`, compiled
  into `atk_resources.qrc` and **tinted at runtime** from theme tokens (normal,
  hover, active, disabled), so one file serves every state. Candidates: Lucide
  (ISC) or Tabler (MIT). Both are permissive; the chosen licence is added to
  `THIRD_PARTY_LICENSES.md`.
- A small `IconProvider` in `src/ui` caches the tinted `QIcon`s by name, colour
  and DPR.

### 2.2 Drawing tools (Phase 2)

- `AnnotationToolbar` becomes a single **icon row** that matches the
  reference's order:
  `Select (hand) · Pencil · Pen · Brush · Marker · Custom ▾ · Eraser ▾ |
  Arrow · Rect · Ellipse · Line · Text | colour swatch · eyedropper (later) |
  Undo · Redo · Clear frame`
- The active tool is drawn as a filled accent tile, with a tooltip giving the
  name and shortcut.
- **Tool options move into a popover.** Eraser mode, pressure, pressure curve,
  stabiliser and the brush-set menu move behind a "⋯" button at the end of the
  row and on the tool's ▾. Most of the time the row stays uncluttered.
- **Floating size/opacity pill** at the viewer's left edge: two vertical
  sliders with their values (px and %), shown only in Draw mode. It is a child
  widget of the viewer area, not of `ViewerWidget` itself, so the viewer still
  only displays frames.
- **Trace ▾ / Ghost ▾** move to the bottom view-tools group, as in the
  reference's onion-skin and grid icons.
- **Drawing speed fix.** This comes from the earlier investigation of strokes
  slowing down:
  - While drawing, the in-progress stroke is currently rebuilt from its first
    point on every pointer move. Instead, render it **incrementally** into a
    stroke layer and only re-smooth the last few segments.
  - Coalesce the store's per-edit refreshes. Notes panel, timeline marks,
    project copy and the full annotation cache now run once per stroke, and
    once per eraser *move*. Make them run once per stroke or swipe.
  - This must be measured before and after on real hardware.

### 2.3 Playlist panel (Phase 3)

- Replace the `QListWidget` text rows with a `QListView` driven by a
  `PlaylistModel` and a custom card delegate:
  - Thumbnail (16:9, lazy-loaded) with index number and frame count overlaid
  - Title: `displayName`, so a roster rename shows here
  - Subtitle: roster person and/or folder
  - Badges: notes count and "has drawings"
  - Status chip
  - Active card: accent left bar and tinted background
  - Missing media: greyed out with a relink action
- Drag to reorder (writes to `Project` order) and drop files to add, with a
  "Drop new files here" hint when the list is short.
- **`ThumbnailService`** (media layer, no widgets): a worker thread that owns
  its own FFmpeg context, decodes one representative frame per clip, and
  scales it. Results return to the panel through queued signals, and are cached
  on disk under `QStandardPaths::CacheLocation`, keyed by path, size and
  mtime. Shutdown order follows `AGENTS.md`: stop, disconnect, join, destroy.

### 2.4 Header, notes and transport (Phase 4)

- **Review header bar** above the viewer:
  - Breadcrumb `Library › Review name`, which is clickable and returns to the
    Review Library
  - Clip name
  - **Status dropdown** for the clip (Not reviewed · In review · Needs changes
    · Approved; the list is configurable)
  - Export and Show-in-folder buttons
- **Notes panel:**
  - Filter header (All notes / This clip / This frame)
  - Search and sort
  - Friendly empty state
  - Composer pinned to the bottom with a "Frame" toggle (attach to the current
    frame, or to the clip in general) and a Submit button. Existing note
    storage and PDF export are unchanged.
- **Timeline / transport:**
  - Thinner track
  - Frame-number box at the right end in the accent colour
  - One icon row underneath, grouped as transport · in/out/loop · edit · view
    (trace, ghost, flip, safe areas) · zoom/settings/fullscreen
  - The fps readout stays visible, showing the rational rate (for example
    23.976), never a hard-coded 24.

### 2.5 Review Library — start screen (Phase 5)

**Where it lives.** The Review Library is a *Home* page inside the main window
(`QStackedWidget`: Home ⇄ Player), not a separate window. It shows when the
player is started **on its own** (see §2.7 for exactly when), and is always
reachable from the breadcrumb or **File › Review Library** (`Ctrl+Shift+H`).
The first-run `WelcomeDialog` stays, shown once over whichever page is up, and
remains under Help.

**Layout** (following the reference):

- Left navigation: **All Reviews · Starred · Archived**, plus Recent.
- Main area:
  - Header: "Reviews", a search box, sort (Latest / Name / Folder), grid ⇄
    list toggle, and a **+ New Review** button.
- Each review row:
  - Name, item count, folder path, and a thumbnail strip of its first clips
  - **Open ▶**, **Add media**, **Rescan folder**, **Rename clips…**
  - **⋯** menu: Star, Archive, Show in folder, Remove from library
- Expanding a row shows its clips as cards (with status chips) that can be
  reordered before opening.

**New Review wizard:**

1. **Choose folder** (`QFileDialog::getExistingDirectory`; works the same on
   Windows, macOS and Linux). Option: include subfolders.
2. **Pick clips.** The folder is scanned on a worker thread for supported media
   (the media layer's extension list, plus image sequences detected the same
   way the Open dialog does). All clips are ticked by default, and individual
   ones can be unticked.
3. **Order:** by name (natural sort, so `clip2` comes before `clip10`), by date,
   by roster (after Phase 6), or by manual drag.
4. **Name the review** (default: the folder name) and **Create**.

**Data model.** A new `src/review` library has no widget dependencies:

- `ReviewList`:
  - Id, name, source folder, include-subfolders flag
  - Ordered entries (relative path, display name, roster key, status)
  - Starred, archived, created and opened times
- `ReviewLibrary`: the index of known reviews. It is stored as JSON under
  `QStandardPaths::AppDataLocation/reviews.json`, written atomically with
  `QSaveFile`.
- **Persistence:**
  - Each review's notes and drawings live in an `.atkproj` saved **in the
    review folder** (`<folder>/<review name>.atkproj`). Whoever opens that
    folder gets the notes too, and the existing project format, annotations
    and PDF/MP4 exports are reused.
  - The review metadata (source folder, roster mapping, statuses) is an
    optional `review` block in that project file. As was done for annotations,
    a project **with** the block is written as **version 3**, so older builds
    refuse it cleanly instead of silently dropping the data. Projects without
    it keep their current version.
  - Entries are stored relative to the folder, so a moved or synced folder
    (OneDrive, a network drive) still opens.
- **Rescan** compares the folder with the list:
  - New files are offered for adding.
  - Missing files are marked "missing" (the existing relink flow applies).
- Opening a review builds the `Project` playlist in the review's order and
  switches to the Player page.

### 2.6 Roster import and batch rename (Phase 6)

**Import a roster:**

- **CSV** (UTF-8 or UTF-16, with or without a BOM; comma, semicolon or tab
  separated) is supported in all cases.
- **Excel `.xlsx`** support is decision **D4** (§5).
- A preview shows the first rows. The user picks:
  - Which column is the **match key** (for example Student ID, Email, or
    "Last name")
  - Which columns form the **name**
  - Whether row order defines the **review order**

**Match clips to roster rows** (`RosterMatcher`, pure logic):

1. An exact key found in the file name (for example a student ID).
2. Name tokens: case-, accent- and separator-insensitive, either order
   (`smith_jane`, `Jane-Smith`, `JSmith`).
3. Fuzzy matching (edit distance) as a *suggestion only*.

The results show in a table: clip → matched person, with a confidence level.
Any row can be changed through a dropdown. **Unmatched clips** and **roster
people with no clip** (missing submissions) are listed separately, and the
latter are useful on their own.

**New name template**, for example:

```
{order:02}_{last}_{first}            → 01_Smith_Jane.mp4
{id}_{last}{first}_{original}        → 1004217_SmithJane_blocking_v3.mov
```

The tokens are `{order}`, `{id}`, `{first}`, `{last}`, `{full}`, any roster
column `{col:Header}`, `{original}`, `{date}` and `{review}`. The extension is
always kept.

**Two modes**, chosen per run:

- **Display names only** (default, non-destructive): this sets
  `SourceEntry::displayName` and the review entry's name. The files on disk are
  untouched, and the playlist, notes PDF and exports show the roster names.
- **Rename files on disk**: shown only after a preview and an explicit
  confirmation. Safety rules:
  - **Dry-run preview**: old → new name for every file, with problems flagged
    before anything changes:
    - Duplicate targets
    - Case-only collisions on Windows and macOS
    - Characters that are illegal on any of the three OSes (`<>:"/\|?*`,
      trailing dots and spaces, reserved names such as `CON`)
    - Over-long paths
    - Files that already exist
  - **Two-step rename**: every file goes to a unique temporary name first,
    then to its final name. Swaps and chains (A→B, B→A) cannot clobber
    anything.
  - A **journal** (`.atk-rename-<timestamp>.json` in the folder) is written
    *before* the first rename. It enables **Undo last rename** from the review's
    ⋯ menu, and it lets an interrupted run roll back at the next launch.
  - Any failure stops the run and rolls back the files already renamed. Nothing
    is overwritten. Files currently open in the player are released first.
  - After a successful rename, the review and `.atkproj` entries are updated, so
    notes and drawings stay attached to the right clip.
- **Order by roster**: one click sorts the review into roster row order, and
  clips with no match go at the end.

### 2.7 Launch routing: when Home shows, and when it does not (Phase 5)

The Review Library is for the user who opens ATK Player **to choose clips**.
Anyone who sends the player a clip goes **straight to the Player page**, with
no Home screen in between.

| How the player was started | Opens on |
|---|---|
| Started on its own (Start menu, Dock, desktop icon, `ATKPlayer` with no file) | **Home** (Review Library) |
| A clip or `.atkproj` was **double-clicked** in Explorer, Finder or a Linux file manager (file association, so a bare path on the command line) | **Player**, with that file loaded |
| Sent by **Maya, Harmony** or another tool: `--open <file>`, a bare path, or a file drag onto the program icon | **Player**, with that file loaded |
| A file arrives over the **external API** (`src/api`) while the player is already running | **Player**: switches from Home if needed and loads the file |
| A file is **dropped** onto the window while Home is showing | **Player**, with that file loaded |
| Started on its own with **Preferences › "Reopen the last project at startup"** turned on | **Player**, with the last project (Home is one click away) |

Implementation notes:

- `main.cpp` already separates the two cases: `requestedMediaPath()` empty or
  not. A requested path calls `openMediaFile` / `openProjectFile` exactly as
  today. Only the empty-path branch asks `MainWindow` to show Home, unless
  reopen-last-project applies. The Home page is built lazily, so a launch from
  Maya or Harmony pays no start-up cost for it.
- Every way a file opens goes through one `MainWindow::showPlayer()` switch,
  so no route can leave the user staring at Home with a clip loaded behind it.
- A failed open, such as a missing or unreadable file, still lands on the
  Player page with the existing error state, so the user sees why.
- **Preference:** "At startup, show: Review Library / Empty player". The
  default is the Review Library. This affects only plain launches; files sent
  by double-click, Maya or Harmony always open directly.
- **Already running (follow-up, optional):** today a second double-click
  starts a second player. Handing the file to the running instance (through a
  local socket, in a later phase) would load it in the existing window
  instead. This is listed under §5 as D9 and is not part of the core phases.
- macOS sends files through `QFileOpenEvent` rather than the command line.
  That is handled with the macOS milestone and follows the same rule (file →
  Player).

### 2.8 Notes PDF: include the text notes, or images only (Phase 4)

**Export Notes PDF** (`ReviewNotesDialog`, *Content* card) gets an on/off
switch: **"Include text notes"**, on by default.

- **On:** the current layout. Each row has the frame picture, with drawings,
  beside its notes.
- **Off: images only.**
  - Each row is the frame picture with its drawings, captioned with the frame
    number and timecode only. No note text, authors or dates.
  - The freed column lets each picture use the full page width.
  - Frames that have notes but **no drawings** are left out, since without the
    text there is nothing on them to review. The dialog's summary line counts
    exactly what will be exported (for example "12 frames will be listed").
  - "Include resolved notes" is disabled while the switch is off, because it no
    longer applies.
  - The header (project, clip, reviewer, date) stays in both modes.
- The setting is remembered between exports, like "Open the PDF when it is
  done".
- Code: `ReviewNotesOptions` gains `bool includeNoteText = true`.
  `ReviewNotesJob::framesToInclude` takes the new flag, the PDF layout gets an
  images-only row, and the dialog gets the switch.
- Tests:
  - `framesToInclude` with the switch on and off (notes-only frames dropped)
  - Pagination of full-width rows
  - The PDF writes and verifies in both modes
  - The note text is absent from the images-only PDF

---

## 3. Phases

Each phase lands as its own PR, runs, and is worth using on its own. Each must
build and pass the full `ctest` suite in Windows Debug and Release before it is
called done.

| Phase | Scope | Main files | Tests |
|---|---|---|---|
| **1. Visual system** | Teal accent and new tokens, `IconProvider`, SVG icon set and licence, stylesheet refresh | `Theme.*`, new `IconProvider.*`, `assets/icons`, `atk_resources.qrc`, `THIRD_PARTY_LICENSES.md` | Icon lookup and tint cache; stylesheet builds |
| **2. Drawing tools** | Icon row, options popover, floating size/opacity pill, Trace/Ghost moved; **incremental stroke rendering and coalesced refreshes** | `AnnotationToolbar.*`, new `BrushSizePill.*`, `ViewerWidget.cpp`, `AnnotationRenderer.cpp`, `MainWindow.cpp` | Existing annotation suites stay green; renderer pixel-equivalence (incremental vs full); timing benchmark of a 2,000-point stroke and a 200-stroke frame |
| **3. Playlist cards** | `PlaylistModel` and card delegate, drag reorder, drop zone, `ThumbnailService` with disk cache | `SourcesPanel.*`, new `PlaylistModel.*`, `PlaylistCardDelegate.*`, `src/media/ThumbnailService.*` | Model and reorder tests; thumbnail worker start/stop and shutdown order; cache invalidation on mtime |
| **4. Header, notes, transport, PDF option** | Review header with clip status, notes panel layout, timeline and transport regrouping; **"Include text notes" switch for the Notes PDF (§2.8)** | `MainWindow.*`, `NotesPanel.*`, `TransportControls.*`, `TimelineWidget.*`, new `ReviewHeaderBar.*`, `ReviewNotesDialog.*`, `ReviewNotesPdf.*` | Status persistence; notes filter and search; existing timeline and transport tests; images-only PDF tests from §2.8 |
| **5. Review Library** | `src/review` (`ReviewList`, `ReviewLibrary`, folder scanner), Home page, New Review wizard, project v3 `review` block, rescan; **launch routing (§2.7)** | new `src/review/*`, new `ReviewLibraryPage.*`, `NewReviewWizard.*`, `ProjectSerializer.cpp`, `MainWindow.*`, `app/main.cpp`, `PreferencesDialog.*` | Library JSON round-trip and atomic save; launch routing (no file → Home; `--open`, bare path, `.atkproj`, API, drop → Player; reopen-last preference); scanner (natural sort, subfolders, sequences); v1/v2/v3 compatibility; relative-path relocation |
| **6. Roster and rename** | CSV (and XLSX per D4) import, `RosterMatcher`, name template, `RenamePlanner` / `RenameExecutor` with journal and undo, rename dialog | new `src/review/Roster*`, `RenamePlanner.*`, `RenameExecutor.*`, `BatchRenameDialog.*` | Parser edge cases (BOM, quotes, delimiters); matcher; planner conflicts per OS rule; executor on a temp dir: swaps, chains, failure and rollback, undo |
| **7. Polish and docs** | Keyboard paths, accessibility names, high-DPI check, user guide, release notes, `ROADMAP.md` | `docs/*`, `README.md` | Full suite; manual acceptance list |

The PDF switch (§2.8) is small and self-contained. It can ship early, even
before Phase 1, if it is wanted sooner.

**Suggested order:** Phases 1 → 2 → 3 make the player look and feel right first.
Phases 5 → 6 deliver the class-review workflow. Phase 4 can slot anywhere after 1.
If the review workflow is the more urgent need, Phases 5–6 can come straight
after Phase 1, using plain playlist rows until Phase 3 lands.

---

## 4. Risks and how the plan handles them

- **Renaming students' files is destructive.** The default is display-only.
  Disk renames require a preview, a confirmation, the two-step rename, a journal
  and undo, and they never overwrite.
- **Thumbnails could stall the UI.** They decode on a worker thread with its own
  FFmpeg context, return through queued signals, and are cached on disk.
- **Older builds opening new projects.** Projects with a `review` block are
  written as version 3, so older builds refuse them cleanly instead of losing
  the data.
- **Scope creep from "look like SyncSketch".** Only layout and interaction
  patterns are copied. There are no cloud sync, share links or accounts; those
  remain out of scope.
- **Milestone boundaries.** This plan needs the owner's approval as a milestone
  before Phase 1 starts.

---

## 5. Decisions (approved 2026-10-12: the recommendations)

| # | Question | Recommendation |
|---|---|---|
| D1 | Accent colour: switch to teal, or keep blue with the new layout? | Teal, defined as one token so it can be changed back in one line |
| D2 | Icon set | Lucide (ISC): broad set, consistent outline style |
| D3 | Home page: inside the main window, or a separate start window? | Inside the main window (Home ⇄ Player), like the reference |
| D4 | Excel support | CSV in Phase 6; `.xlsx` through a small built-in reader that uses the zlib already shipped (no new dependency). The alternative is the QXlsx library (MIT), which adds a dependency |
| D5 | Default rename mode | Display names only; rename on disk is opt-in per run |
| D6 | Clip status values | Not reviewed · In review · Needs changes · Approved, editable in Preferences |
| D7 | Where a review's notes are saved | An `.atkproj` inside the review folder (travels with the clips). The alternative is a central app-data store |
| D8 | Images-only PDF: keep frames that have notes but no drawings? | Leave them out (§2.8) |
| D9 | Double-click while the player is already running | Optional follow-up: hand the file to the running window instead of starting a second player |
