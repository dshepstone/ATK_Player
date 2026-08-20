# ATK Player — Toon Boom Harmony Integration

Drives ATK Player from Toon Boom Harmony.

**Status: not implemented.** Milestone M5.

## Planned design

Harmony's scripting environment is QtScript/JavaScript rather than Python, so
there are two viable routes and the choice depends on the Harmony edition being
targeted:

**Route A — Python client.** Where the Harmony edition can reach a Python
interpreter, reuse [`../python/`](../python/) unchanged. This is preferred: one
client library, one place for protocol bugs to live.

**Route B — script bridge.** A small Harmony script that speaks the API directly.
The protocol is newline-delimited JSON over a socket, chosen partly so that a
bridge like this needs a socket and a JSON encoder and nothing else. Harmony
scripts have access to Qt's networking through the QtScript bindings, so this is
a genuinely small piece of code rather than a parallel implementation.

Route B duplicates a little logic. That is accepted: the alternative is making
the protocol depend on Python being reachable, which would tie the API design to
one integration's constraints.

## Intended workflow

Mirrors the Maya integration: export or render a preview from Harmony, send
`open_media`, sync the current frame and the playback range, and offer a jump-back
so the artist can return Harmony to the frame under review.

## Planned layout

```
harmony/
    scripts/
        ATK_Review.js        # script bridge (route B)
        atk_harmony.py       # Python entry point (route A)
    packages/                # Harmony package definition
    install.md
    README.md
```

## Open questions

- Which Harmony editions and versions to target, since scripting capability
  differs between Essentials, Advanced and Premium.
- Whether Harmony's QtScript socket support is available in every targeted
  edition; if not, route A becomes mandatory and the supported edition list
  shrinks accordingly.
