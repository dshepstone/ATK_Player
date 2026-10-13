# ATK Player 0.5.0

Reviewing a whole class or team. Make a review from a folder of clips, name
every clip after the person who made it from a class or team list, play them
back the way you want, and send notes. The interface has a new look throughout.
See the [User Guide](USER_GUIDE.md) for a walkthrough.

## New

- **Review Library start page.** Started on its own, ATK Player opens a list of
  your reviews: search, star, archive, thumbnails of the first clips, and
  Open. Clips opened from Explorer, Maya, Harmony or the API still go straight
  to the player. Turn the start page off in Preferences.
- **New Review from a folder** (`Ctrl+Shift+N`). Pick a folder (optionally with
  its subfolders), tick the clips, choose name or date order, and the review
  is saved next to the clips. Late submissions are offered the next time the
  review is opened. Large and network folders are searched without freezing
  the window.
- **Rename Clips from Team List.** Match clips to people on a class or staff
  list by ID or name, then rename only the names shown in ATK Player (the
  default) or the files themselves, with a name pattern such as
  `{order:02}_{last}_{first}`.
  - Reads Excel `.xlsx`, `.xlsm` and Excel 97–2003 `.xls`, and CSV or text
    files, with no extra software. Report-style class lists (titles above,
    signature lines below) are read as they are.
  - A clip that is the second for the same person is marked as such.
  - Everything is checked before a file is touched; renames can be undone,
    and an interrupted rename is recovered the next time the review opens.
- **Playlist playback modes**, from the arrow beside Loop: Play Through
  Playlist, Stop After Each Clip, Loop This Clip, Loop Whole Playlist.
- **Clip status**: Not reviewed, In review, Needs changes or Approved, set in
  the new review header and shown on each playlist card.
- **Notes PDF without text**: switch off **Include text notes** for a PDF of
  the drawn frames only.
- **Notes search and sort** in the Notes panel.

## Improved

- **New look**: a teal accent, a consistent icon set, a review header
  (review › clip, status, Export, rename, show folder), and a bottom bar
  grouped as drawing, range and loop, transport, view tools, and display.
- **Playlist cards** with thumbnails, frame counts and badges for notes,
  drawings and status. Thumbnails are made in the background and cached.
- **Drawing row**: tool icons with a simpler brush chooser, a Tool options
  pop-up, and a floating size and opacity pill. The Notes panel starts closed
  (`F6` or **Notes** at the top right) so the picture stays large.
- **Drawing stays quick** however many strokes are on a frame, and long
  strokes only redraw the part of the screen that changes.
- **Timeline time labels** are now at round times (every 1, 2, 5, 10, 15 or 30
  seconds and so on), so they no longer repeat on short clips.
- **Keyboard and screen readers**: the Review Library works from the keyboard
  (`Tab`, arrow keys, `Space` or `Enter`), and every button, list and field has
  a name for screen readers.

## Projects

A project in which any clip has a status other than **Not reviewed** is saved
as project version 3. ATK Player 0.4.x cannot open it (it reports that the
project was made by a newer version) rather than silently dropping the
statuses. Projects without statuses are saved exactly as before.

## Upgrading

Install the 0.5.0 MSI over 0.4.x or 0.3.0. It upgrades in place and keeps
your preferences, projects and media.

## Requirements

- Windows 11 x64 (the currently verified release platform)
- Microsoft Visual C++ 2015–2022 x64 Redistributable

## Unsigned Build Notice

The Windows 0.5.0 MSI and executable are unsigned. Windows may show an
Unknown Publisher or Microsoft Defender SmartScreen warning. Click
**More info → Run anyway**, or right-click the MSI → **Properties** → tick
**Unblock**. Verify the download against the published SHA-256 checksum.
