# Icons

Empty in Phase 0.

The transport controls currently use text glyphs from the system UI font
(`src/ui/TransportBar.cpp`), which keeps the framework free of binary assets and
licence questions while the architecture is being established.

Real artwork arrives with milestone **M2**.

## When icons are added

- **SVG**, rendered at runtime through Qt's SVG support, so the same asset serves
  every scale factor. High-DPI displays are the norm, and shipping a PNG per
  scale is a maintenance cost with no benefit here.
- **Monochrome**, tinted from the palette in `src/ui/Theme.h` rather than
  carrying baked-in colours. The application is themeable in principle and
  hard-coded icon colours are what makes that stop being true.
- Loaded through a Qt resource file so they are compiled into the binary and
  cannot go missing at runtime.
- Every icon's source, author and licence recorded in
  [`../../THIRD_PARTY_LICENSES.md`](../../THIRD_PARTY_LICENSES.md) before it is
  committed — including whether attribution must appear in the About dialog.
