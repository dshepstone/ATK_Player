Move and copy your drawings and notes between frames: copy, cut and paste them, or drag a frame's notes and drawings along the timeline.

## Download

**[Download ATK-Player-v0.5.2-Windows.zip](https://github.com/dshepstone/ATK_Player/releases/download/v0.5.2/ATK-Player-v0.5.2-Windows.zip)**

1. Download the ZIP.
2. Extract all files.
3. Open the extracted folder and double-click `ATK-Player-0.5.2-Windows-x64.msi`.
4. Follow the setup wizard.

After installation, launch ATK Player from the Windows Start Menu or use the installer's **Launch ATK Player** checkbox.

The ZIP contains the Windows MSI installer, installation instructions and licence notices. It is about **130 MB** because the MSI includes licence notices and matching source materials for the application and its dependencies.

## What's new

- **Copy, cut and paste between frames.** Use the new copy, cut and paste buttons on the drawing row, or **Ctrl+C**, **Ctrl+X** and **Ctrl+V**.
  - With a drawing picked by the select tool, they copy or cut just that drawing. With nothing picked, they take everything on the frame, drawings and notes.
  - Paste puts them on the frame on screen, in the same place on the picture, and works on another clip too. A single pasted drawing comes up selected, ready to drag into position.
  - In the Notes panel, right-click a note and choose **Copy Note** or **Cut Note**, then paste it on another frame.
- **Drag notes and drawings along the timeline.** Drag one of the orange marks under the timeline to another frame to move that frame's notes and drawings there. Hold **Ctrl** as you let go to copy instead.
- Each paste, cut or drag is one undo step.

Full notes: [RELEASE_NOTES_0.5.2.md](https://github.com/dshepstone/ATK_Player/blob/v0.5.2/docs/RELEASE_NOTES_0.5.2.md). Walkthrough: [User Guide](https://github.com/dshepstone/ATK_Player/blob/v0.5.2/docs/USER_GUIDE.md).

## Upgrading

Install the 0.5.2 MSI over 0.5.1, 0.5.0, 0.4.x or 0.3.0. It upgrades in place and keeps your preferences, projects and media. The project format is unchanged from 0.5.1.

## Requirements

- Windows 11 x64 (the currently verified release platform)
- Microsoft Visual C++ 2015&ndash;2022 x64 Redistributable

## Unsigned Build Notice

The Windows 0.5.2 MSI and executable are unsigned. Windows may show an Unknown Publisher or Microsoft Defender SmartScreen warning. Click **More info &rarr; Run anyway**, or right-click the MSI &rarr; **Properties** &rarr; tick **Unblock**. Verify the download against the published SHA-256 checksum.

## Verify your download

In PowerShell, run this from the folder containing the downloaded ZIP:

~~~powershell
Get-FileHash .\ATK-Player-v0.5.2-Windows.zip -Algorithm SHA256
~~~

Expected ZIP SHA-256:

~~~text
{{ZIP_SHA256}}
~~~

After extracting, you can also verify the MSI:

~~~powershell
Get-FileHash .\ATK-Player-0.5.2-Windows-x64.msi -Algorithm SHA256
~~~

Expected MSI SHA-256:

~~~text
{{MSI_SHA256}}
~~~

## Code signing policy

ATK Player is preparing to apply to SignPath Foundation. Approval and signing integration are pending; the attached Windows artifacts remain unsigned. See the [Code signing policy](https://github.com/dshepstone/ATK_Player/blob/main/CODE_SIGNING_POLICY.md) for maintainer roles, manual release approval, and privacy information.
