"""Draw five character-inspired taskbar stencils. Requires Pillow for development only.

All internal highlights are transparent cutouts, so the IME's monochrome
conversion preserves the features in both light and dark Windows themes.
"""
from pathlib import Path
import math
from PIL import Image, ImageDraw, ImageFont
from generate_icons import SIZES

ROOT = Path(__file__).resolve().parents[1]
DEST = ROOT / "ico"
CHARACTERS = (
    ("09-captain-america", "美国队长"),
    ("10-hulk", "绿巨人"),
    ("11-spider-man", "蜘蛛侠"),
    ("12-ant-man", "蚁人"),
    ("13-rocket-raccoon", "火箭浣熊"),
)


def render(name, size):
    resolution = size * 8
    mask = Image.new("L", (resolution, resolution))
    draw = ImageDraw.Draw(mask)
    unit = resolution / 64

    def points(values):
        return [(round(x * unit), round(y * unit)) for x, y in values]

    def polygon(values, fill=255):
        draw.polygon(points(values), fill=fill)

    def ellipse(box, fill=255):
        draw.ellipse(tuple(round(x * unit) for x in box), fill=fill)

    def line(values, width=2, fill=0):
        draw.line(points(values), fill=fill, width=max(1, round(width * unit)), joint="curve")

    if name == "09-captain-america":
        # Shield rings and a five-point star, separated by transparent rings.
        ellipse((4, 4, 60, 60))
        ellipse((9, 9, 55, 55), 0)
        ellipse((13, 13, 51, 51))
        ellipse((18, 18, 46, 46), 0)
        star = []
        for i in range(10):
            angle = -math.pi / 2 + i * math.pi / 5
            radius = 12.5 if i % 2 == 0 else 5.5
            star.append((32 + radius * math.cos(angle), 32 + radius * math.sin(angle)))
        polygon(star)
    elif name == "10-hulk":
        # Squared jaw, broken hairline, low brow and gritted teeth.
        polygon([(11, 9), (19, 4), (28, 7), (35, 4), (45, 6), (53, 12),
                 (53, 39), (47, 51), (39, 59), (25, 59), (17, 51), (11, 39)])
        polygon([(17, 20), (20, 13), (25, 18), (30, 13), (35, 18), (41, 13), (47, 20), (47, 24), (17, 24)], 0)
        polygon([(16, 28), (28, 32), (27, 36), (18, 34)], 0)
        polygon([(48, 28), (36, 32), (37, 36), (46, 34)], 0)
        polygon([(30, 35), (34, 35), (36, 42), (28, 42)], 0)
        polygon([(21, 46), (43, 46), (41, 52), (23, 52)], 0)
        if size >= 32:
            for x in (27, 32, 37):
                line([(x, 46), (x, 51)], 1.5, 255)
    elif name == "11-spider-man":
        ellipse((11, 4, 53, 60))
        # Web cuts are omitted at tiny sizes, where the eyes carry the identity.
        if size >= 32:
            line([(32, 5), (32, 20)], 1.6)
            line([(18, 11), (25, 20)], 1.6)
            line([(46, 11), (39, 20)], 1.6)
            line([(14, 16), (23, 14), (32, 16), (41, 14), (50, 16)], 1.6)
            line([(20, 48), (25, 44), (32, 46), (39, 44), (44, 48)], 1.6)
            line([(32, 46), (32, 59)], 1.6)
        polygon([(16, 22), (23, 25), (29, 32), (27, 40), (21, 38), (17, 32)], 0)
        polygon([(48, 22), (41, 25), (35, 32), (37, 40), (43, 38), (47, 32)], 0)
    elif name == "12-ant-man":
        # Antennae, round helmet, angular visor and a twin-filter respirator.
        line([(24, 18), (21, 5), (16, 3)], 3.5, 255)
        line([(40, 18), (43, 5), (48, 3)], 3.5, 255)
        ellipse((13, 12, 51, 59))
        polygon([(8, 27), (16, 23), (16, 44), (9, 40)])
        polygon([(56, 27), (48, 23), (48, 44), (55, 40)])
        line([(32, 15), (32, 25)], 3)
        polygon([(18, 27), (28, 29), (28, 35), (20, 34)], 0)
        polygon([(46, 27), (36, 29), (36, 35), (44, 34)], 0)
        polygon([(25, 40), (39, 40), (43, 49), (37, 55), (27, 55), (21, 49)], 0)
        polygon([(29, 43), (35, 43), (36, 52), (28, 52)])
        ellipse((12, 41, 23, 52)); ellipse((41, 41, 52, 52))
        ellipse((15, 44, 20, 49), 0); ellipse((44, 44, 49, 49), 0)
    elif name == "13-rocket-raccoon":
        # Pointed ears, dark eye-band, spiky cheeks and a pale muzzle.
        polygon([(8, 4), (24, 13), (40, 13), (56, 4), (53, 30),
                 (60, 37), (53, 38), (59, 46), (48, 46), (44, 53),
                 (32, 60), (20, 53), (16, 46), (5, 46), (11, 38), (4, 37), (11, 30)])
        polygon([(13, 11), (21, 17), (14, 24)], 0)
        polygon([(51, 11), (43, 17), (50, 24)], 0)
        polygon([(25, 19), (39, 19), (34, 32), (30, 32)], 0)
        polygon([(13, 28), (25, 30), (23, 35), (16, 34)], 0)
        polygon([(51, 28), (39, 30), (41, 35), (48, 34)], 0)
        polygon([(19, 42), (32, 37), (45, 42), (41, 51), (32, 56), (23, 51)], 0)
        polygon([(27, 41), (37, 41), (32, 46)])
        line([(32, 46), (32, 50)], 2.5, 255)
        line([(26, 50), (32, 52), (38, 50)], 2.5, 255)
    else:
        raise ValueError(name)
    mask = mask.resize((size, size), Image.Resampling.LANCZOS)
    image = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    image.putalpha(mask)
    return image


