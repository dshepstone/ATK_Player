# ATK Player 0.5.5

Status bar messages now show, so saving says so, and review projects
you already have can be added to the Review Library.

## Fixed

- **Status bar messages show.** The frame readout along the bottom filled the
  whole status bar, which left no room for messages, so none appeared,
  including 0.5.4's save confirmation. Messages now show in the readout,
  after the media name. **Save** and **Save As** both show
  **Saving name.atkproj…** and then **Saved name.atkproj at** the time; a
  failed save says the project was not saved. On the Review Library, where
  the status bar is normally hidden, it appears while a message is showing.
- **A/B Comparison: B follows the offset when started early.** Starting a
  comparison straight after opening a project, before ATK Player had finished
  reading clip B, could hold B on its first frame whatever the B offset. B now
  runs to its last frame.

## New

- **Add Existing reviews.** The Review Library has an **Add Existing…**
  button. Choose one or more `.atkproj` files and they are listed, with their
  clip count and preview strip, without being opened; open them from the
  list as usual. Adding one that is already listed brings it back from the
  archive and updates its counts.
- **Opened projects are listed.** A project opened with **Open Project…**,
  **Recent Projects**, from Explorer or through the local API is added to the
  Review Library, so it is there next time.

## Upgrading

Install the 0.5.5 MSI over 0.5.4, 0.5.3, 0.5.2, 0.5.1, 0.5.0, 0.4.x or 0.3.0.
It upgrades in place and keeps your preferences, projects, media and Review
Library. The project format is unchanged from 0.5.3.

## Requirements

- Windows 11 x64 (the currently verified release platform)
- Microsoft Visual C++ 2015–2022 x64 Redistributable

## Unsigned Build Notice

The Windows 0.5.5 MSI and executable are unsigned. Windows may show an
Unknown Publisher or Microsoft Defender SmartScreen warning. Click
**More info → Run anyway**, or right-click the MSI → **Properties** → tick
**Unblock**. Verify the download against the published SHA-256 checksum.
