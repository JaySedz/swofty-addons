# Writes tex/strips.png: the visor strip atlas, 16 rows of 1024 x 64, one per team (rows match kRows in
# native/src/helmet_cam.cpp). These are placeholders: a team colour band with the team's name. Paint your own strip
# art over a row (keep the row's 64 px band; alpha 0 = no strip) and the visor shows it for that team.
# Usage: python tools/make_strips.py
import os

from PIL import Image, ImageDraw, ImageFont

W, H, ROWS = 1024, 64, 16
TEAMS = [
    ('VISOR', (18, 18, 20)),            # 0 generic: any car without a row of its own
    ('FERRARI', (190, 10, 20)),         # 1
    ('MERCEDES', (20, 20, 22)),         # 2
    ('RED BULL', (20, 30, 70)),         # 3
    ('McLAREN', (240, 120, 0)),         # 4
    ('ASTON MARTIN', (0, 90, 70)),      # 5 force_india
    ('ALPINE', (10, 60, 160)),          # 6 renault
    ('WILLIAMS', (0, 50, 120)),         # 7
    ('HAAS', (30, 30, 30)),             # 8
    ('AUDI', (60, 60, 60)),             # 9 sauber
    ('RACING BULLS', (30, 50, 120)),    # 10 toro_rosso
    ('CADILLAC', (15, 15, 15)),         # 11 gm_cadillac
    ('KONNERSPORT', (40, 40, 45)),      # 12
    ('VISOR', (18, 18, 20)),            # 13 spare
    ('VISOR', (18, 18, 20)),            # 14 spare
    ('VISOR', (18, 18, 20)),            # 15 spare
]


def font(size):
    for name in ('arialbd.ttf', 'arial.ttf', 'DejaVuSans-Bold.ttf'):
        try:
            return ImageFont.truetype(name, size)
        except OSError:
            pass
    return ImageFont.load_default()


def main():
    img = Image.new('RGBA', (W, H * ROWS), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    f = font(40)
    for i, (label, colour) in enumerate(TEAMS):
        y = i * H
        # the strip: a band with soft top/bottom edges (the visor's curve hides a hard line)
        d.rectangle([0, y + 6, W - 1, y + H - 7], fill=colour + (255,))
        d.rectangle([0, y + 4, W - 1, y + 5], fill=colour + (140,))
        d.rectangle([0, y + H - 6, W - 1, y + H - 5], fill=colour + (140,))
        if label != 'VISOR':
            tw = d.textlength(label, font=f)
            d.text(((W - tw) / 2, y + (H - 44) / 2), label, font=f, fill=(235, 235, 235, 255))
    out = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'tex', 'strips.png')
    img.save(out)
    print('wrote', os.path.normpath(out))


if __name__ == '__main__':
    main()
