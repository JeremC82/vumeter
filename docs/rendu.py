"""Dessine docs/rendu.png : apercu des 6 modes d'affichage sur une dalle 128x64 (LED simulees).

Reprend la geometrie et les couleurs de vumeter.ino ; les niveaux sont inventes (spectre type musique).
"""
import colorsys
import math
import os
import random

from PIL import Image, ImageDraw, ImageFont

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'rendu.png')
W, H = 128, 64
random.seed(7)

# ---------------------------------------------------------------- donnees inventees
def spectrum(shift):
    out = []
    for b in range(64):
        f = b / 63
        v = 0.92 - 0.45 * f + 0.12 * math.sin(b * 0.55 + shift) + random.uniform(-0.12, 0.08)
        if b < 4: v += 0.06
        out.append(max(0.05, min(1.0, v + 0.06)))
    return out


SPEC = [spectrum(0), spectrum(1.7)]
PEAK = [[min(0.99, v + random.uniform(0.03, 0.12)) for v in s] for s in SPEC]
for b in range(0, 64, 5): PEAK[1][b] = PEAK[0][b]  # quelques cretes egales (jaune)
RMS = [0.80, 0.70]

# ---------------------------------------------------------------- outils pixels
def hsv(h, s, v):
    r, g, b = colorsys.hsv_to_rgb((h % 360) / 360, s, v)
    return (int(r * 255), int(g * 255), int(b * 255))


def dim(c, sh=4):
    return (c[0] >> sh, c[1] >> sh, c[2] >> sh)


def zone(f):
    return (255, 30, 20) if f > 0.88 else (255, 190, 0) if f > 0.72 else (20, 220, 40)


WHITE = (255, 255, 255)
GLYPH = {  # police 5x7 minimale (comme la police Adafruit GFX)
    'L': ['10000'] * 6 + ['11111'],
    'R': ['11110', '10001', '10001', '11110', '10100', '10010', '10001'],
    'V': ['10001', '10001', '10001', '10001', '10001', '01010', '00100'],
    'U': ['10001'] * 6 + ['01110'],
}


def text(px, x, y, s, col):
    for ch in s:
        for r, row in enumerate(GLYPH[ch]):
            for c, bit in enumerate(row):
                if bit == '1' and 0 <= x + c < px.width and 0 <= y + r < px.height:
                    px.putpixel((x + c, y + r), col)
        x += 6


