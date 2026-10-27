# ATK Player 0.5.8

Move a group of notes along the timeline, a bigger current-frame number, and
an Open Folder button.

## New

- **Select several timeline markers and move them together**, like keys in
  Maya's time slider.
  - Hold `Shift` and drag across the timeline to select every note and drawing
    marker in that range. The selection is shaded in translucent blue.
  - `Shift`+click a marker to add it to the selection or take it out.
  - Drag inside the blue band to slide the whole group along the timeline. A
    label shows how far it will move, and each marker's landing spot is shown
    before you let go. Every frame keeps its own notes and drawings, and the
    group keeps its spacing. Hold `Ctrl` as you let go to copy instead.
  - The whole move is one undo step. Click outside the band or press `Esc` to
    clear the selection. It works whether Drag Markers is on or off; dragging
    a single marker works as before.
- **Large current-frame number.** The frame number beside the timeline is now
  large, bold and yellow, about the height of the frame ruler, with
  `FRAME #` underneath. Click it and type a frame number to jump there, as
  before.
- **Open Folder.** A new button beside the clip name opens the folder of the
  clip playing in the viewer, with the clip selected, in Windows Explorer (or
  Finder on macOS). The folder icon at the top right now selects the clip too.

## Upgrading

Install the 0.5.8 MSI over 0.5.7, 0.5.6, 0.5.5, 0.5.4, 0.5.3, 0.5.2, 0.5.1,
0.5.0, 0.4.x or 0.3.0. It upgrades in place and keeps your preferences,
projects, media and Review Library. The project format is unchanged from
0.5.3.

## Requirements

- Windows 11 x64 (the currently verified release platform)
- Microsoft Visual C++ 2015–2022 x64 Redistributable

## Unsigned Build Notice

The Windows 0.5.8 MSI and executable are unsigned. Windows may show an
Unknown Publisher or Microsoft Defender SmartScreen warning. Click
**More info → Run anyway**, or right-click the MSI → **Properties** → tick
**Unblock**. Verify the download against the published SHA-256 checksum.
