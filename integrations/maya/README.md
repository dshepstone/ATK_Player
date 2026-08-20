# ATK Player — Maya Integration

Drives ATK Player from Autodesk Maya, so a playblast can go straight into review
without leaving Maya to find and open the file.

**Status: not implemented.** Milestone M5.

## Planned design

A thin layer over [`../python/`](../python/). It contains no protocol code of its
own — if the API changes, only the client library changes.

Maya ships its own Python interpreter (`mayapy`), so the client library's
standard-library-only rule is what makes this work without asking animators to
install packages into a Maya installation they may not be able to write to.

## Intended workflow

1. Animator presses the shelf button.
2. The script runs a playblast of the current shot to a temporary location.
3. It sends `open_media` to ATK Player, which starts the player if it is not
   already running.
4. It sends `seek_frame` with Maya's current frame, so review resumes exactly
   where the animator was working rather than at frame 0.
5. It sets the loop range from Maya's playback range.

The reverse direction matters too: `get_current_frame` lets a "jump Maya to the
frame I am reviewing" button work, which closes the review loop in both
directions.

## Planned layout

```
maya/
    scripts/
        atk_maya/
            __init__.py
            playblast.py     # render a playblast, hand it to the player
            shelf.py         # shelf button definitions
            sync.py          # frame sync in both directions
    icons/
    install.md               # where to put things in a Maya installation
    README.md
```

## Open questions

- Which Maya versions to support. Maya 2025 and 2027 are both installed on the
  current development machine; the Python version each ships determines the
  floor for the client library.
- Whether to launch ATK Player automatically when it is not running, or to prompt.
  Automatic is convenient, but a silently spawned application that the animator
  did not ask for is worse than a clear message.
