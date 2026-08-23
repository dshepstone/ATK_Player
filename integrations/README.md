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

Harmony reaches the same API through a small Qt Script adapter and its
`RemoteCmd` raw-text transport. It contains only preview export, relative-frame
mapping, polling, and toolbar workflow code.

## Status

The generic Python client, Maya playblast adapter, and Harmony 25 review script
are implemented in M5. Real-Harmony acceptance remains pending.

| Directory | Purpose |
|---|---|
| [`python/`](python/) | Reference client library. The other integrations build on it. |
| [`maya/`](maya/) | Autodesk Maya shelf tools and playblast-to-review workflow. |
| [`harmony/`](harmony/) | Toon Boom Harmony script bridge. |
