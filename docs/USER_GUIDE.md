# ATK Player User Guide — Reviewing a Class or Team

This guide walks through a typical review: turning a folder of submitted clips
into a review, naming the clips after the people who made them, playing them
back, and sending notes. For installation see the [README](../README.md); for
every menu command and its shortcut see **Edit → Preferences → Shortcuts**.

---

## 1. The Review Library

Start ATK Player from the Start Menu or its icon and the **Review Library**
opens: every review you have made, newest first.

- **All Reviews / Starred / Archived** on the left. Star the reviews you are
  working on; archive finished ones to keep the list short (they are not
  deleted).
- **Search** finds reviews by name or folder.
- Each row shows the review's first clips, how many clips it holds, when it
  was last opened and its folder (a long path is shortened in the middle;
  hover it for the full path). **Open** (or a double-click) opens it in the player; **⋯**
  has Show Folder, Archive and Remove from Library. Removing a review from the
  library never deletes its clips or its project file.
- **Add Existing…** lists `.atkproj` projects you already have (choose one or
  several) without opening them; open them from the list as usual.
- **Open Project…** and **Open Clips…** open files directly, as before. A
  project opened this way (or from Recent Projects or Explorer) is added to
  the library, so it is listed next time.

Opening a clip from Explorer (double-click), from Maya or Harmony, or through
the local API goes straight to the player; the library only appears when ATK
Player is started on its own. You can turn it off under **Edit → Preferences**,
and return to it at any time with **File → Review Library** (`Ctrl+Shift+H`).

The library is a small list kept with your ATK Player settings
(`reviews.json` in the application data folder). The reviews themselves are
ordinary `.atkproj` project files next to the clips.

## 2. Making a review from a folder

**New Review** (`Ctrl+Shift+N`, or the button in the library):

1. **Browse…** to the folder that holds the clips. It is searched in the
   background, so a network or OneDrive folder never freezes the window.
   Tick **Include clips in subfolders** if each person has their own folder.
2. Untick any clips you don't want. **Order by name** sorts the way people
   expect (`shot2` before `shot10`); **Order by date modified** puts the
   oldest first. Image sequences (`walk.0001.png`…) appear as one clip.
3. Name the review and click **Create Review**. ATK Player saves
   `<folder>/<name>.atkproj` and opens it.

When you open the review later and new clips have appeared in its folder
(late submissions), ATK Player offers to add them.

## 3. Naming clips after people (Rename from Team List)

Students and staff rarely name files consistently. **File → Rename Clips from
Team List…**, or the rename button in the review header, matches each clip to
a person on a list and renames it.

### The list

Any of these work; ATK Player reads the first sheet:

- **Excel**: `.xlsx`, `.xlsm`, and older `.xls` (Excel 97–2003) workbooks.
- **CSV** or **text** (comma, semicolon or tab separated), as saved by Excel,
  Google Sheets or a student information system.

Class lists exported as reports are fine: title rows, course details and
signature lines are skipped. ATK Player finds the row with headings such as
**ID**, **Name**, **First Name** or **Last Name**, and leaves out lines with
neither a name nor an ID. If it guesses the columns wrongly, choose them in
the window.

### Matching

Each clip is matched to one person, best match first:

| How the clip is named | Match |
|---|---|
| Contains the person's ID (`8834796_walk.mp4`) | High |
| First and last name, any order or separator (`Logel_Jordan`, `jordan-logel`) | High |
| Initial and last name (`JLogel`, `logel_j`) | High |
| Last name only, when nobody else shares it | Medium |
| Last name one letter off (`Smtih`) | **Check** — a suggestion only |

Change any match in the table. If a person has **two clips** (a resubmission,
or a copy you already renamed), the second one says **Second clip for …**;
pick the person there to give that clip the name instead. People with no clip
are listed below the table.

### What gets renamed

- **Only the names shown in ATK Player** (the default). The playlist, the
  Notes PDF and exports use the new names; the files are untouched.
- **The files on disk**, when you choose it. Everything is checked before any
  file is renamed (illegal or duplicate names, names already taken), and
  ATK Player asks you to confirm. Drawings, notes and statuses stay with their
  clips, and the project is saved pointing at the new names.

The **name pattern** builds each name, for example `{last}_{first}` or
`{order:02}_{last}_{first}`. You can use `{order}`, `{id}`, `{first}`,
`{last}`, `{full}`, `{original}` (the current name), `{review}`, `{date}`
and `{col:Heading}` for any other column of the list.

**Put the review in team-list order** sorts the playlist to match the list,
with unmatched clips last.

### Undo and safety

**Undo Last Rename** in the rename window puts every file back. A rename is
recorded before it starts, so if the computer stops part way through, ATK
Player offers to put the original names back the next time the review or the
rename window opens.

On Windows a file cannot be renamed while any program has it open. ATK Player
lets go of its own copy first and waits a few seconds for antivirus, Windows
Search or OneDrive to finish with a file. If another program still has a clip
open, nothing is renamed and you are told which file.

