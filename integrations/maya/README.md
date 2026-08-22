# ATK Player — Maya Integration

Drives ATK Player from Autodesk Maya, so a playblast can go straight into review
without leaving Maya to find and open the file.

**Status: implemented.** Add both `integrations/python` and `integrations/maya`
to Maya's `PYTHONPATH`, then call `from atk_maya import playblast_to_atk`.

## Design

A thin layer over [`../python/`](../python/). It contains no protocol code of its
own — if the API changes, only the client library changes.

Maya ships its own Python interpreter (`mayapy`), so the client library's
standard-library-only rule is what makes this work without asking animators to
install packages into a Maya installation they may not be able to write to.

## Intended workflow

1. Animator presses the shelf button.
2. The script runs a playblast of the current shot to a temporary location.
3. It sends `open_media` to the already-running ATK Player local API.
4. It sends `seek_frame` with Maya's current frame, so review resumes exactly
   where the animator was working rather than at frame 0.
5. It sets the loop range from Maya's playback range.

The reverse direction matters too: `get_current_frame` lets a "jump Maya to the
frame I am reviewing" button work, which closes the review loop in both
directions.

The adapter maps `ATK index = Maya current frame - playblast start frame`.
For example, Maya 1042 in a 1001-start playblast maps to internal frame 41,
shown as UI frame 42. This mapping is tested without requiring Maya.

## Layout

```
maya/
    atk_maya/
            __init__.py
            playblast.py     # render a playblast, hand it to the player
    tests/
    README.md
```

The helper deliberately does not silently launch ATK Player. Connection failures
remain clear client errors. Run mapping tests with
`python -m unittest discover -s tests -v`.
