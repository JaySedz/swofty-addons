# Draws icon-generated.png (256x256; the shipped icon.png is hand-made, rename this to replace it): a racing helmet in side profile (facing right) with a visor and team stripe,
# and an "AI" badge. Run: python tools\make_icon.py   (needs Pillow). Drawn at 4x and scaled down.
import os
from PIL import Image, ImageDraw, ImageFont, ImageChops

S = 4
W = 256 * S


def p(*xy):
    return [v * S for v in xy]


def layer():
    return Image.new("L", (W, W), 0)


img = Image.new("RGBA", (W, W), (0, 0, 0, 0))
d = ImageDraw.Draw(img)
d.rounded_rectangle(p(8, 8, 248, 248), radius=48 * S, fill=(24, 30, 44, 255))
d.rounded_rectangle(p(8, 8, 248, 248), radius=48 * S, outline=(56, 66, 92, 255), width=3 * S)

# shell: a wide dome, its lower edge sloping up from the chin (front, right) to the nape (back, left)
shell = layer()
sd = ImageDraw.Draw(shell)
sd.ellipse(p(34, 42, 222, 222), fill=255)
cut = layer()
ImageDraw.Draw(cut).polygon(p(0, 186, 0, 256, 256, 256, 256, 206, 214, 206, 120, 196), fill=255)
shell = ImageChops.subtract(shell, cut)
# chin bar reaching forward
ImageDraw.Draw(shell).polygon(p(150, 170, 222, 160, 230, 182, 214, 206, 150, 200), fill=255)


def paint(mask, colour):
    img.paste(Image.new("RGBA", (W, W), colour), (0, 0), mask)


paint(shell, (234, 238, 246, 255))
# team stripe over the crown, clipped to the shell
stripe = layer()
ImageDraw.Draw(stripe).polygon(p(20, 128, 236, 64, 236, 88, 20, 152), fill=255)
paint(ImageChops.multiply(stripe, shell), (255, 128, 0, 255))
# visor: the front window, clipped to the shell
visor = layer()
ImageDraw.Draw(visor).rounded_rectangle(p(128, 110, 260, 156), radius=16 * S, fill=255)
paint(ImageChops.multiply(visor, shell), (16, 20, 32, 255))
d.rounded_rectangle(p(146, 118, 200, 126), radius=4 * S, fill=(90, 160, 255, 255))
# a vent line along the lower shell
d.line(p(64, 176, 140, 182), fill=(170, 178, 196, 255), width=4 * S)

# "AI" badge
d.ellipse(p(20, 156, 96, 232), fill=(124, 92, 255, 255), outline=(24, 30, 44, 255), width=6 * S)
font = None
for f in ("segoeuib.ttf", "arialbd.ttf"):
    try:
        font = ImageFont.truetype(os.path.join(os.environ.get("WINDIR", "C:\\Windows"), "Fonts", f), 36 * S)
        break
    except OSError:
        pass
d.text(p(58, 193), "AI", font=font, fill=(255, 255, 255, 255), anchor="mm")

out = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "icon-generated.png")
img.resize((256, 256), Image.LANCZOS).save(out)
print("wrote", out)
