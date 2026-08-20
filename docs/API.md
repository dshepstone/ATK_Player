# External Control API

ATK Player exposes a local API so other applications — Maya, Harmony, a render
farm script, a shell one-liner — can drive it directly.

> **Status:** the command dispatcher is **implemented and unit-tested**. The
> network transport is **not implemented**; `ApiServer::start()` logs and returns
> `false`. The transport lands in milestone M5. Commands can be exercised today
> through `ApiServer::handleRequest()` in-process, which is how the tests do it.

---

## Why an API at all

The animation review loop is: render a playblast, watch it, note what is wrong,
fix it, repeat. Without an API, every iteration means leaving the DCC, finding
the file, opening it, and scrubbing back to where you were. With one, the DCC
can load the new take and jump to the frame you were looking at.

The design consequence is that the API must be able to do **everything the UI can
do**. That is why commands are dispatched to `PlaybackController` and
`TimelineModel` — the same objects the buttons drive — rather than to a separate
"automation" path that would inevitably drift.

---

## Design rules

**Loopback only.** The server binds to `127.0.0.1` and `::1`, never
`QHostAddress::Any`. A review player must not become a network service on a
studio LAN because someone left a checkbox on.

**Off by default.** The API is enabled explicitly in preferences.

**Language independent.** Newline-delimited JSON over TCP: one JSON object per
line in each direction. Any language with a socket and a JSON parser can speak
it — no generated stubs, no RPC framework, no version-locked client.

**Nothing in a request selects code to run.** Paths in requests are opened as
media and nothing else.

**Requests are size-capped** so a malformed client cannot exhaust memory.

**Commands execute on the UI thread** via a queued connection, because they
mutate models the UI observes.

---

## Protocol

Default port **45571**, configurable. Both directions are newline-delimited JSON.

### Request

```json
{ "id": 1, "command": "seek_frame", "params": { "frame": 42 } }
```

| Field | Required | Meaning |
|---|---|---|
| `command` | yes | The command name. |
| `params` | no | Command arguments. Omit when the command takes none. |
| `id` | no | Echoed back in the response, so a client can match replies to requests when pipelining. |

### Response

Success:

```json
{ "id": 1, "ok": true, "result": { "currentFrame": 42 } }
```

Failure:

```json
{ "id": 1, "ok": false, "error": "parameter frame must be a number" }
```

`ok` is always present. Exactly one of `result` or `error` follows it. `id` is
present only if the request carried one.

A command that exists in this build but is not implemented yet fails with an
error containing **"not implemented"**, distinct from **"unknown command"** for
one that does not exist. Clients can use that difference to feature-detect
rather than guessing from the version number.

---

## Command reference

### Introspection

#### `list_commands`

Returns the commands this build understands. Call it first to feature-detect.

```json
{ "command": "list_commands" }
```

```json
{ "ok": true, "result": { "commands": ["list_commands", "get_status", "..."] } }
```

#### `get_status`

The player's full state in one call.

```json
{ "command": "get_status" }
```

```json
{
  "ok": true,
  "result": {
    "state": "paused",
    "currentFrame": 42,
    "frameCount": 240,
    "fps": 24.0,
    "loop": false,
    "hasMedia": true,
    "placeholder": false
  }
}
```

`state` is `"playing"`, `"paused"` or `"stopped"`.

`placeholder` is `true` while the frame numbers describe the Phase 0 placeholder
extent rather than an open file. A client must not treat `currentFrame` or
`frameCount` as referring to real media while it is set — check it, or check
`hasMedia`, before acting on the numbers.

---

### Media

#### `open_media` — *not implemented (M1)*

Opens a file in the main viewer.

```json
{ "command": "open_media", "params": { "path": "C:/shots/sh040_v012.mov" } }
```

| Parameter | Type | Required |
|---|---|---|
| `path` | string | yes — absolute path |

#### `open_project` — *not implemented (M3)*

Opens an `.atkproj` review session.

```json
{ "command": "open_project", "params": { "path": "C:/reviews/sh040.atkproj" } }
```

| Parameter | Type | Required |
|---|---|---|
| `path` | string | yes — absolute path |

---

### Transport

#### `play`, `pause`, `stop`

No parameters. `stop` returns the playhead to the start of the active range.

```json
{ "command": "play" }
```

#### `seek_frame`