def preview():
    canvas = Image.new("RGB", (1000, 430), "#f4f5f7")
    draw = ImageDraw.Draw(canvas)
    font_path = Path("C:/Windows/Fonts/msyh.ttc")
    font = ImageFont.truetype(str(font_path), 21) if font_path.exists() else ImageFont.load_default()
    small = ImageFont.truetype(str(font_path), 14) if font_path.exists() else ImageFont.load_default()
    draw.text((30, 20), "角色主题 · 任务栏单色图标", fill="#20262e", font=font)
    draw.rounded_rectangle((20, 235, 980, 410), radius=14, fill="#242a34")
    for i, (name, label) in enumerate(CHARACTERS):
        x = i * 194 + 112
        large = render(name, 88)
        canvas.paste(large, (x - 44, 72), large)
        draw.text((x, 179), label, anchor="mm", fill="#252b32", font=font)
        for offset, size in ((-35, 16), (0, 24), (39, 32)):
            icon = render(name, size)
            white = Image.new("RGBA", icon.size, "white")
            white.putalpha(icon.getchannel("A"))
            canvas.paste(white, (x + offset - size // 2, 270 - size // 2), white)
            draw.text((x + offset, 307), str(size), anchor="mm", fill="#bec7d3", font=small)
        # Match the actual binary mask used by the IME on a light taskbar.
        actual = render(name, 16)
        actual.putalpha(actual.getchannel("A").point(lambda a: 255 if a >= 128 else 0))
        draw.rounded_rectangle((x - 32, 336, x + 32, 371), radius=7, fill="#f4f5f7")
        canvas.paste(actual, (x - 8, 346), actual)
        draw.text((x, 382), "16 px 实际轮廓", anchor="mm", fill="#bec7d3", font=small)
    canvas.save(DEST / "avengers-preview.png")


if __name__ == "__main__":
    DEST.mkdir(exist_ok=True)
    for name, _ in CHARACTERS:
        frames = [render(name, size) for size in SIZES]
        frames[-1].save(DEST / f"{name}.ico", format="ICO", sizes=[(s, s) for s in SIZES],
                        append_images=frames[:-1], bitmap_format="bmp")
        print(f"{name}.ico")
    preview()
