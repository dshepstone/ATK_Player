# ATK Player 0.5.6

Notes PDFs for a whole playlist in one step.

## New

- **Export Notes PDFs for Playlist.** File → **Export Notes PDFs for
  Playlist…** (also in the review header's Export menu) saves a Notes PDF for
  each clip you choose, all into one folder, as `<clip>_review_notes.pdf`, the
  name Export Notes PDF gives a single clip. Each PDF is the same as Export
  Notes PDF makes: one row per drawn or noted frame.
  - The dialog lists every clip in the playlist with how many frames its PDF
    would list. Clips with drawings or notes start ticked; clips with nothing
    to list, or whose media is missing or still being read, cannot be chosen.
    **Select All** and **Select None** tick or clear the rest.
  - Choose the folder (the project's folder to start with), the paper, and the
    same content choices as Export Notes PDF: text notes, resolved notes and
    tracing paper.
  - The PDFs are made one after another with one progress window; **Cancel**
    stops the batch, keeping the PDFs already saved.
  - When some of the PDFs are already in the folder, choose **Replace** or
    **Keep Both** (the new ones are saved as `name (2).pdf`). Two clips with
    the same file name get numbered PDFs rather than one replacing the other.
  - The open clip's PDF includes changes not yet saved to the project, as
    Export Notes PDF does.

## Upgrading

Install the 0.5.6 MSI over 0.5.5, 0.5.4, 0.5.3, 0.5.2, 0.5.1, 0.5.0, 0.4.x or
0.3.0. It upgrades in place and keeps your preferences, projects, media and
Review Library. The project format is unchanged from 0.5.3.

## Requirements

- Windows 11 x64 (the currently verified release platform)
- Microsoft Visual C++ 2015–2022 x64 Redistributable

## Unsigned Build Notice

The Windows 0.5.6 MSI and executable are unsigned. Windows may show an
Unknown Publisher or Microsoft Defender SmartScreen warning. Click
**More info → Run anyway**, or right-click the MSI → **Properties** → tick
**Unblock**. Verify the download against the published SHA-256 checksum.
