# Icons

`ATK_Player_Icon.png` is the canonical application artwork. The Windows M6
packaging uses the derived `ATK_Player_Icon.ico` for the executable, MSI,
Start Menu shortcut and `.atkproj` association.

The current committed master is 32×32. The ICO deliberately preserves that
real resolution rather than claiming invented high-resolution detail. Replace
both from a genuine multi-resolution master in a future artwork pass.

The transport controls continue to use text glyphs from the system UI font.

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
