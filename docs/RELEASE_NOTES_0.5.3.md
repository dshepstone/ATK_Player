# ATK Player 0.5.3

Review tools in the style of SyncSketch: hold a drawing over the frames that
follow it, change the playback speed, and zoom from the bottom bar. Dragging
notes and drawings along the timeline becomes a switch you turn on, and the
playlist numbers are easier to read.

## New

- **Hold Drawings.** Click the pin button in the bottom bar, or press
  `Shift+H`. Each drawing then stays on screen on the frames after it, until
  the next drawing, like a held key drawing. A faint bar under the timeline
  shows how far each drawing holds. The arrow beside the button chooses what
  drawing on a held frame does:
  - **Add to Current Drawing** adds your strokes to the held drawing, on the
    frame where it was drawn.
  - **Add New Drawing** starts a new drawing on the frame on screen, which
    then holds from there.
  - **Don't Hold Drawings** turns holding off. This is the default.

  Erasing, moving, clearing, copying or cutting a held drawing changes it on
  the frame where it was drawn. Holding only changes what you see; exports
  keep their own "keep on screen" setting.
- **Playback speed.** The speed button beside Loop shows the frame rate the
  clip plays at, such as `24fps`, or `12fps` at half speed. Its menu offers
  0.1x to 2x. Press `<` for slower, `>` for faster and `K` for normal speed.
  The clip plays without sound away from normal speed. Frame stepping and
  scrubbing are not affected.
- **Zoom button.** The bottom bar shows the zoom level, **Fit** or a
  percentage. Its menu zooms in and out, fits the picture to the viewer, or
  picks 25%, 50%, 100%, 200% or 400%.
- **Flip Canvas on the drawing row.** The flip button (`H`) is now on the
  drawing row as well as in the bottom bar.
- **Numbered playlist.** Each playlist card has its number in a strip down
  its left edge. The clip in the viewer has its strip filled with the accent
  colour.

## Changed

- **Drag Markers is now a switch.** Turn it on with the Drag Markers button in
  the bottom bar (arrows pointing apart) to drag the orange marks under the
  timeline. Each mark moves its frame's notes and drawings to another frame,
  or copies them if you hold `Ctrl`. The switch is off to start with, so a
  mark can't be moved by accident. Clicking a mark still goes to its frame.

All the new commands are in the menus and can be given other shortcuts under
**Edit → Preferences → Shortcuts**.

## Upgrading

Install the 0.5.3 MSI over 0.5.2, 0.5.1, 0.5.0, 0.4.x or 0.3.0. It upgrades in
place and keeps your preferences, projects and media. The project format is
unchanged from 0.5.2.

## Requirements

- Windows 11 x64 (the currently verified release platform)
- Microsoft Visual C++ 2015–2022 x64 Redistributable

## Unsigned Build Notice

The Windows 0.5.3 MSI and executable are unsigned. Windows may show an
Unknown Publisher or Microsoft Defender SmartScreen warning. Click
**More info → Run anyway**, or right-click the MSI → **Properties** → tick
**Unblock**. Verify the download against the published SHA-256 checksum.
