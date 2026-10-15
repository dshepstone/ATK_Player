---
name: gumroad-update
description: Update David Shepstone's Gumroad store for a new ATK Player release — the ATK Player product description and the custom store home page (shepstone.gumroad.com). Use after a GitHub release is published, or whenever the user asks to update Gumroad, the Gumroad home page, or Gumroad screenshots.
---

# Gumroad update

The store is https://shepstone.gumroad.com. Two things carry release text:

| What | Where it lives | How it's published |
|---|---|---|
| ATK Player product page (`/l/atk-player`) | Product description (HTML) | Gumroad API, `scripts/update-product.ps1` |
| Store home page | Custom HTML page, slug `profile` | Gumroad CLI, `gumroad pages push profile <file>` |

The user runs these on Windows, in the VS Code terminal (Windows PowerShell 5.1),
from a working folder outside any repo (e.g. `Downloads\gumroad update`). A cloud
session can't reach gumroad.com, so there: write the files, hand them over, and
give the user the exact commands. Never ask the user to paste a token into chat.

## Before you start

1. The GitHub release must be out: `https://github.com/dshepstone/ATK_Player/releases/tag/vX.Y.Z`.
   Take the "What's new" points from `packaging/windows/github-release-X.Y.Z.md`
   and `docs/RELEASE_NOTES_X.Y.Z.md`; keep Gumroad wording short and plain.
2. One-time setup on the user's machine:
   - Gumroad CLI (https://github.com/antiwork/gumroad-cli/releases). It installs to
     `%LOCALAPPDATA%\Programs\gumroad\gumroad.exe`; call it as `gumroad`.
   - `gumroad auth login` (approve in the browser).
   - For the product script: an API token from Gumroad → Settings → Advanced →
     Applications → create app (redirect URI `http://localhost`) → Generate access
     token, set as `$env:GUMROAD_TOKEN` in that terminal only.
   - Scripts downloaded or copied in are blocked by Windows: run them as
     `powershell -ExecutionPolicy Bypass -File <script>` (and `Unblock-File` if needed).

## 1. Product description

1. Write the new description as an HTML fragment (`h2`/`h3`/`p`/`ul`/`a` only),
   e.g. `atk-player-description.html`. It replaces the whole description, so read
   the current one first (the dry run saves it) and keep anything the user wrote
   by hand: price notes, personal messages, links.
2. Dry run, then apply:
   ```powershell
   powershell -ExecutionPolicy Bypass -File <skill>\scripts\update-product.ps1 -DescriptionFile .\atk-player-description.html
   powershell -ExecutionPolicy Bypass -File <skill>\scripts\update-product.ps1 -DescriptionFile .\atk-player-description.html -Apply
   ```
   The dry run backs up the live description to `gumroad-description-backup-<time>.html`.
   `-Apply` sends only `description` (UTF-8 encoded) and reads it back.
3. The installer file on the product's **Content** tab is not touched by this; if
   buyers download from Gumroad, the user uploads the new ZIP there by hand.

## 2. Store home page

1. Get the current page (never rebuild it from a screenshot):
   ```powershell
   powershell -ExecutionPolicy Bypass -File <skill>\scripts\get-home.ps1
   ```
   This saves `home.html`: the seller-authored part of
   `https://shepstone.gumroad.com/landing/embed`, with the scripts Gumroad injects
   stripped off. (The plain store URL only returns Gumroad's iframe wrapper.)
2. Edit `home.html`: version in the screenshot caption, the `New · vX.Y.Z` badge,
   the ATK Player paragraph, and the "New in X.Y.Z" checklist. Leave the rest.
3. **Images must be hosted by Gumroad.** The page runs in a sandbox that only loads
   images from `public-files.gumroad.com`; any other host shows a blank box even
   though the link opens fine in a browser tab. Run:
   ```powershell
   powershell -ExecutionPolicy Bypass -File <skill>\scripts\use-gumroad-images.ps1
   ```
   It uploads every non-Gumroad `<img src>` in `home.html` with
   `gumroad media upload` and rewrites the links.
4. Preview, then publish:
   ```powershell
   powershell -ExecutionPolicy Bypass -File <skill>\scripts\update-home.ps1
   powershell -ExecutionPolicy Bypass -File <skill>\scripts\update-home.ps1 -Apply
   ```
   The dry run backs up the live page, runs `gumroad pages preview`, and
   `gumroad pages push profile home.html --dry-run`. `-Apply` refuses to publish
   while any image is off Gumroad, then pushes and lists the live image links.
   `gumroad pages list` showing "No pages yet" is normal: the home page is the
   `profile` slug, not a listed page.

To roll back the home page: `gumroad pages push profile .\<backup>.html` (strip
it with `get-home.ps1 -Source <backup>` first if Gumroad rejects it), or
"Remove custom page" in the Gumroad dashboard to restore the default template.

## Also update for a release

- shepstone.ca: `src/content/software/atk-player.md` in `dshepstone/shepstone`
  (download/release URLs, version, "What's new"), merged by PR; it deploys on merge.
