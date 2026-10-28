Bigger play controls, Save buttons in the top row, and an export save location you set once per review.

## Download

**[Download ATK-Player-v0.5.9-Windows.zip](https://github.com/dshepstone/ATK_Player/releases/download/v0.5.9/ATK-Player-v0.5.9-Windows.zip)**

1. Download the ZIP.
2. Extract all files.
3. Open the extracted folder and double-click `ATK-Player-0.5.9-Windows-x64.msi`.
4. Follow the setup wizard.

After installation, launch ATK Player from the Windows Start Menu or use the installer's **Launch ATK Player** checkbox.

The ZIP contains the Windows MSI installer, installation instructions and licence notices. It is about **130 MB** because the MSI includes licence notices and matching source materials for the application and its dependencies.

## What's new

- **Bigger play controls.** Larger frame and skip buttons, with Play / Pause the largest button in the bottom bar. Draw on Frame is now a palette button, and separators keep the bar's groups apart.
- **`Space` plays from anywhere in the player**, including right after typing a frame number. Text fields such as notes still take the space, and holding `Space` no longer flickers.
- **Save and Save As** buttons in the top row, beside Export.
- **Set a save location once per review.** In Export Review, **Set Save Location…** remembers a folder in the project, so every export of that review starts there. **Browse…** still works for one-off exports.
- **The Review Library fits the window**, with long folder paths shortened in the middle and no sideways scroll bar.

Full notes: [RELEASE_NOTES_0.5.9.md](https://github.com/dshepstone/ATK_Player/blob/v0.5.9/docs/RELEASE_NOTES_0.5.9.md). Walkthrough: [User Guide](https://github.com/dshepstone/ATK_Player/blob/v0.5.9/docs/USER_GUIDE.md).

## Upgrading

Install the 0.5.9 MSI over 0.5.8, 0.5.7, 0.5.6, 0.5.5, 0.5.4, 0.5.3, 0.5.2, 0.5.1, 0.5.0, 0.4.x or 0.3.0. It upgrades in place and keeps your preferences, projects and media. Projects gain an optional save-location setting; older versions open them and ignore it.

## Requirements

- Windows 11 x64 (the currently verified release platform)
- Microsoft Visual C++ 2015&ndash;2022 x64 Redistributable

## Unsigned Build Notice

The Windows 0.5.9 MSI and executable are unsigned. Windows may show an Unknown Publisher or Microsoft Defender SmartScreen warning. Click **More info &rarr; Run anyway**, or right-click the MSI &rarr; **Properties** &rarr; tick **Unblock**. Verify the download against the published SHA-256 checksum.

## Verify your download

In PowerShell, run this from the folder containing the downloaded ZIP:

~~~powershell
Get-FileHash .\ATK-Player-v0.5.9-Windows.zip -Algorithm SHA256
~~~

Expected ZIP SHA-256:

~~~text
{{ZIP_SHA256}}
~~~

After extracting, you can also verify the MSI:

~~~powershell
Get-FileHash .\ATK-Player-0.5.9-Windows-x64.msi -Algorithm SHA256
~~~

Expected MSI SHA-256:

~~~text
{{MSI_SHA256}}
~~~

## Code signing policy

ATK Player is preparing to apply to SignPath Foundation. Approval and signing integration are pending; the attached Windows artifacts remain unsigned. See the [Code signing policy](https://github.com/dshepstone/ATK_Player/blob/main/CODE_SIGNING_POLICY.md) for maintainer roles, manual release approval, and privacy information.
