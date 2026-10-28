# ATK Player 0.5.9

Bigger play controls, Save buttons in the top row, and an export save location
you set once per review.

## New

- **Bigger play controls.** The first, step, skip and last frame buttons are
  larger, and Play / Pause is the largest button in the bottom bar, on its own
  raised tile. Separators keep the bar's groups apart, and **Draw on Frame**
  is now a palette button to make room.
- **`Space` plays from anywhere in the player.** It used to do nothing while
  the frame-number box or the range in / out fields still had focus after a
  click. Now it plays and pauses there too. Fields you type text into, such as
  notes, still take the space. Holding `Space` no longer flickers between play
  and pause.
- **Save and Save As in the top row.** Two buttons beside Export save the
  review project, the same as **File → Save Project** (`Ctrl+S`) and
  **Save Project As** (`Ctrl+Shift+S`).
- **Set a save location once per review.** The Export Review window has a
  **Project folder** row. **Set Save Location…** picks a folder that is saved
  in the project, so every later export of that review starts there: review
  and notes videos, frames, image sequences and Notes PDFs. **Browse…** still
  picks a different place for one export, and **Clear** goes back to saving
  beside the project. Save the project after setting it to keep it.
- **The Review Library fits the window.** There is no sideways scroll bar any
  more. Long folder paths are shortened in the middle, keeping the drive and
  the last folders; hover a path to see it in full.

## Upgrading

Install the 0.5.9 MSI over 0.5.8, 0.5.7, 0.5.6, 0.5.5, 0.5.4, 0.5.3, 0.5.2,
0.5.1, 0.5.0, 0.4.x or 0.3.0. It upgrades in place and keeps your preferences,
projects, media and Review Library. Projects gain an optional save-location
setting; older versions open them and ignore it.

## Requirements

- Windows 11 x64 (the currently verified release platform)
- Microsoft Visual C++ 2015–2022 x64 Redistributable

## Unsigned Build Notice

The Windows 0.5.9 MSI and executable are unsigned. Windows may show an
Unknown Publisher or Microsoft Defender SmartScreen warning. Click
**More info → Run anyway**, or right-click the MSI → **Properties** → tick
**Unblock**. Verify the download against the published SHA-256 checksum.
