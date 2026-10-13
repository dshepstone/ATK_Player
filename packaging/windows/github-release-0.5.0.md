Review a whole class or team: make a review from a folder of clips, name every clip after the person who made it from a class or team list, play them back the way you want, and send notes. ATK Player also has a new look throughout.

## Download

**[Download ATK-Player-v0.5.0-Windows.zip](https://github.com/dshepstone/ATK_Player/releases/download/v0.5.0/ATK-Player-v0.5.0-Windows.zip)**

1. Download the ZIP.
2. Extract all files.
3. Open the extracted folder and double-click `ATK-Player-0.5.0-Windows-x64.msi`.
4. Follow the setup wizard.

After installation, launch ATK Player from the Windows Start Menu or use the installer's **Launch ATK Player** checkbox.

The ZIP contains the Windows MSI installer, installation instructions and licence notices. It is about **130 MB** because the MSI includes licence notices and matching source materials for the application and its dependencies.

## What's new

- **Review Library start page.** Started on its own, ATK Player opens a list of your reviews, with search, starring, archiving and thumbnails of the first clips. Clips opened from Explorer, Maya, Harmony or the API still go straight to the player. You can turn the start page off in **Edit → Preferences**.
- **New Review from a folder** (**Ctrl+Shift+N**). Pick a folder (optionally with its subfolders), tick the clips, choose name or date order, and the review is saved next to the clips. Late submissions are offered the next time the review opens. Large and network folders are searched without freezing the window.
- **Rename Clips from Team List.** Match clips to people on a class or staff list by ID or name, then rename either the names shown in ATK Player (the default) or the files themselves, using a pattern such as `{order:02}_{last}_{first}`.
  - Reads Excel `.xlsx`, `.xlsm` and Excel 97–2003 `.xls` files, and CSV or text files, with no extra software. Class lists exported as reports (titles above, signature lines below) are read as they are.
  - When a person has two clips (a resubmission, or a copy already renamed), the second one is marked **Second clip for …**.
  - Everything is checked before any file is touched. Renames can be undone, and an interrupted rename is put back the next time the review opens.
- **Playlist playback modes**, from the arrow beside **Loop**: Play Through Playlist, Stop After Each Clip, Loop This Clip and Loop Whole Playlist.
- **Clip status.** Mark each clip Not reviewed, In review, Needs changes or Approved in the new review header. The status shows on its playlist card.
- **Notes PDF without text.** Switch off **Include text notes** for a PDF of the drawn frames only.
- **Notes search and sort** in the Notes panel.

## Improved

- **New look:** a teal accent and a consistent icon set.
  - A review header shows the review and clip, the status, Export, rename and show folder.
  - The bottom bar is grouped by task.
- **Playlist cards** with thumbnails, frame counts and badges for notes, drawings and status.
- **Drawing row:** tool icons, a simpler brush chooser, a Tool options pop-up and a floating size and opacity pill. The Notes panel starts closed (**F6** or **Notes** at the top right) so the picture stays large.
- **Timeline time labels** sit at round times and no longer repeat on short clips.
- **Keyboard and screen readers:** the Review Library works from the keyboard, and every control has a name for screen readers.

Full notes: [RELEASE_NOTES_0.5.0.md](https://github.com/dshepstone/ATK_Player/blob/v0.5.0/docs/RELEASE_NOTES_0.5.0.md). Walkthrough: [User Guide](https://github.com/dshepstone/ATK_Player/blob/v0.5.0/docs/USER_GUIDE.md).

## Upgrading

Install the 0.5.0 MSI over 0.4.x or 0.3.0. It upgrades in place and keeps your preferences, projects and media.

A project in which any clip has a status other than **Not reviewed** is saved as project version 3. ATK Player 0.4.x cannot open it; it reports that the project was made by a newer version rather than dropping the statuses. Projects without statuses are saved exactly as before.

## Requirements

- Windows 11 x64 (the currently verified release platform)
- Microsoft Visual C++ 2015&ndash;2022 x64 Redistributable

## Unsigned Build Notice

The Windows 0.5.0 MSI and executable are unsigned. Windows may show an Unknown Publisher or Microsoft Defender SmartScreen warning. Click **More info &rarr; Run anyway**, or right-click the MSI &rarr; **Properties** &rarr; tick **Unblock**. Verify the download against the published SHA-256 checksum.

## Verify your download

In PowerShell, run this from the folder containing the downloaded ZIP:

~~~powershell
Get-FileHash .\ATK-Player-v0.5.0-Windows.zip -Algorithm SHA256
~~~

Expected ZIP SHA-256:

~~~text
{{ZIP_SHA256}}
~~~

After extracting, you can also verify the MSI:

~~~powershell
Get-FileHash .\ATK-Player-0.5.0-Windows-x64.msi -Algorithm SHA256
~~~

Expected MSI SHA-256:

~~~text
{{MSI_SHA256}}
~~~

## Code signing policy

ATK Player is preparing to apply to SignPath Foundation. Approval and signing integration are pending; the attached Windows artifacts remain unsigned. See the [Code signing policy](https://github.com/dshepstone/ATK_Player/blob/main/CODE_SIGNING_POLICY.md) for maintainer roles, manual release approval, and privacy information.
