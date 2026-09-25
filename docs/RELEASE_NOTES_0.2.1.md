# ATK Player 0.2.1

A polish release focused on a smoother first run and faster review.

## What's new

- **Welcome screen.** The first launch after installing or upgrading shows a
  welcome window with a quick overview, a link to send feedback on GitHub and
  the creator's website. Reopen it any time from
  **Help → Welcome to ATK Player**.
- **Frame number while scrubbing.** Dragging the playhead now shows the current
  frame number in a badge just above it, so you always know where you are.
- **Fewer save prompts.** Opening or closing a single video no longer asks you
  to save a project. ATK Player now asks only when there's review work worth
  keeping: bookmarks, a playlist of more than one clip, or changes to a saved
  `.atkproj` project.
- **Cleaner default layout.** The Bookmarks panel starts closed so the viewer
  gets the full width. Open it with `F5`, **View → Bookmarks Panel**, or by
  creating a range bookmark.

## For developers and pipeline integrations

- The API's `discardUnsaved` guard uses the same rule as the save prompt, so
  `open_media` on an untitled single-clip session with no bookmarks no longer
  needs `discardUnsaved: true`.
- `build-installer.ps1` can sign through Azure Trusted Signing
  (`-TrustedSigningDlib` / `-TrustedSigningMetadata`). See
  `packaging/windows/CODE_SIGNING.md`.

## Upgrading

Install the 0.2.1 MSI over 0.2.0. It upgrades in place and keeps your
preferences, projects and media.

## Requirements

- Windows 11 x64 (the currently verified release platform)
- Microsoft Visual C++ 2015–2022 x64 Redistributable

## Unsigned Build Notice

The Windows 0.2.1 MSI and executable are unsigned. Windows may show an
Unknown Publisher or Microsoft Defender SmartScreen warning. Click
**More info → Run anyway**, or right-click the MSI → **Properties** → tick
**Unblock**. Verify the download against the published SHA-256 checksum.