## 4. Playing the review

The **playlist** on the left shows a card for each clip: its number in a
strip down the left edge, a thumbnail, the frame count, and badges for notes,
drawings and status. The loaded clip's number strip is filled with the accent
colour. Drag cards to reorder; `F4` shows or hides the panel.

**Arrange Playlist**: the list button at the bottom right of the playlist (or
**Arrange Playlist…** on a card's right-click menu) opens a window for bulk
reordering. Pick several clips with `Shift` or `Ctrl`, then move them together
with the up, down, top and bottom arrows, or drag them. **Sort by** reorders
the whole list by name (numbers in order, so `shot2` comes before `shot10`),
file date, length, category or review status, or reverses it.

**Categories**: name your own groups, such as class sections or shot types,
with **New…** in the Arrange window, then pick clips and **Set for Selected**.
A clip can have one category or none. Sorting by category groups the clips in
the order the categories were made. The category shows as a tag on the clip's
card, and a card's right-click **Category** menu files one clip quickly.
Categories and the order are saved with the review; nothing changes until you
click **Apply**.

What happens at the end of each clip is set with the arrow beside the
**Loop** button (also in the **Playback** menu):

| Choice | At the end of a clip |
|---|---|
| **Play Through Playlist** (default) | Goes on to the next clip; stops after the last |
| **Stop After Each Clip** | Stops; `Space` plays the next clip |
| **Loop This Clip** | Plays the clip again |
| **Loop Whole Playlist** | Goes on to the next clip; the last goes back to the first |

The **Loop** button itself (`L`) switches between your loop choice and your
non-loop choice, and both choices are remembered.

**Playback speed**: the speed button beside Loop shows the rate the clip
plays at, such as `24fps`, or `12fps` at half speed. Its menu offers 0.1x to
2x; `<` and `>` step slower and faster, and `K` returns to normal speed. Away
from normal speed the clip plays without sound. Frame stepping and scrubbing
are not affected.

**Playing**: the play button is the large one in the middle of the bottom
bar, between the step and skip buttons. `Space` plays and pauses wherever you
are in the player, including just after typing a frame number; only fields
you type text into, such as notes, take the space instead.

**Saving**: the **Save** and **Save As** buttons at the top right, beside
Export, save the review project (`Ctrl+S` / `Ctrl+Shift+S`).

**Current frame**: the large yellow number to the left of the timeline is the
frame on screen. Click it, type a frame number and press `Enter` to go there.
The frame number also sits on top of the playhead at all times, outlined; it
fills in yellow while you drag the playhead.

**In/Out**: the **In/Out** button at the left of the range bar under the
timeline (`Shift+I`) switches between playing only the in/out range and
playing the whole clip. Turning it off keeps the range, so turning it on again
goes straight back to it. Mark a range with `I` and `O`, the range fields, or
by dragging the ends of the range bar.

**Open Folder**: the button beside the clip name at the top (and the folder
icon at the top right) opens the clip's folder in Explorer (Windows) or Finder
(macOS) with the clip selected.

**Zoom**: the zoom button near the right of the bottom bar shows the zoom level
(**Fit**, or a percentage). Its menu zooms in and out, fits the picture, or
picks 25%, 50%, 100%, 200% or 400%. The button beside it fits the picture.

Frame stepping (`Left` / `Right`), review ranges (`I` / `O`), bookmarks (`B`)
and A/B comparison work as described in the README.

## 5. Notes, drawings and status

- **Draw on Frame** (`D`, the palette button at the left of the bottom bar)
  shows the drawing row: pencil, pen, brush, marker,
  custom (imported Photoshop) brushes, line, arrow, rectangle, ellipse, text,
  eraser and select, with colour and size (`[` / `]`). Tracing paper (`T`),
  ghosts (`G`), Hold Drawings, Drag Markers and Flip Canvas (`H`) sit in the
  bottom bar; Flip Canvas is also on the drawing row.
- **Hold Drawings** (`Shift+H`, the pin button): a drawing stays on screen on
  the frames after it until the next drawing, like a held key drawing, and a
  faint bar under the timeline shows how far each one holds. The arrow beside
  the button chooses what drawing on a held frame does: **Add to Current
  Drawing** adds the strokes to the held drawing, on the frame it was drawn
  on; **Add New Drawing** starts a new drawing on the frame on screen, which
  then holds from there. Erasing, moving or clearing a held drawing changes it
  where it was drawn. **Don't Hold Drawings** turns it off. Holding is a view
  setting: exports keep their own "keep on screen" choice.
- **Adjust a drawing** with the select tool: click it to show its handles.
  Drag the square handles to resize it (`Shift` keeps its proportions, `Alt`
  resizes from the centre) and the round knob above it to rotate it (`Shift`
  snaps to 15°). Lines and arrows have a handle on each end; text resizes from
  its corners. Drag the drawing itself to move it, press `Esc` during a drag to
  put it back, and `Ctrl+Z` to undo.
- **Copy, cut and paste between frames** with the copy, cut and paste buttons
  on the drawing row, or `Ctrl+C`, `Ctrl+X` and `Ctrl+V`. With a drawing
  picked by the select tool, they copy or cut just that drawing; with nothing
  picked, everything on the frame, drawings and notes. Go to another frame,
  or another clip, and paste: everything lands in the same place on the
  picture. A single pasted drawing comes up selected, so you can drag it into
  position straight away. To copy one written note, right-click it in the
  Notes panel and choose **Copy Note** or **Cut Note**, then paste.
- **Drag a frame's notes and drawings to another frame** on the timeline: the
  small orange marks under the track show which frames have them. Turn on
  **Drag Markers** (the button with arrows pointing apart; off by default) and
  the marks grow into handles: drag one to another frame to move everything
  there, or hold `Ctrl` while you let go to copy instead. Clicking a mark goes
  to that frame whether Drag Markers is on or off. Each paste, cut or drag is
  one undo step.
- **Move several frames' notes at once**, like keys in Maya's time slider:
  hold `Shift` and drag across the timeline to select every mark in that
  range (it is shaded blue), or `Shift`+click marks to add or remove them one
  at a time. Then drag inside the blue band to slide the whole group along
  the timeline; each frame keeps its own notes and drawings, and the group
  keeps its spacing. Hold `Ctrl` while you let go to copy instead. Click
  outside the band or press `Esc` to clear the selection. This works whether
  Drag Markers is on or off, and the whole move is one undo step.
- The **Notes** panel (`F6`, or **Notes** at the top right) starts closed so
  the picture stays large. Notes can be searched and sorted; `Ctrl+.` and
  `Ctrl+,` jump between frames with notes or drawings.
- Set each clip's **status** (Not reviewed, In review, Needs changes,
  Approved) in the review header. It shows on the playlist card and is saved
  in the project.

