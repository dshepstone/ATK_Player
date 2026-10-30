# ATK Player 0.6.0

Arrange the playlist with categories, set in and out points with `[` and `]`,
widen the picture with Cinema mode, and keep your export choices per review.

## New

- **Arrange Playlist.** The list button at the bottom right of the playlist
  (or **Arrange Playlist…** on a card's right-click menu) opens a window for
  reordering. Pick several clips and move them together with the arrows, drag
  them, or type the number of the place to send them to. **Sort by** orders
  the list by name (numbers in order, so `shot2` comes before `shot10`), file
  date, length, category or review status, or reverses it. Nothing changes
  until **Apply**, and the clip you are watching stays loaded.
- **Categories.** Name your own groups, such as class sections or shot types,
  and file each clip in one. The category shows as a tag on the clip's card,
  and a card's right-click **Category** menu files one clip quickly.
- **Category headings and filter.** The playlist shows a heading with the
  name and clip count above each group. The category box under the
  **PLAYLIST** title shows one category at a time, or only the clips with no
  category; playing through the playlist skips the clips it hides.
- **In and out points with `[` and `]`.** The **[ In** and **Out ]** buttons
  under the timeline (or `[` and `]`) mark the current frame with a bracket on
  the timeline, and playback and Loop stay between the marks. The timeline no
  longer zooms to the marks. The **On/Off** button (`\`) switches the points
  off to play the whole clip and back on where they were. Right-click any of
  the three to clear them. Each clip keeps its own points. In Draw mode `[`
  and `]` still change the brush size.
- **Cinema mode.** The wide-rectangle button at the right of the bottom bar
  (or `C`, or **View → Cinema Mode**) widens the picture without going full
  screen: the playlist steps aside and the timeline drops to the basics. Press
  it again to put everything back.
- **Export choices remembered per review.** The Export window's burn-in card
  has **Remember these choices for this review**, so every later export of
  that review starts from the same drawings, notes and burn-in choices.
  **Use Defaults** goes back to the standard choices for one export.
- **Frame number always on the playhead**, outlined at rest and filled yellow
  while you drag.
- **Smoother scrubbing over drawings.** Scrubbing past frames with many
  drawings and notes no longer drags; frames you have already passed redraw
  as fast as frames without drawings.

## Upgrading

Install the 0.6.0 MSI over 0.5.9, 0.5.8, 0.5.7, 0.5.6, 0.5.5, 0.5.4, 0.5.3,
0.5.2, 0.5.1, 0.5.0, 0.4.x or 0.3.0. It upgrades in place and keeps your
preferences, projects, media and Review Library. Projects gain optional
categories, per-clip in/out points and export choices; older versions open
them and ignore what they don't know. The in and out shortcuts moved from
`I` / `O` to `[` / `]`.

## Requirements

- Windows 11 x64 (the currently verified release platform)
- Microsoft Visual C++ 2015–2022 x64 Redistributable

## Unsigned Build Notice

The Windows 0.6.0 MSI and executable are unsigned. Windows may show an
Unknown Publisher or Microsoft Defender SmartScreen warning. Click
**More info → Run anyway**, or right-click the MSI → **Properties** → tick
**Unblock**. Verify the download against the published SHA-256 checksum.
