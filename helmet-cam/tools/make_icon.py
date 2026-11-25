# Writes icon.png (256 x 256): a helmet eye port with a tinted visor and a strip across the top.
# Usage: python tools/make_icon.py
import os

from PIL import Image, ImageDraw, ImageFilter

S = 256
img = Image.new('RGBA', (S, S), (0, 0, 0, 0))
d = ImageDraw.Draw(img)
d.rounded_rectangle([8, 8, S - 8, S - 8], radius=48, fill=(22, 22, 26, 255))
# the view through the visor: sky, track, horizon
port = Image.new('RGBA', (S, S), (0, 0, 0, 0))
p = ImageDraw.Draw(port)
p.rectangle([0, 0, S, 140], fill=(120, 150, 185, 255))
p.rectangle([0, 140, S, S], fill=(70, 72, 78, 255))
p.polygon([(128, 138), (40, 230), (216, 230)], fill=(95, 97, 104, 255))
p.line([(128, 138), (40, 230)], fill=(230, 230, 230, 255), width=4)
p.line([(128, 138), (216, 230)], fill=(230, 230, 230, 255), width=4)
mask = Image.new('L', (S, S), 0)
ImageDraw.Draw(mask).rounded_rectangle([30, 70, S - 30, 200], radius=46, fill=255)
mask = mask.filter(ImageFilter.GaussianBlur(3))
img.paste(port, (0, 0), mask)
# tint, strip and brow
tint = Image.new('RGBA', (S, S), (40, 30, 10, 0))
tint.putalpha(mask.point(lambda v: v * 70 // 255))
img = Image.alpha_composite(img, tint)
d = ImageDraw.Draw(img)
d.rectangle([30, 74, S - 30, 92], fill=(190, 10, 20, 255))
d.rectangle([24, 52, S - 24, 72], fill=(22, 22, 26, 255))
img.save(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'icon.png'))
print('wrote icon.png')
