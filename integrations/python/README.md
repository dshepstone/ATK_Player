# ATK Player — Python Client

The reference client library for the ATK Player API. The Maya and Harmony
integrations are built on top of it rather than reimplementing the protocol.

**Status: not implemented.** Milestone M5, after `ApiServer` gains its transport.
See [../../docs/API.md](../../docs/API.md) for the protocol this will speak.

## Planned design

**Standard library only.** The client is a socket and `json` — no third-party
dependencies. This matters because it has to import inside Maya's bundled Python
and inside Harmony's scripting environment, where installing packages ranges from
awkward to impossible.

**Python 3.7+**, matching the oldest interpreter shipped by the DCC versions
worth supporting.

**One class, thin methods.** Each API command maps to one method. The library
does not cache state or try to model the player — it sends a command and returns
the result. Anything cleverer would drift out of sync with the application.

**Errors raise.** A response with `"ok": false` raises `AtkPlayerError` carrying
the server's message, so a failed call cannot be mistaken for a successful one by
a caller that forgot to check a return value.

## Intended usage

```python
from atk_player import AtkPlayer, AtkPlayerError

try:
    player = AtkPlayer()                    # 127.0.0.1:45571
    player.open_media(r"C:\shots\sh040_v012.mov")
    player.set_loop_range(12, 96)
    player.set_loop_enabled(True)
    player.play()
except AtkPlayerError as error:
    print(f"ATK Player refused the command: {error}")
except ConnectionError:
    print("ATK Player is not running, or its API is disabled.")
```

Feature detection, so a client works against an older player:

```python
if "load_compare_b" in player.list_commands():
    player.load_compare_b(previous_take, frame_offset=-6)
```

## Planned layout

```
python/
    atk_player/
        __init__.py
        client.py        # AtkPlayer: connection, request/response framing
        errors.py        # AtkPlayerError and friends
    tests/
        test_client.py   # against a stub server, no running application needed
    README.md
    pyproject.toml
```
