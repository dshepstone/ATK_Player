Review tools in the style of SyncSketch: hold drawings until the next one, change the playback speed, zoom from the bottom bar, and read the playlist by number.

## Download

**[Download ATK-Player-v0.5.3-Windows.zip](https://github.com/dshepstone/ATK_Player/releases/download/v0.5.3/ATK-Player-v0.5.3-Windows.zip)**

1. Download the ZIP.
2. Extract all files.
3. Open the extracted folder and double-click `ATK-Player-0.5.3-Windows-x64.msi`.
4. Follow the setup wizard.

After installation, launch ATK Player from the Windows Start Menu or use the installer's **Launch ATK Player** checkbox.

The ZIP contains the Windows MSI installer, installation instructions and licence notices. It is about **130 MB** because the MSI includes licence notices and matching source materials for the application and its dependencies.

## What's new

- **Hold Drawings** (pin button, **Shift+H**). A drawing stays on screen on the frames after it, until the next drawing. A faint bar under the timeline shows how far each one holds. The arrow beside the button chooses what happens when you draw on a held frame:
  - **Add to Current Drawing** adds your strokes to the held drawing.
  - **Add New Drawing** starts a new drawing on that frame.
  - **Don't Hold Drawings** turns holding off.
- **Playback speed.** The button beside Loop shows the rate the clip plays at, such as **24fps**, or **12fps** at half speed. The menu goes from 0.1x to 2x. Press **<** for slower, **>** for faster and **K** for normal speed. The clip plays without sound away from normal speed.
- **Zoom button.** It shows **Fit** or the zoom percentage. Its menu zooms in and out, fits the picture, or jumps to 25%, 50%, 100%, 200% or 400%.
- **Flip Canvas** is now on the drawing row too.
- **Numbered playlist.** Each card's number sits in a strip down its left edge. The clip in the viewer has its strip in the accent colour.
- **Drag Markers is now a switch.** Turn it on with its button in the bottom bar to drag the orange marks under the timeline and move a frame's notes and drawings, or copy them with **Ctrl**. It starts off, so marks can't be moved by accident.

Full notes: [RELEASE_NOTES_0.5.3.md](https://github.com/dshepstone/ATK_Player/blob/v0.5.3/docs/RELEASE_NOTES_0.5.3.md). Walkthrough: [User Guide](https://github.com/dshepstone/ATK_Player/blob/v0.5.3/docs/USER_GUIDE.md).

## Upgrading

Install the 0.5.3 MSI over 0.5.2, 0.5.1, 0.5.0, 0.4.x or 0.3.0. It upgrades in place and keeps your preferences, projects and media. The project format is unchanged from 0.5.2.

## Requirements

- Windows 11 x64 (the currently verified release platform)
- Microsoft Visual C++ 2015&ndash;2022 x64 Redistributable

## Unsigned Build Notice

The Windows 0.5.3 MSI and executable are unsigned. Windows may show an Unknown Publisher or Microsoft Defender SmartScreen warning. Click **More info &rarr; Run anyway**, or right-click the MSI &rarr; **Properties** &rarr; tick **Unblock**. Verify the download against the published SHA-256 checksum.

## Verify your download

In PowerShell, run this from the folder containing the downloaded ZIP:

~~~powershell
Get-FileHash .\ATK-Player-v0.5.3-Windows.zip -Algorithm SHA256
~~~

Expected ZIP SHA-256:

~~~text
{{ZIP_SHA256}}
~~~

After extracting, you can also verify the MSI:

~~~powershell
Get-FileHash .\ATK-Player-0.5.3-Windows-x64.msi -Algorithm SHA256
~~~

Expected MSI SHA-256:

~~~text
{{MSI_SHA256}}
~~~

## Code signing policy

ATK Player is preparing to apply to SignPath Foundation. Approval and signing integration are pending; the attached Windows artifacts remain unsigned. See the [Code signing policy](https://github.com/dshepstone/ATK_Player/blob/main/CODE_SIGNING_POLICY.md) for maintainer roles, manual release approval, and privacy information.
