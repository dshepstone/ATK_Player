# Integrations

Clients that drive ATK Player from other applications.

## The rule

**No DCC-specific code goes into `ATKPlayer.exe`.**

The application exposes one generic, language-independent API (see
[../docs/API.md](../docs/API.md)). Each integration is a separate small client
that speaks it. This is what keeps adding a third DCC from requiring any change
to the player, and it keeps Maya's and Harmony's very different scripting
environments from leaking into the application's build.

```
  Maya  ──┐
          ├──> integrations/python (client library) ──> JSON over loopback ──> ATK Player
Harmony ──┘                                                                     (API server)
```

Harmony can also reach the API through a small script bridge where its
environment makes the Python client awkward — the protocol is plain
newline-delimited JSON, so a bridge needs a socket and nothing else.

## Status

The generic Python client and Maya playblast adapter are implemented in M5.
Harmony remains future work and can reuse the same protocol.

| Directory | Purpose |
|---|---|
| [`python/`](python/) | Reference client library. The other integrations build on it. |
| [`maya/`](maya/) | Autodesk Maya shelf tools and playblast-to-review workflow. |
| [`harmony/`](harmony/) | Toon Boom Harmony script bridge. |
