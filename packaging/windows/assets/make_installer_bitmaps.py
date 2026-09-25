"""Regenerates the WiX UI bitmaps from the ATK logo.

    python packaging/windows/assets/make_installer_bitmaps.py

Requires Pillow. The outputs are committed so packaging does not need Python
imaging tools; rerun this only when the logo or layout changes.

WixUIDialog.bmp (493x312) is the full-size background of the Welcome and
Finish pages; Windows Installer draws text over its right-hand side, so only
the left panel carries artwork. WixUIBanner.bmp (493x58) is the header strip of
the inner pages; its title text sits on the left, so the logo goes right.
"""
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[2]
LOGO = REPO / "assets" / "icons" / "ATK_Player_Icon.png"

PANEL = (0x21, 0x23, 0x26)
TEXT = (0xDA, 0xDD, 0xE1)
ACCENT = (0x4C, 0x9E, 0xE0)
MUTED = (0x8B, 0x92, 0x9B)
FONT_DIRS = [Path("C:/Windows/Fonts"), Path("/usr/share/fonts/truetype/dejavu")]


def font(names, size):
    for directory in FONT_DIRS:
        for name in names:
            candidate = directory / name
            if candidate.exists():
                return ImageFont.truetype(str(candidate), size)
    return ImageFont.load_default()


def logo(size, background):
    mark = Image.open(LOGO).convert("RGBA").resize((size, size), Image.LANCZOS)
    tile = Image.new("RGBA", (size, size), background + (255,))
    tile.alpha_composite(mark)
    return tile.convert("RGB")


def centred(draw, y, text, face, fill, width):
    left, _, right, _ = draw.textbbox((0, 0), text, font=face)
    draw.text(((width - (right - left)) // 2, y), text, font=face, fill=fill)


def dialog():
    image = Image.new("RGB", (493, 312), (255, 255, 255))
    draw = ImageDraw.Draw(image)
    panel_width = 164
    draw.rectangle((0, 0, panel_width - 1, 311), fill=PANEL)
    draw.rectangle((panel_width - 3, 0, panel_width - 1, 311), fill=ACCENT)
    image.paste(logo(96, PANEL), ((panel_width - 96) // 2, 52))
    bold = font(["segoeuib.ttf", "DejaVuSans-Bold.ttf"], 20)
    small = font(["segoeui.ttf", "DejaVuSans.ttf"], 11)
    centred(draw, 162, "ATK Player", bold, TEXT, panel_width)
    centred(draw, 190, "Animation Tool Kit", small, ACCENT, panel_width)
    centred(draw, 205, "Media Player", small, ACCENT, panel_width)
    centred(draw, 282, "shepstone.ca", small, MUTED, panel_width)
    image.save(HERE / "WixUIDialog.bmp")


def banner():
    image = Image.new("RGB", (493, 58), (255, 255, 255))
    image.paste(logo(44, (255, 255, 255)), (493 - 44 - 10, 7))
    image.save(HERE / "WixUIBanner.bmp")


if __name__ == "__main__":
    dialog()
    banner()
