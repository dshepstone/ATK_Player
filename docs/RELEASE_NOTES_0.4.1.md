# ATK Player 0.4.1

A performance update for drawing. Drawing on a frame now stays quick however
many strokes are already on it.

## Fixes

- **Drawing no longer slows down as strokes pile up.** Before, every time you
  lifted the pen ATK Player redrew every stroke on the frame, so each new line
  made the next pause a little longer. Hand lettering with 30–40 strokes could
  freeze for about a tenth of a second at every pen-up. Now only the stroke you
  just finished is drawn, so the pause stays the same from the first stroke to
  the hundredth.
- **Imported Photoshop brushes (Custom tool) are much faster.** The brush tip
  is now prepared once for each size and colour and reused, instead of being
  rescaled from its full resolution on every repaint. Undo, erase and moving a
  drawing redraw the whole frame up to about 18 times faster with these brushes.
- **Ghost drawings** no longer redraw when you draw on the current frame. The
  ghosts come from other frames and don't change.

Drawings look exactly the same as in 0.4.0, and projects are unchanged.

## Upgrading

Install the 0.4.1 MSI over 0.4.0 or 0.3.0. It upgrades in place and keeps
your preferences, projects and media.

## Requirements

- Windows 11 x64 (the currently verified release platform)
- Microsoft Visual C++ 2015–2022 x64 Redistributable

## Unsigned Build Notice

The Windows 0.4.1 MSI and executable are unsigned. Windows may show an
Unknown Publisher or Microsoft Defender SmartScreen warning. Click
**More info → Run anyway**, or right-click the MSI → **Properties** → tick
**Unblock**. Verify the download against the published SHA-256 checksum.
