Arrange the playlist with categories, set in and out points with `[` and `]`, widen the picture with Cinema mode, and keep your export choices per review.

## Download

**[Download ATK-Player-v0.6.0-Windows.zip](https://github.com/dshepstone/ATK_Player/releases/download/v0.6.0/ATK-Player-v0.6.0-Windows.zip)**

1. Download the ZIP.
2. Extract all files.
3. Open the extracted folder and double-click `ATK-Player-0.6.0-Windows-x64.msi`.
4. Follow the setup wizard.

After installation, launch ATK Player from the Windows Start Menu or use the installer's **Launch ATK Player** checkbox.

The ZIP contains the Windows MSI installer, installation instructions and licence notices. It is about **130 MB** because the MSI includes licence notices and matching source materials for the application and its dependencies.

## What's new

- **Arrange Playlist** with multi-select, drag, typed positions and **Sort by** (name, date, length, category, status).
- **Categories** for clips, with a heading above each group in the playlist and a filter to show one category at a time.
- **In and out points with `[` and `]`**: brackets on the timeline, playback stays between them, and an **On/Off** button (`\`) to play the whole clip.
- **Cinema mode** (`C`): a wider picture without full screen. The playlist steps aside and the timeline drops to the basics.
- **Export choices remembered per review**, so every export of a review starts from the same burn-in choices.
- **Frame number always on the playhead**, and **smoother scrubbing** over frames with many drawings and notes.

Full notes: [RELEASE_NOTES_0.6.0.md](https://github.com/dshepstone/ATK_Player/blob/v0.6.0/docs/RELEASE_NOTES_0.6.0.md). Walkthrough: [User Guide](https://github.com/dshepstone/ATK_Player/blob/v0.6.0/docs/USER_GUIDE.md).

## Upgrading

Install the 0.6.0 MSI over 0.5.9, 0.5.8, 0.5.7, 0.5.6, 0.5.5, 0.5.4, 0.5.3, 0.5.2, 0.5.1, 0.5.0, 0.4.x or 0.3.0. It upgrades in place and keeps your preferences, projects and media. Projects gain optional categories, per-clip in/out points and export choices; older versions open them and ignore them. The in and out shortcuts moved from `I` / `O` to `[` / `]`.

## Requirements

- Windows 11 x64 (the currently verified release platform)
- Microsoft Visual C++ 2015&ndash;2022 x64 Redistributable

## Unsigned Build Notice

The Windows 0.6.0 MSI and executable are unsigned. Windows may show an Unknown Publisher or Microsoft Defender SmartScreen warning. Click **More info &rarr; Run anyway**, or right-click the MSI &rarr; **Properties** &rarr; tick **Unblock**. Verify the download against the published SHA-256 checksum.

## Verify your download

In PowerShell, run this from the folder containing the downloaded ZIP:

~~~powershell
Get-FileHash .\ATK-Player-v0.6.0-Windows.zip -Algorithm SHA256
~~~

Expected ZIP SHA-256:

~~~text
{{ZIP_SHA256}}
~~~

After extracting, you can also verify the MSI:

~~~powershell
Get-FileHash .\ATK-Player-0.6.0-Windows-x64.msi -Algorithm SHA256
~~~

Expected MSI SHA-256:

~~~text
{{MSI_SHA256}}
~~~

## Code signing policy

ATK Player is preparing to apply to SignPath Foundation. Approval and signing integration are pending; the attached Windows artifacts remain unsigned. See the [Code signing policy](https://github.com/dshepstone/ATK_Player/blob/main/CODE_SIGNING_POLICY.md) for maintainer roles, manual release approval, and privacy information.
