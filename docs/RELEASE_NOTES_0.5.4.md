# ATK Player 0.5.4

A fix for ATK Player failing to start, and a confirmation when a project is
saved.

## Fixed

- **ATK Player starts again after being closed maximized.** In 0.5.3, once
  ATK Player had been closed while maximized, every later start failed with no
  window and no message. Restoring the saved window layout moved the review
  header bar while Windows was still preparing the maximized window, which
  left the layout pointing at memory that had been freed. The saved layout is
  now checked before it is restored and is never rearranged afterwards. Your
  saved layout from 0.5.3 is kept; nothing needs to be cleared.

## New

- **Saving says so.** While a project is being saved the pointer shows that
  ATK Player is busy and the status bar reads **Saving name.atkproj…**. When
  the save is done the status bar shows **Saved name.atkproj at** and the time
  for a few seconds. If a save fails, the status bar says the project was not
  saved, as well as the error message.

## Upgrading

Install the 0.5.4 MSI over 0.5.3, 0.5.2, 0.5.1, 0.5.0, 0.4.x or 0.3.0. It
upgrades in place and keeps your preferences, projects and media. The project
format is unchanged from 0.5.3.

## Requirements

- Windows 11 x64 (the currently verified release platform)
- Microsoft Visual C++ 2015–2022 x64 Redistributable

## Unsigned Build Notice

The Windows 0.5.4 MSI and executable are unsigned. Windows may show an
Unknown Publisher or Microsoft Defender SmartScreen warning. Click
**More info → Run anyway**, or right-click the MSI → **Properties** → tick
**Unblock**. Verify the download against the published SHA-256 checksum.
