# ATK Player 0.5.7

A new application icon.

## New

- **New ATK icon.** The interlocking ATK logo replaces the old icon on the
  window title bar, taskbar, Start Menu shortcut, `.atkproj` project files,
  the installer and Apps & features, and the welcome window.
  - It is drawn from a high-resolution master at every size Windows asks for,
    from 16 to 256 pixels, so it stays sharp on high-DPI displays instead of
    being enlarged from a 32-pixel image.
  - The setup wizard's welcome and banner artwork use the new logo.

## Upgrading

Install the 0.5.7 MSI over 0.5.6, 0.5.5, 0.5.4, 0.5.3, 0.5.2, 0.5.1, 0.5.0,
0.4.x or 0.3.0. It upgrades in place and keeps your preferences, projects,
media and Review Library. The project format is unchanged from 0.5.3.

Windows caches icons, so a Start Menu shortcut or taskbar pin may show the old
icon until you sign out and back in.

## Requirements

- Windows 11 x64 (the currently verified release platform)
- Microsoft Visual C++ 2015–2022 x64 Redistributable

## Unsigned Build Notice

The Windows 0.5.7 MSI and executable are unsigned. Windows may show an
Unknown Publisher or Microsoft Defender SmartScreen warning. Click
**More info → Run anyway**, or right-click the MSI → **Properties** → tick
**Unblock**. Verify the download against the published SHA-256 checksum.
