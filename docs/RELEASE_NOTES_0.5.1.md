# ATK Player 0.5.1

Drawings can now be adjusted after you draw them. Select a drawing and resize,
rotate or reshape it with handles, the way Free Transform works in Photoshop.

## New

- **Free transform for drawings.** Choose the select tool in the drawing row
  and click a drawing to show its handles.
  - **Strokes, rectangles and ellipses:** drag a square handle to resize. Hold
    `Shift` to keep the proportions, or `Alt` to resize from the centre.
  - **Rotate** by dragging the round knob above the drawing, or by dragging
    just outside a corner. Hold `Shift` to snap to 15° steps. A rotated drawing
    keeps resizing along its own sides.
  - **Lines and arrows** have a handle on each end, so you can re-aim them
    directly.
  - **Text** resizes from its corners. The lettering grows or shrinks with the
    box and keeps its proportions.
  - Press inside the box, even where there is no ink (the middle of an
    ellipse, say), to move the drawing.
  - Press `Esc` during a drag to put the drawing back. Each change is one undo
    step (`Ctrl+Z`).
  - Line weight and brush size stay the same while you resize; only the shape
    changes.

Rotated drawings appear the same way in ghosts, the annotated MP4 and the
Review Notes PDF.

## Upgrading

Install the 0.5.1 MSI over 0.5.0, 0.4.x or 0.3.0. It upgrades in place and
keeps your preferences, projects and media.

A project with a rotated rectangle or ellipse is saved as project version 4.
ATK Player 0.5.0 and earlier can't open it. They report that the project was
made by a newer version, instead of showing the shape upright and saving it
that way. Projects without rotated shapes are saved exactly as before, and
rotated strokes, lines and arrows don't change the version.

## Requirements

- Windows 11 x64 (the currently verified release platform)
- Microsoft Visual C++ 2015–2022 x64 Redistributable

## Unsigned Build Notice

The Windows 0.5.1 MSI and executable are unsigned. Windows may show an
Unknown Publisher or Microsoft Defender SmartScreen warning. Click
**More info → Run anyway**, or right-click the MSI → **Properties** → tick
**Unblock**. Verify the download against the published SHA-256 checksum.
