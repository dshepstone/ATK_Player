# ATK Player — Toon Boom Harmony Integration

Toolbar-friendly review scripts for Toon Boom Harmony 25.x on Windows 11.
Harmony exports an OpenH264 `.mov`, opens it in an already-running ATK Player,
sets the exported clip's loop range and relative frame, and can later jump the
Harmony timeline back to the frame under review.

ATK Player must already be running with **Preferences → Integrations → Enable
Local API** selected. The script connects only to `127.0.0.1`; its default port
is `45571` and can be changed with `ATK_Settings()`.

## Toolbar functions

- `ATK_TestConnection()` validates the ATK application, protocol version 1, and
  zero-based frame convention.
- `ATK_TestSequentialRequests()` diagnoses three sequential logical API requests
  without exporting a movie.
- `ATK_ReviewInPlayer()` exports Harmony frames 1 through `frame.numberOf()`.
- `ATK_ReviewRangeInPlayer(start, end)` exports an explicit inclusive,
  one-based Harmony range and is useful from a custom wrapper script.
- `ATK_JumpToReviewFrame()` maps ATK's current relative frame back into Harmony.
- `ATK_Settings()` changes the Local API port stored in Harmony preferences.

Harmony is one-based and ATK's API is zero-based. For a preview beginning at
Harmony frame 45, Harmony frame 51 maps to ATK internal frame 6. ATK therefore
shows UI Frame 7 because its labels describe the exported movie, not the
absolute Harmony scene timeline. Jump-back reverses that mapping.

The movie is rendered with `exporter.exportToQuicktime`, `openH264`, scene
preview resolution, and sound enabled. This positional exporter was selected
after real Harmony 25 validation showed its object-form `exportMovie` did not
honor a non-1 start frame. Output goes under
`specialFolders.temp/ATK_Player/Harmony`. A timestamped successor is loaded
before the prior session preview is removed, avoiding deletion of media still
open in ATK.

See [install.md](install.md) for installation and toolbar setup.

## Troubleshooting and validation status

- Connection errors mean ATK is closed, its Local API is disabled, or the port
  differs. The script never launches ATK.
- The first OpenH264 export may require Harmony's normal Cisco codec download.
- Harmony 25's `RemoteCmd.send()` appends NUL to raw commands. The adapter adds
  the LF required by ATK's NDJSON protocol and uses a fresh connection for each
  request so that NUL cannot contaminate the next request. It deliberately
  avoids framed `sendMsg()`.
- No Python helper, Node.js, npm package, or Harmony plug-in is required.