## 6. Sending the review

The **Export** button in the review header holds:

- **Export Notes PDF…**: one page per noted frame with its drawing. Switch
  **Include text notes** off for pictures only.
- **Export Notes Video (MP4)…**: the clip with its drawings and notes burned in.
- **Export Notes PDFs for Playlist…**: a Notes PDF for each clip you tick, all
  saved into one folder as `<clip>_review_notes.pdf`, one after another. Clips
  with nothing to list are greyed out. If some of the PDFs are already in the
  folder, choose **Replace** or **Keep Both** (the new ones become
  `name (2).pdf`).
- **Export Review…** and **Export Current Frame…**, described in the README.

The **File** menu has all of these plus **Export Image Sequence…**.

**Set a save location once per review**: in the Export Review window, click
**Set Save Location…** and choose a folder. It is saved in the project, so
every later export of that review, including frames, image sequences and
Notes PDFs, starts in that folder instead of beside the project. Save the
project afterwards to keep it. **Browse…** still picks a different place for
one export, and **Clear** goes back to saving beside the project.

**Keep the burn-in choices for a review**: the Export Review window's
**Drawings, notes and burn-ins** choices (frame number, bookmarks, drawings,
frame notes, play-through, hold or freeze, tracing paper) are remembered for
the review or playlist while **Remember these choices for this review** is
ticked. Every later export of it, including Export Current Frame and Export
Image Sequence, starts from them, and they are saved in the project, so the
review still has them next time it is opened. Opening a different review or a
clip on its own starts from the standard choices again. **Use Defaults** goes
back to the standard choices for one export.

## Keyboard reference

| Action | Shortcut |
|---|---|
| Review Library | `Ctrl+Shift+H` |
| New Review from Folder | `Ctrl+Shift+N` |
| Play / Pause | `Space` |
| Previous / Next Frame | `Left` / `Right` |
| Loop on/off | `L` |
| Set range in / out | `I` / `O` |
| Play in/out range only on/off | `Shift+I` |
| Draw on Frame | `D` |
| Copy / Cut / Paste drawings and notes | `Ctrl+C` / `Ctrl+X` / `Ctrl+V` |
| Brush size | `[` / `]` |
| Tracing Paper / Ghosts | `T` / `G` |
| Hold Drawings on/off | `Shift+H` |
| Flip Canvas | `H` |
| Slower / Faster / Normal speed | `<` / `>` / `K` |
| Zoom in / out / Fit / 100% | `Ctrl+=` / `Ctrl+-` / `Ctrl+0` / `Ctrl+1` |
| Next / Previous note or drawing | `Ctrl+.` / `Ctrl+,` |
| Playlist / Bookmarks / Notes panels | `F4` / `F5` / `F6` |

In the Review Library, `Tab` moves between reviews and their buttons, the
arrow keys switch between All, Starred and Archived, and `Space` or `Enter`
presses the focused button (for example **Open**).
