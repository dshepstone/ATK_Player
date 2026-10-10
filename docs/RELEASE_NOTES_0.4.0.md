# ATK Player 0.4.0

ATK Player now does review drawing and notes. You can draw over any frame with a
pen tablet, write notes beside it, and hand the review on as an annotated video
or a SyncSketch-style notes PDF.

## What's new

- **Draw on frames.** Press **D** or click **Draw on Frame** on the left of the
  playback row to show the drawing tools.
  - **Tools:** Pencil, Pen, Brush, Marker, Line, Arrow, Rectangle, Ellipse and
    Text.
  - **Tablet support:** pressure and tilt from Wacom and Cintiq pens, with a
    choice of pressure curve and a stabiliser. The pen's eraser end erases.
  - **Size and opacity:** set them on the toolbar; **[** and **]** change the
    size.
  - **Area eraser:** removes only the ink under it. "Erase stroke" removes whole
    strokes instead.
  - **Undo and redo:** **Ctrl+Z** and **Ctrl+Shift+Z**.
- **Text.** Click to type at a point, or drag a box for text that wraps. Text
  shows at its final size while you type, and the box can be resized or moved.
  The **Select** tool moves drawings and text; Delete removes them.
- **Photoshop brushes.** Import `.abr` brush sets from **Brushes ▾** and paint
  with the **Custom** tool. A project carries the brushes it uses.
- **Notes panel** (right side, **F6**), styled like SyncSketch's sidebar.
  - The note box writes to the frame on screen. **Enter** submits and
    **Shift+Enter** adds a line.
  - Moving to another frame gives a clean box. An unsent note comes back
    when you return to its frame.
  - Filter by All Notes, This Frame, Open or Resolved. Click a note to jump to
    its frame; right-click to resolve, edit or delete it.
- **Tracing paper** (**T**). Fades the picture under the drawings (25, 50,
  75 or 100%).
- **Ghost drawings** (**G**). Onion-skins drawings from nearby frames, red for
  earlier and green for later.
  - Choose how many frames to ghost before and after.
  - Ghosts can count timeline frames or only drawn frames.
- **Jump between notes and drawings** with **Ctrl+<** and **Ctrl+>**.
- **Export Notes PDF.** One row per drawn or noted frame: the picture with its
  drawings, the frame number and timecode, and the notes. Choose A4 or Letter,
  whether to include resolved notes, and tracing paper.
- **Export Notes Video.** An MP4 with the drawings and notes burned in.
  - It plays through at the clip's own frame rate.
  - Optionally, keep each note on screen for a while, or freeze the picture
    on it; the sound pauses during a freeze and stays in sync.
- **Export windows** have a new design, with a progress window showing
  percentage, current step, time left and Cancel. Finished PDFs and videos
  open automatically.
- **Save prompts** appear only when there is something of yours to keep: a
  playlist, bookmarks, drawings or notes. Just watching a video, or a saved
  project, never asks.

## Changes

- **Preferences** moved from Ctrl+, to **Ctrl+K**, because Ctrl+, (the "<"
  key) now jumps to the previous note.
- Seeking in long-GOP videos (few keyframes, common in camera and web files)
  is faster, so stepping and exports are quicker.

## For developers and pipeline integrations

- `.atkproj` files that contain drawings or notes are written as format
  version 2. Older versions refuse to open them ("created by a newer
  version"), so they can't drop the drawings when saving. Projects without
  drawings stay version 1 and still open in 0.3.0. Version 1 files with
  drawings from pre-release builds still load.
- **Local API:** export burn-ins gain `annotations`, `annotationNotes`,
  `annotationHoldFrames`, `annotationFreezeFrames` and `tracingPaper`. All are
  off by default.
- **New command:** `file.exportNotesVideo`. `file.exportReviewNotes` is now
  titled "Export Notes PDF…".
- No new third-party components. PDFs are written with Qt's own PDF writer.
- Design notes: [ANNOTATION_PLAN.md](ANNOTATION_PLAN.md).

## Upgrading

Install the 0.4.0 MSI over 0.3.0. It upgrades in place and keeps your
preferences, projects and media.

## Known limitations

- Drawings and notes belong to one source; they are not shown during A/B
  comparison.
- Imported Photoshop brushes use the brush tip shape only, not Photoshop's
  dynamics (scatter, texture, dual brush).
- When exporting into a Google Drive, OneDrive or Dropbox folder, the finished
  file opens once the sync app releases it. That can take a few seconds.

## Requirements

- Windows 11 x64 (the currently verified release platform)
- Microsoft Visual C++ 2015–2022 x64 Redistributable

## Unsigned Build Notice

The Windows 0.4.0 MSI and executable are unsigned. Windows may show an
Unknown Publisher or Microsoft Defender SmartScreen warning. Click
**More info → Run anyway**, or right-click the MSI → **Properties** → tick
**Unblock**. Verify the download against the published SHA-256 checksum.
