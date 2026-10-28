"""Generate the project's original monochrome ICO silhouettes (requires Pillow).

Development tool only; loading the checked-in icons needs no Python dependency.
"""
from pathlib import Path
from PIL import Image, ImageDraw

DEST = Path(__file__).resolve().parents[1] / "ico"
SIZES = (16, 20, 24, 32, 40, 48)


def render(name, size):
    scale = 4
    image = Image.new("RGBA", (size * scale, size * scale))
    draw = ImageDraw.Draw(image)
    unit = size * scale / 64

    def line(points, width=5):
        draw.line([(round(x * unit), round(y * unit)) for x, y in points],
                  fill="black", width=max(1, round(width * unit)), joint="curve")

    def polygon(points):
        draw.polygon([(round(x * unit), round(y * unit)) for x, y in points], fill="black")

    if name == "01-nib":
        polygon([(32, 5), (49, 33), (41, 51), (23, 51), (15, 33)])
        draw.line([(32 * unit, 21 * unit), (32 * unit, 50 * unit)], fill=(0, 0, 0, 0), width=round(4 * unit))
        line([(22, 58), (42, 58)])
    elif name == "02-mountain":
        line([(5, 51), (23, 17), (36, 39), (45, 26), (59, 51), (5, 51)], 5)
        line([(18, 27), (23, 31), (28, 26)], 3)
    elif name == "03-leaf":
        polygon([(12, 51), (10, 32), (22, 14), (53, 8), (51, 39), (35, 51)])
        draw.line([(18 * unit, 44 * unit), (43 * unit, 20 * unit)], fill=(0, 0, 0, 0), width=round(4 * unit))
        line([(9, 57), (22, 43)], 4)
    elif name == "04-spark":
        polygon([(32, 5), (39, 25), (59, 32), (39, 39), (32, 59), (25, 39), (5, 32), (25, 25)])
    elif name == "05-book":
        line([(32, 55), (10, 48), (10, 10), (32, 17), (54, 10), (54, 48), (32, 55), (32, 17)], 4)
        line([(17, 22), (25, 25)], 3)
        line([(39, 25), (47, 22)], 3)
    elif name == "06-wave":
        for y in (18, 32, 46):
            line([(6, y + 4), (18, y - 3), (30, y + 4), (42, y - 3), (57, y + 4)], 5)
    elif name == "07-diamond":
        line([(32, 5), (58, 32), (32, 59), (6, 32), (32, 5)], 5)
        polygon([(32, 21), (43, 32), (32, 43), (21, 32)])
    elif name == "08-bamboo":
        line([(29, 7), (29, 57)], 5)
        for y in (21, 39):
            line([(23, y), (35, y)], 4)
        polygon([(32, 24), (52, 10), (46, 29)])
        polygon([(27, 40), (9, 27), (13, 44)])
    else:
        raise ValueError(name)
    return image.resize((size, size), Image.Resampling.LANCZOS)


if __name__ == "__main__":
    DEST.mkdir(exist_ok=True)
    for name in ("01-nib", "02-mountain", "03-leaf", "04-spark", "05-book", "06-wave", "07-diamond", "08-bamboo"):
        frames = [render(name, size) for size in SIZES]
        frames[-1].save(DEST / f"{name}.ico", format="ICO", sizes=[(s, s) for s in SIZES],
                        append_images=frames[:-1], bitmap_format="bmp")
        print(f"{name}.ico")
