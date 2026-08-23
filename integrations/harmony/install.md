# Installing the Harmony 25 review script on Windows

Validated against Toon Boom Harmony Premium 25.0.0 build 23967 on Windows 11.

Do not copy files into Harmony's packaged application scripts and do not run as
Administrator.

## User Script Editor installation

1. Open Harmony 25 and choose **Windows → Script Editor**.
2. Add or import `scripts/ATK_Review.js` into the **User** script location.
   Harmony's Script Editor user location keeps it editable for your Windows or
   Harmony database user without changing the installation.
3. Refresh the Script Editor if the functions do not appear immediately.
4. Open the Toolbar Manager and assign `ATK_ReviewInPlayer` and
   `ATK_JumpToReviewFrame` to toolbar buttons. Optionally add
   `ATK_TestConnection` and `ATK_Settings`.

## Studio/shared installation

1. Copy `ATK_Review.js` into a shared, read-only-to-artists scripts directory.
2. Set `TOONBOOM_GLOBAL_SCRIPT_LOCATION` to that directory before Harmony
   starts. Harmony exposes this as the Script Editor **System Variable** script
   location.
3. Restart Harmony and assign the same toolbar functions.

## First use

1. Start ATK Player's Release build.
2. Enable **Preferences → Integrations → Local API**.
3. If ATK does not use port `45571`, run `ATK_Settings()` and enter its port.
4. Run `ATK_TestConnection()` before the first review export.
