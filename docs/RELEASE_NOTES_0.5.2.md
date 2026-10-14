# ATK Player 0.5.2

Drawings and notes can now travel between frames. Copy, cut and paste them,
or drag a frame's notes and drawings along the timeline to another frame.

## New

- **Copy, cut and paste between frames.** Use the new copy, cut and paste
  buttons on the drawing row, or `Ctrl+C`, `Ctrl+X` and `Ctrl+V`.
  - With a drawing picked by the select tool, they copy or cut just that
    drawing. With nothing picked, they take everything on the frame, drawings
    and notes.
  - Paste puts them on the frame on screen, in the same place on the picture.
    It works on another clip too.
  - A single pasted drawing comes up selected, so you can drag it into position
    straight away.
  - In the Notes panel, right-click a note and choose **Copy Note** or
    **Cut Note**, then paste it on another frame.
  - In text fields the keys still copy and paste text as usual.
- **Drag notes and drawings along the timeline.** The small orange marks under
  the timeline show which frames have notes or drawings. Drag a mark to another
  frame to move everything there, or hold `Ctrl` as you let go to copy it.
  Clicking a mark without dragging still goes to that frame.

Each paste, cut or drag is one undo step (`Ctrl+Z`).

## Upgrading

Install the 0.5.2 MSI over 0.5.1, 0.5.0, 0.4.x or 0.3.0. It upgrades in place
and keeps your preferences, projects and media. The project format is
unchanged from 0.5.1.

## Requirements

- Windows 11 x64 (the currently verified release platform)
- Microsoft Visual C++ 2015–2022 x64 Redistributable

## Unsigned Build Notice

The Windows 0.5.2 MSI and executable are unsigned. Windows may show an
Unknown Publisher or Microsoft Defender SmartScreen warning. Click
**More info → Run anyway**, or right-click the MSI → **Properties** → tick
**Unblock**. Verify the download against the published SHA-256 checksum.