Moves the playhead. The frame is **clamped** to the media extent and to the
active in/out range — the API cannot put the playhead somewhere the UI could not.

```json
{ "command": "seek_frame", "params": { "frame": 42 } }
```

| Parameter | Type | Required |
|---|---|---|
| `frame` | integer | yes |

Returns `{ "currentFrame": <clamped frame> }`. Compare it against what you asked
for to detect clamping.

#### `step_forward`, `step_backward`

Move one frame. Both leave play mode if the player was playing, because stepping
is a deliberate single-frame move. Return `{ "currentFrame": … }`.

#### `get_current_frame`

```json
{ "ok": true, "result": { "currentFrame": 42, "frameCount": 240 } }
```

---

### Range and looping

#### `set_loop_enabled`

| Parameter | Type | Required |
|---|---|---|
| `enabled` | boolean | yes |

#### `set_loop_range`

Sets the in/out range and enables it.

```json
{ "command": "set_loop_range", "params": { "start": 12, "end": 96 } }
```

| Parameter | Type | Required |
|---|---|---|
| `start` | integer | yes |
| `end` | integer | yes |

A reversed range is normalised rather than rejected.

#### `clear_loop_range`

Disables the in/out range so playback covers the whole extent again. No
parameters.

---

### Annotation

#### `add_bookmark`

```json
{
  "command": "add_bookmark",
  "params": { "frame": 24, "name": "contact", "note": "foot slides", "color": 3 }
}
```

| Parameter | Type | Required | Notes |
|---|---|---|---|
| `frame` | integer | no | Defaults to the current frame — which is what a DCC hotkey binding wants. |
| `name` | string | no | |
| `note` | string | no | |
| `color` | integer | no | Palette index 0–7. Omit for none. |

One bookmark per frame: adding a second on the same frame replaces the first.
Returns `{ "frame": <frame> }`.

---

### A/B comparison

#### `load_compare_a`, `load_compare_b` — *not implemented (M4)*

Load a source into viewer A or B.

```json
{ "command": "load_compare_b", "params": { "path": "C:/shots/sh040_v011.mov", "frameOffset": -6 } }
```

| Parameter | Type | Required |
|---|---|---|
| `path` | string | yes |
| `frameOffset` | integer | no — frames added to the master frame for this source |

Both viewers run off one master clock; the offset shifts only which frame this
source contributes. See [ARCHITECTURE.md](ARCHITECTURE.md).

#### `set_compare_offset` — *not implemented (M4)*

Adjusts a comparison source's frame offset without reloading it, so a reviewer
can nudge two takes into alignment live.

```json
{ "command": "set_compare_offset", "params": { "slot": "b", "frameOffset": -6 } }
```

| Parameter | Type | Required |
|---|---|---|
| `slot` | string | yes — `"a"` or `"b"` |
| `frameOffset` | integer | yes |

---

## Client libraries

### Python — `integrations/python/` *(milestone M5)*

The reference client. Maya uses it directly, and Harmony uses it where its
scripting environment allows.

```python
from atk_player import AtkPlayer

player = AtkPlayer()              # connects to 127.0.0.1:45571
player.open_media(r"C:\shots\sh040_v012.mov")
player.set_loop_range(12, 96)
player.set_loop_enabled(True)
player.play()
```

### Maya — `integrations/maya/` *(milestone M5)*

A shelf button that renders a playblast and sends it to ATK Player, preserving
the current frame so review picks up where it left off.

### Harmony — `integrations/harmony/` *(milestone M5)*

Either the Python client where the scripting environment supports it, or a small
script bridge that speaks the same JSON.

**None of these put DCC-specific code in `ATKPlayer.exe`.** The application
exposes one generic API; each integration is a separate small client. That is
what keeps adding a third DCC from touching the player at all.

---

## Testing without a socket

`ApiCommandDispatcher` is a pure request-in / response-out object. Once the
transport exists, the same dispatcher serves it — so protocol behaviour is
already covered by `tests/tst_apicommands.cpp`, which drives it directly:

```cpp
TimelineModel timeline;
PlaybackController playback(&timeline);
ApiCommandDispatcher dispatcher(&playback, &timeline);

QJsonObject request;
request.insert("command", "seek_frame");
request.insert("params", QJsonObject{ { "frame", 42 } });

const ApiResponse response = dispatcher.dispatch(request);
```