# ---------------------------------------------------------------- les modes (geometrie de vumeter.ino)
def bars(stereo=False, center=False, gap=1, dual=False):
    """Moteur de barres de vumeter.ino : fond noir, cretes blanches (ou rouge / blanc / jaune en double)."""
    im = Image.new('RGB', (W, H)); d = ImageDraw.Draw(im)
    bw = 3 if gap else max(1, W // 64)
    pitch = bw + gap
    lst = []
    if not stereo:
        n = W // pitch
        for i in range(n):
            lst.append((i * pitch, 2, i * 64 // n, (i + 1) * 64 // n, i * 300 / n))
    else:
        half, n = W // 2, (W // 2) // pitch
        for i in range(n):
            b0, b1, hue = i * 64 // n, (i + 1) * 64 // n, 200 - 200 * i / n
            lst.append((half - (i + 1) * pitch + gap, 0, b0, b1, hue))
            lst.append((half + i * pitch, 1, b0, b1, hue))
    n = H // 4 if center else H // 2

    def row(x, s, col):
        if center:
            d.line((x, H // 2 - 2 - s * 2, x + bw - 1, H // 2 - 2 - s * 2), fill=col)
            d.line((x, H // 2 + 1 + s * 2, x + bw - 1, H // 2 + 1 + s * 2), fill=col)
        else:
            d.line((x, H - 1 - s * 2, x + bw - 1, H - 1 - s * 2), fill=col)

    for x, ch, b0, b1, hue in lst:
        chans = (0, 1) if ch == 2 else (ch,)
        rng = range(b0, max(b0 + 1, b1))
        v = max(SPEC[c][b] for c in chans for b in rng)
        lit = int(v * n + 0.5)
        for s in range(lit):
            f = (s + 0.5) / n
            row(x, s, hsv(hue, 1, 0.35 + 0.65 * f) if center else hsv(120 - 120 * f, 1, 1))
        if dual:
            cl = min(n - 1, int(max(PEAK[0][b] for b in rng) * n))
            cr = min(n - 1, int(max(PEAK[1][b] for b in rng) * n))
            if cl == cr:
                row(x, cl, (255, 220, 0))
            else:
                row(x, cl, (255, 30, 20)); row(x, cr, WHITE)
        else:
            cap = min(n - 1, int(max(PEAK[c][b] for c in chans for b in rng) * n))
            if cap >= lit:
                row(x, cap, WHITE)
    return im


def vupos(db):
    lin, lo, hi = 10 ** (db / 20), 10 ** (-1), 10 ** (3 / 20)
    return max(0, min(1, (lin - lo) / (hi - lo)))


def mode_aiguilles():
    im = Image.new('RGB', (W, H))
    dw, dh = W // 2, H
    for c, needle in enumerate((0.72, 0.55)):
        cv = Image.new("RGB", (dw, dh), (20, 70, 200)); d = ImageDraw.Draw(cv)
        ink, red = (10, 6, 0), (200, 20, 10)
        d.rectangle((0, 0, dw - 1, dh - 1), outline=(6, 20, 70))
        px, py, r = dw / 2, dh * 1.35, dh * 1.15
        a0, a1 = -0.85, 0.85
        for t in (-20, -10, -7, -5, -3, -2, -1, 0, 1, 2, 3):
            a = a0 + (a1 - a0) * vupos(t)
            ln = 4 if t in (0, -10, -20, 3) else 2
            for k in range(ln):
                x, y = int(px + math.sin(a) * (r - k)), int(py - math.cos(a) * (r - k))
                if 0 <= x < dw and 0 <= y < dh: cv.putpixel((x, y), red if t > 0 else ink)  # hors cadran : coupe
        p = 0.0
        while p <= 1.0:
            a = a0 + (a1 - a0) * p
            x, y = int(px + math.sin(a) * (r + 1)), int(py - math.cos(a) * (r + 1))
            if 0 <= x < dw and 0 <= y < dh: cv.putpixel((x, y), red if p >= vupos(0) else ink)
            p += 0.01
        text(cv, dw // 2 - 5, dh - 10, 'VU', ink)
        text(cv, 2, dh - 9, 'R' if c else 'L', ink)
        a = a0 + (a1 - a0) * needle
        d.line((px, py, px + math.sin(a) * (r - 1), py - math.cos(a) * (r - 1)), fill=ink)  # aiguille fine
        im.paste(cv, (c * dw, 0))
    return im


def mode_jeu():
    """Mode Jeu : shoot'em up automatique (sprites de vumeter.ino)."""
    im = Image.new('RGB', (W, H)); d = ImageDraw.Draw(im)
    ship = [0b0001000, 0b0011100, 0b0111110, 0b1111111, 0b1001001]
    en = [[0b0100010, 0b0011100, 0b0110110, 0b1111111, 0b1010101],
          [0b1000001, 0b0111110, 0b1101011, 0b0111110, 0b0100010]]

    def sprite(rows, x, y, col):
        for r, bits in enumerate(rows):
            for c in range(7):
                if bits & (0x40 >> c) and 0 <= x + c < W and 0 <= y + r < H:
                    im.putpixel((x + c, y + r), col)

    for _ in range(W // 4):  # etoiles
        v = random.randint(50, 150)
        im.putpixel((random.randrange(W), random.randrange(8, H)), (v, v, v))
    cols = max(4, min(12, (W - 14) // 10))
    x0 = (W - cols * 10) // 2 + 1 + 3
    dead = {(0, 2), (1, 5), (2, 0), (2, 7), (1, 8)}
    for r, hue in enumerate((300, 35, 140)):
        for c in range(cols):
            if (r, c) in dead or (r, c) == (2, 4):
                continue
            sprite(en[(c + r) % 2], x0 + c * 10, 9 + r * 7, hsv(hue, 1, 0.9))
    sprite(en[1], 58, 38, hsv(140, 1, 0.9))  # ennemi en pique
    sprite(ship, 61, 57, (0, 200, 255))
    for y in (44, 45, 46):  # tir du vaisseau
        im.putpixel((64, y), (255, 230, 0) if y < 46 else (160, 120, 0))
    for y in (30, 31):  # tir ennemi
        im.putpixel((90, y), (255, 40, 30))
    for k, (dx, dy) in enumerate(((1, 0), (1, 1), (0, 1), (-1, 1), (-1, 0), (-1, -1), (0, -1), (1, -1))):
        im.putpixel((25 + dx * 2, 18 + dy * 2), (255, 140, 0))  # explosion
    d.text((1, -1), '1250', fill=(200, 200, 200), font=TINY)
    for i in range(3):
        d.rectangle((W - 4 - i * 5, 2, W - 2 - i * 5, 4), fill=(0, 200, 255))
    return im


TF = {str(i): g for i, g in enumerate(([7, 5, 5, 5, 7], [2, 6, 2, 2, 7], [7, 1, 7, 4, 7], [7, 1, 7, 1, 7], [5, 5, 7, 1, 1],
                                        [7, 4, 7, 1, 7], [7, 4, 7, 5, 7], [7, 1, 1, 1, 1], [7, 5, 7, 5, 7], [7, 5, 7, 1, 7]))}
TF.update({'L': [4, 4, 4, 4, 7], 'R': [6, 5, 6, 5, 5], 'P': [6, 5, 6, 4, 4], 'E': [7, 4, 6, 4, 7], 'A': [2, 5, 7, 5, 5],
           'K': [5, 5, 6, 5, 5], 'm': [0, 6, 7, 5, 5], '.': [0, 0, 0, 0, 2]})


def tiny(im, x, y, txt, col):
    for ch in txt:
        for r, bits in enumerate(TF.get(ch, [0] * 5)):
            for c in range(3):
                if bits & (4 >> c) and 0 <= x + c < im.width and 0 <= y + r < im.height:
                    im.putpixel((x + c, y + r), col)
        x += 4


PWR_DB, PWR_POS = [-40, -30, -20, -10, -5, 0, 3], [0, .17, .40, .66, .78, .90, 1.0]
WAT_DB, WAT_POS = [-60, -50, -40, -30, -20, -10, 0, 5], [0, .10, .25, .38, .50, .66, .85, 1.0]


def scale_pos(dbs, pos, v):
    if v <= dbs[0]:
        return 0
    for i in range(1, len(dbs)):
        if v <= dbs[i]:
            return pos[i - 1] + (pos[i] - pos[i - 1]) * (v - dbs[i - 1]) / (dbs[i] - dbs[i - 1])
    return 1


def mode_amp(style):
    """Modes Puissance (style 0) et Watts (style 1) de vumeter.ino."""
    A0, A1 = -0.8, 0.8
    aw, ah = W // 2, H
    im = Image.new('RGB', (W, H))
    for c, needle in enumerate((0.62, 0.48)):
        cv = Image.new('RGB', (aw, ah)); d = ImageDraw.Draw(cv)
        r = min(ah * 1.12, (aw / 2 - 2) / math.sin(A1))
        px, py = aw / 2, 14 + r

        def pt(p, rr):
            a = A0 + (A1 - A0) * p
            return px + math.sin(a) * rr, py - math.cos(a) * rr

        def label(p, rr, txt, col):
            x, y = pt(p, rr)
            w = len(txt) * 4 - 1
            tiny(cv, max(1, min(aw - 1 - w, int(x - w / 2))), int(y - 2), txt, col)

        if style == 0:
            for y in range(ah):
                f = y / ah
                g = ((f - .5) / .5) ** 2 if f > .5 else 0
                d.line((0, y, aw, y), fill=(int(2 + 25 * g), int(3 + 60 * g), int(12 + 210 * g)))
            for v in (-35, -25, -15, -7):
                q = scale_pos(PWR_DB, PWR_POS, v)
                d.line((*pt(q, r * .62), *pt(q, r * .92)), fill=(90, 100, 150))
            for v, q in zip(PWR_DB, PWR_POS):
                d.line((*pt(q, r * .42), *pt(q, r)), fill=(225, 230, 255))
                label(q, r + 5, str(abs(v)), (235, 240, 255))
            ncol, lcol = (255, 40, 30), (150, 160, 200)
        else:
            for y in range(ah):
                f = y / ah
                g = (f - .6) / .4 if f > .6 else 0
                d.line((0, y, aw, y), fill=(int(70 + 80 * g), int(150 + 60 * g), int(245 + 10 * g)))
            navy = (8, 18, 110)
            steps = int(r * 4)
            for k in range(steps + 1):
                q = k / steps
                if any(abs(q - t) < .006 for t in WAT_POS[1:7]):
                    continue
                for t in range(4 if q > .85 else 3):
                    x, y = pt(q, r - t)
                    if 0 <= x < aw and 0 <= y < ah:
                        cv.putpixel((int(x), int(y)), navy)
            for i, (txt, q) in enumerate(zip(('5.0m', '50m', '100', '150', '200', '250'), (.05, .22, .40, .53, .67, .84))):
                if i in (1, 2, 4):
                    label(q, r + 5, txt, navy)
            for i in range(1, 7):
                if i % 2 == 0:
                    label(WAT_POS[i], r - 9, str(abs(WAT_DB[i])), navy)
            ncol, lcol = navy, navy
        tiny(cv, aw - 5 if c else 2, ah - 6, 'R' if c else 'L', lcol)
        d.line((px, py, *pt(needle, r + 3)), fill=ncol)
        im.paste(cv, (c * aw, 0))
    if style == 0:
        tiny(im, W // 2 - 7, H - 18, 'PEAK', (200, 205, 230))
        d = ImageDraw.Draw(im)
        d.ellipse((W // 2 - 3, H - 11, W // 2 + 3, H - 5), outline=(40, 80, 220))
        d.ellipse((W // 2 - 2, H - 10, W // 2 + 2, H - 6), fill=(230, 240, 255))
    return im


# ---------------------------------------------------------------- dalle LED simulee
P = 6  # pas des LED en pixels


def led_panel(px):
    out = Image.new('RGB', (W * P + 16, H * P + 16), (8, 8, 10))
    d = ImageDraw.Draw(out)
    for y in range(H):
        for x in range(W):
            r, g, b = px.getpixel((x, y))
            if r + g + b < 30: r, g, b = 22, 22, 26  # LED eteinte
            cx, cy = 8 + x * P + P / 2, 8 + y * P + P / 2
            d.ellipse((cx - 2.4, cy - 2.4, cx + 2.4, cy + 2.4), fill=(r, g, b))
    return out


def font(size, bold=False):
    for name in (('arialbd.ttf' if bold else 'arial.ttf'), 'DejaVuSans.ttf'):
        try:
            return ImageFont.truetype(name, size)
        except OSError:
            pass
    return ImageFont.load_default()


TINY = font(9)
MODES = [('Spectre', bars()), ('Spectre St (cretes : rouge = gauche, blanc = droite, jaune = les deux)', bars(dual=True)),
         ('Miroir', bars(center=True)), ('Stereo', bars(stereo=True)),
         ('Aiguilles', mode_aiguilles()), ('Jeu (dans la demo, joue tout seul)', mode_jeu()),
         ('Puissance (voyant PEAK au centre)', mode_amp(0)), ('Watts', mode_amp(1))]
pw, ph = W * P + 16, H * P + 16
M, CAP, TOP = 40, 50, 110
img = Image.new('RGB', (2 * pw + 3 * M, TOP + 4 * (ph + CAP + M)), (18, 18, 22))
d = ImageDraw.Draw(img)
d.text((img.width // 2, 40), 'VU-metre HUB75 - les 8 modes (dalle 128x64)', font=font(34, True), fill=(235, 235, 235), anchor='mm')
d.text((img.width // 2, 80), 'Touche m du menu serie : mode suivant - Auto fait defiler les modes toutes les 20 s - LED eteintes en gris fonce',
       font=font(18), fill=(150, 150, 150), anchor='mm')
for i, (name, px) in enumerate(MODES):
    x, y = M + (i % 2) * (pw + M), TOP + (i // 2) * (ph + CAP + M)
    d.text((x, y + 18), name, font=font(22, True), fill=(220, 220, 220), anchor='lm')
    img.paste(led_panel(px), (x, y + CAP - 8))
img.save(OUT)
print(OUT)
