# Icons

The application artwork lives in `ATK_Icon_Pack/` (see its `README.txt`).
`ATK_Player_Icon_1024.png` is the high-resolution master.

- `png/` sizes 16 to 256 are compiled into the UI resources under
  `:/icons/app` and combined by `icons::applicationIcon()` for the window and
  taskbar icon; `:/icons/ATK_Player_Icon.png` is the 256 px image for larger
  in-app logos such as the welcome window.
- `ATK_Player_Icon.ico` (16 to 256) is embedded in the executable and used by
  the MSI, Start Menu shortcut and `.atkproj` association.
- `ATK_Player_Icon.icns` and `macos/` are for the macOS bundle.
- `packaging/windows/assets/make_installer_bitmaps.py` draws the installer
  bitmaps from the 1024 px master.

Interface icons live in `lucide/`: Lucide outline icons (ISC, see
`lucide/LICENSE` and `docs/THIRD_PARTY_LICENSES.md`). They stroke with
`currentColor` and are tinted from the theme by `src/ui/IconProvider`. To add
one, copy its SVG from the same Lucide release into `lucide/`, list it in
`src/ui/atk_resources.qrc`, and use it as `icons::icon(QStringLiteral("name"))`.

David Shepstone designed the application icon with assistance from AI tools and confirmed ownership on September 30, 2026. It is supplied under the repository's MIT License; see [asset provenance](../../docs/THIRD_PARTY_LICENSES.md).

## Asset rules

- **SVG**, rendered at runtime through Qt's SVG support, so the same asset serves
  every scale factor. High-DPI displays are the norm, and shipping a PNG per
  scale is a maintenance cost with no benefit here.
- **Monochrome**, tinted from the palette in `src/ui/Theme.h` rather than
  carrying baked-in colours. The application is themeable in principle and
  hard-coded icon colours are what makes that stop being true.
- Loaded through a Qt resource file so they are compiled into the binary and
  cannot go missing at runtime.
- Every icon's source, author and licence recorded in
  [`../../docs/THIRD_PARTY_LICENSES.md`](../../docs/THIRD_PARTY_LICENSES.md) before it is
  committed — including whether attribution must appear in the About dialog.
