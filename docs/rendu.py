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
def mode_barres():
    im = Image.new('RGB', (W, H)); d = ImageDraw.Draw(im)
    text(im, 1, 13, 'L', (160, 160, 160)); text(im, 1, 43, 'R', (160, 160, 160))
    sw, n = 4, (W - 8) // 4
    d.line((8, 31, 8 + n * sw - 2, 31), fill=(60, 60, 60))
    for t in (-40, -30, -20, -10, -6, -3, 0):
        x = 8 + int((t + 45) / 45 * n * sw) - 1
        x = max(8, min(W - 2, x))
        d.line((x, 29, x, 33), fill=(200, 40, 30) if t >= -3 else (120, 120, 120))
    for c in range(2):
        lit, cap = int(RMS[c] * n + 0.5), min(n - 1, int((RMS[c] + 0.08) * n))
        for s in range(n):
            f = (s + 1) / n
            on = (255, 30, 20) if f > 0.88 else (30, 110, 255)  # barres bleues, rouge en saturation
            col = WHITE if s == cap else on if s < lit else dim(on)
            x, y = 8 + s * sw, 36 if c else 6
            d.rectangle((x, y, x + sw - 2, y + 21), fill=col)
    return im


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
        if c == 0: d.ellipse((dw - 7, dh - 8, dw - 3, dh - 4), fill=(255, 30, 20))
        a = a0 + (a1 - a0) * needle
        for o in (0, 1):
            d.line((px + o, py, px + o + math.sin(a) * (r - 1), py - math.cos(a) * (r - 1)), fill=ink)
        im.paste(cv, (c * dw, 0))
    return im


def mode_onde():
    im = Image.new('RGB', (W, H)); d = ImageDraw.Draw(im)
    for c in range(2):
        y0, col = (47, (255, 120, 20)) if c else (15, (30, 200, 255))
        prev = None
        for x in range(W):
            t = x / W
            v = (9 * math.sin(2 * math.pi * 3 * t + c) + 4 * math.sin(2 * math.pi * 11 * t + 2 * c)
                 + 2 * math.sin(2 * math.pi * 29 * t) + random.uniform(-1.5, 1.5))
            y = max(c * 32, min(c * 32 + 31, int(y0 - v)))
            im.putpixel((x, y0), (30, 30, 30))
            lo, hi = (min(prev, y), max(prev, y)) if prev is not None else (y, y)
            d.line((x, lo, x, hi), fill=col)
            prev = y
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


MODES = [('Barres', mode_barres()), ('Spectre', bars()), ('SpectreL (barres collees)', bars(gap=0)),
         ('Spectre St (cretes : rouge = gauche, blanc = droite, jaune = les deux)', bars(dual=True)),
         ('Miroir', bars(center=True)), ('Stereo', bars(stereo=True)),
         ('Aiguilles', mode_aiguilles()), ('Onde', mode_onde())]
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
