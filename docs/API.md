# Local External-Control API

ATK Player exposes an optional local automation API for DCC tools and scripts. It is disabled by default and binds only to `127.0.0.1`; it cannot be exposed to the LAN. Enable it under **Preferences → Integrations**. The default port is `45571` (configurable from 1024–65535).

## Transport

The protocol is newline-delimited UTF-8 JSON over TCP. Connections are persistent and may carry multiple requests. Each request and response is one JSON object followed by `\n`.

```json
{"id":1,"command":"get_status","params":{}}
{"id":1,"ok":true,"result":{"currentFrame":0,"displayFrame":1,"frameIndexBase":0}}
```

`id` is echoed unchanged. Invalid JSON, non-object JSON, unknown commands, and invalid parameters receive `ok:false` with a concise string `error`. Requests are capped at 64 KiB; pending output is capped at 256 KiB. Violating clients are disconnected. There is no authentication because the listener is strictly loopback-only; local automation access should still be treated as privileged.

All API frame indices are zero-based. User-facing UI frame labels are one-based. Status exposes both `currentFrame` and `displayFrame`, plus `frameIndexBase:0`. Rational rates and timestamps remain authoritative.

## Commands

- Introspection/playback: `get_api_info`, `list_commands`, `get_status`, `play`, `pause`, `stop`, `seek_frame`, `step_forward`, `step_backward`, `get_current_frame`, `set_loop_enabled`, `set_loop_range`, `clear_loop_range`.
- Review: `add_bookmark`, `list_bookmarks`.
- Project/playlist: `open_media`, `open_project`, `new_project`, `save_project`, `save_project_as`, `add_media`, `list_sources`, `activate_source`.
- Comparison: `set_comparison_enabled`, `load_compare_a`, `load_compare_b`, `set_compare_offset`, `set_compare_view`, `set_compare_audio_mode`, `load_external_audio`, `clear_external_audio`, `set_external_audio_offset`.
- Export/UI: `export_review`, `export_frame`, `export_image_sequence`, `get_export_status`, `cancel_export`, `show_window`.

Use `list_commands` for feature detection and `get_api_info` for protocol version 1, application identity, active port, frame-index base, and request cap.

Navigation is asynchronous. `seek_frame`, `step_forward`, and `step_backward`
return `{"accepted":true,"targetFrame":N}` immediately. `targetFrame` is the
zero-based logical destination, not a claim that it has already painted. Poll
authoritative `get_status.currentFrame`, or use the Python client's
`wait_until_frame()`, to observe completion.

Paths must be absolute local paths; URL schemes are rejected. Input files must exist. Project destinations end in `.atkproj`, review videos in `.mp4`, and current-frame images in `.png`. Image sequences require a non-existing destination directory whose parent exists. Replacing a dirty project is rejected unless `discardUnsaved:true` is explicit. API operations never open file dialogs or save/discard prompts. Playlist identity is the stable UUID from `list_sources`; duplicate paths are valid distinct sources.

Comparison view modes are `side_by_side`, `stacked`, `wipe`, `blend`, and `difference`; audio modes are `a`, `b`, and `external`. Offset requests specify exactly one of `frameOffset` or `offsetUs`, and comparison offsets apply to slot `b`.

`export_review {path, overwrite?}` uses the UI's same immutable `ExportSpec` and asynchronous `ExportJob`, returning a `jobId` immediately. Poll `get_export_status {jobId?}` for progress and terminal state; `cancel_export` is cooperative. Atomic output prevents partial destination files.

`export_frame {path, frame?, overwrite?}` writes one lossless PNG through the
offline renderer. `frame` is an optional zero-based Source A frame; omitting it
uses the authoritative presented frame. `export_image_sequence {directory,
prefix?, startFrame?, endFrame?}` writes an inclusive zero-based Source A range.
Bounds must be supplied together; omitting them uses the active review range.
Sequence filenames use one-based source-visible numbers with at least four
digits, such as `shot_0021.png`. Both commands share `get_export_status` and
`cancel_export`; failed or cancelled sequences never install a partial final
directory.

All three export commands accept an optional `burnIns` object with boolean
`frameNumber`, `bookmarkLabels`, and `bookmarkNotes` fields. Every field defaults
to false, and omitting the object preserves clean export output. Frame-number
text is the one-based Source A frame even in comparison mode. Bookmark burn-ins
use a snapshot of Source A review bookmarks: points apply only on their exact
frame and ranges apply inclusively. The Python wrappers expose matching trailing
`frame_number`, `bookmark_labels`, and `bookmark_notes` keyword arguments.

The standard-library reference client is in [`integrations/python`](../integrations/python/). Maya support in [`integrations/maya`](../integrations/maya/) maps scene frames to zero-based playblast frames rather than changing the protocol.
