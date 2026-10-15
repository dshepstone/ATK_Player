Status bar messages now show, so saving says so, and review projects you already have can be added to the Review Library.

## Download

**[Download ATK-Player-v0.5.5-Windows.zip](https://github.com/dshepstone/ATK_Player/releases/download/v0.5.5/ATK-Player-v0.5.5-Windows.zip)**

1. Download the ZIP.
2. Extract all files.
3. Open the extracted folder and double-click `ATK-Player-0.5.5-Windows-x64.msi`.
4. Follow the setup wizard.

After installation, launch ATK Player from the Windows Start Menu or use the installer's **Launch ATK Player** checkbox.

The ZIP contains the Windows MSI installer, installation instructions and licence notices. It is about **130 MB** because the MSI includes licence notices and matching source materials for the application and its dependencies.

## What's new

- **Fixed: status bar messages show.** The frame readout filled the status bar and left no room for messages, so none appeared, including 0.5.4's save confirmation. **Save** and **Save As** now show **Saving name.atkproj…** and then **Saved name.atkproj at** the time, in the player and on the Review Library.
- **Add Existing reviews.** The Review Library's **Add Existing…** button lists `.atkproj` projects you already have, without opening them.
- **Opened projects are listed.** A project opened with Open Project, Recent Projects or from Explorer is added to the Review Library.

Full notes: [RELEASE_NOTES_0.5.5.md](https://github.com/dshepstone/ATK_Player/blob/v0.5.5/docs/RELEASE_NOTES_0.5.5.md). Walkthrough: [User Guide](https://github.com/dshepstone/ATK_Player/blob/v0.5.5/docs/USER_GUIDE.md).

## Upgrading

Install the 0.5.5 MSI over 0.5.4, 0.5.3, 0.5.2, 0.5.1, 0.5.0, 0.4.x or 0.3.0. It upgrades in place and keeps your preferences, projects and media. The project format is unchanged from 0.5.3.

## Requirements

- Windows 11 x64 (the currently verified release platform)
- Microsoft Visual C++ 2015&ndash;2022 x64 Redistributable

## Unsigned Build Notice

The Windows 0.5.5 MSI and executable are unsigned. Windows may show an Unknown Publisher or Microsoft Defender SmartScreen warning. Click **More info &rarr; Run anyway**, or right-click the MSI &rarr; **Properties** &rarr; tick **Unblock**. Verify the download against the published SHA-256 checksum.

## Verify your download

In PowerShell, run this from the folder containing the downloaded ZIP:

~~~powershell
Get-FileHash .\ATK-Player-v0.5.5-Windows.zip -Algorithm SHA256
~~~

Expected ZIP SHA-256:

~~~text
{{ZIP_SHA256}}
~~~

After extracting, you can also verify the MSI:

~~~powershell
Get-FileHash .\ATK-Player-0.5.5-Windows-x64.msi -Algorithm SHA256
~~~

Expected MSI SHA-256:

~~~text
{{MSI_SHA256}}
~~~

## Code signing policy

ATK Player is preparing to apply to SignPath Foundation. Approval and signing integration are pending; the attached Windows artifacts remain unsigned. See the [Code signing policy](https://github.com/dshepstone/ATK_Player/blob/main/CODE_SIGNING_POLICY.md) for maintainer roles, manual release approval, and privacy information.
