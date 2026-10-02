# ATK Player 0.3.0

ATK Player now reviews pictures as well as video. You can open single stills
and whole numbered render sequences, play them at the rate you choose, and
mirror the viewer for any source.

## What's new

- **Still images.** PNG, JPEG, TIFF, BMP, TGA, WebP and OpenEXR files open
  anywhere a video does: Open Media, Add to Playlist, Relink, projects, A/B
  comparison and every export.
  - Each still is held for a set number of frames: 48 by default, at least
    10. Change the default in **Preferences → Review → Still image
    duration**.
  - Transparent areas show over black, and EXR is shown with an sRGB display
    transfer.
- **Image sequences.** Open any numbered frame, such as `shot.1001.exr`, and
  choose **Image Sequence** to load the whole render as one playlist entry,
  shown as `shot.[1001-1096].exr`.
  - Selecting or dropping all of a sequence's frames asks only once.
  - Missing frames hold the previous frame, and Media Information counts them.
  - Projects save a sequence as one path plus its frame range.
- **Frame rate for images.** Sequences and stills play at the rate set in
  **Preferences → Review** (23.976, 24, 25, 29.97, 30, 48, 50, 59.94 or 60;
  default 24).
  - Each source keeps its own rate. Right-click it in the Sources panel →
    **Frame Rate…** to change it.
  - Bookmarks and review ranges stay on the same frames when the rate changes.
- **Flip Horizontal.** **View → Flip Horizontal** (`H`) mirrors the picture
  for video and images. This includes both A/B panes and the Wipe, Blend and
  Difference views.
  - A **FLIPPED H** badge shows while it's on.
  - It turns off when a source opens, is never saved and never affects
    exports.
- **Drag and drop.** Drop video or image files onto the window to add them to
  the playlist.
- **Media Information** now shows the source type: video, still image or
  image sequence.

## For developers and pipeline integrations

- FFmpeg is now built with the vcpkg `zlib` feature, which provides PNG, EXR
  and deflate-TIFF decoding.
  - zlib is under the permissive zlib license.
  - `z.dll` ships beside the FFmpeg DLLs, and its licence is installed as
    `licenses/zlib.txt`.
- `.atkproj` remains format version 1. Image sources add optional `"still"`
  and `"sequence"` objects that older versions ignore.
- The local API's `open_media` and `add_media` never show the sequence prompt.
  They always add single files.
- Design notes: [STILL_IMAGE_SOURCES.md](STILL_IMAGE_SOURCES.md).

## Upgrading

Install the 0.3.0 MSI over 0.2.1. It upgrades in place and keeps your
preferences, projects and media.

## Known limitations

- Photos are not rotated from their EXIF orientation.
- There is no colour management for images; ICC profiles are ignored.
- The timeline shows frame positions starting at 1, not the sequence's own
  numbers (1001…).
- A sequence's frame range is fixed when it opens. Relink to pick up frames
  rendered later.

## Requirements

- Windows 11 x64 (the currently verified release platform)
- Microsoft Visual C++ 2015–2022 x64 Redistributable

## Unsigned Build Notice

The Windows 0.3.0 MSI and executable are unsigned. Windows may show an
Unknown Publisher or Microsoft Defender SmartScreen warning. Click
**More info → Run anyway**, or right-click the MSI → **Properties** → tick
**Unblock**. Verify the download against the published SHA-256 checksum.
