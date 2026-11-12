"""Dessine docs/branchement.png : ESP32 -> dalle HUB75, entrees audio stereo, bouton, alimentation, chainage."""
import os

from PIL import Image, ImageDraw, ImageFont

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'branchement.png')
W, H = 2000, 1440
img = Image.new('RGB', (W, H), (250, 250, 247))
d = ImageDraw.Draw(img)


def font(size, bold=False):
    for name in (('arialbd.ttf' if bold else 'arial.ttf'), 'DejaVuSans.ttf'):
        try:
            return ImageFont.truetype(name, size)
        except OSError:
            pass
    return ImageFont.load_default()


F, FB, FS, FT = font(20), font(20, True), font(16), font(30, True)
INK, GREY, RED, BLUE, GREEN, ORANGE = (30, 30, 30), (120, 120, 120), (200, 40, 30), (30, 90, 200), (30, 140, 60), (220, 120, 0)
COLORS = {'R1': (220, 40, 40), 'G1': (30, 160, 60), 'B1': (40, 90, 220), 'R2': (220, 40, 40), 'G2': (30, 160, 60), 'B2': (40, 90, 220),
          'A': (140, 70, 20), 'B': (140, 70, 20), 'C': (140, 70, 20), 'D': (140, 70, 20), 'E': (140, 70, 20),
          'CLK': (130, 40, 160), 'LAT': (130, 40, 160), 'OE': (130, 40, 160)}


def box(x0, y0, x1, y1, title, fill=(255, 255, 255), outline=INK):
    d.rounded_rectangle((x0, y0, x1, y1), 14, fill=fill, outline=outline, width=3)
    d.text(((x0 + x1) // 2, y0 + 22), title, font=FB, fill=INK, anchor='mm')


def text(x, y, s, f=F, col=INK, anchor='la'):
    d.text((x, y), s, font=f, fill=col, anchor=anchor)


def wire(points, col, w=3):
    d.line(points, fill=col, width=w, joint='curve')


def dot(x, y, col=INK):
    d.ellipse((x - 5, y - 5, x + 5, y + 5), fill=col)


text(W // 2, 30, 'VU-metre HUB75 - branchement', FT, anchor='mt')
text(W // 2, 72, 'ESP32 DevKit (38 broches) - meme brochage dalle que l\'horloge - sans wifi', FS, GREY, anchor='mt')

# ---------------------------------------------------------------- ESP32
ex0, ey0, ex1, ey1 = 760, 140, 1080, 900
box(ex0, ey0, ex1, ey1, 'ESP32 DevKit', (236, 242, 250))
LEFT = [('3V3', '3,3 V'), ('GND', 'masse'), ('GPIO36', 'audio G (VP)'), ('GPIO39', 'audio D (VN)'), ('GPIO18', 'demo'), ('5V / VIN', '5 V'), ('GND', 'masse')]
RIGHT = [('GPIO25', 'R1'), ('GPIO26', 'G1'), ('GPIO27', 'B1'), ('GPIO14', 'R2'), ('GPIO12', 'G2'), ('GPIO13', 'B2'),
         ('GPIO23', 'A'), ('GPIO19', 'B'), ('GPIO5', 'C'), ('GPIO17', 'D'), ('GPIO32', 'E'), ('GPIO16', 'CLK'),
         ('GPIO4', 'LAT'), ('GPIO15', 'OE'), ('GND', 'GND')]
lpin = {}
for i, (p, _) in enumerate(LEFT):
    y = ey0 + 80 + i * 105
    d.rectangle((ex0 - 12, y - 6, ex0, y + 6), fill=INK)
    text(ex0 + 12, y, p, F, INK, 'lm')
    lpin[i] = (ex0 - 12, y)
rpin = {}
for i, (p, sig) in enumerate(RIGHT):
    y = ey0 + 70 + i * 50
    d.rectangle((ex1, y - 6, ex1 + 12, y + 6), fill=INK)
    text(ex1 - 12, y, p, F, INK, 'rm')
    rpin[sig] = (ex1 + 12, y)

# ---------------------------------------------------------------- HUB75 connector (pins 1-16)
hx0, hy0 = 1440, 140
box(hx0, hy0, hx0 + 260, hy0 + 590, 'HUB75 (entree)', (255, 252, 240))
HUB = ['R1', 'G1', 'B1', 'GND', 'R2', 'G2', 'B2', 'E', 'A', 'B', 'C', 'D', 'CLK', 'LAT', 'OE', 'GND']
hpin = {}
for n, sig in enumerate(HUB):
    y = hy0 + 70 + n * 32
    d.rectangle((hx0 + 20, y - 11, hx0 + 42, y + 11), outline=INK, width=2, fill=(255, 255, 255))
    text(hx0 + 56, y, '%2d' % (n + 1), FS, GREY, 'lm')
    text(hx0 + 92, y, sig, FB, COLORS.get(sig, GREY), 'lm')
    if sig not in hpin: hpin[sig] = (hx0 + 20, y)
# physical layout reminder
text(hx0 + 130, hy0 + 610, 'Nappe 2 x 8 vue de face :', FS, GREY, 'mt')
text(hx0 + 130, hy0 + 634, 'impairs a gauche, pairs a droite', FS, GREY, 'mt')
order = ['R1', 'G1', 'B1', 'R2', 'G2', 'B2', 'A', 'B', 'C', 'D', 'E', 'CLK', 'LAT', 'OE', 'GND']
for i, sig in enumerate(order):
    x0, y0 = rpin[sig]
    hx, hy = hpin[sig]
    mid = 1130 + i * 19
    col = COLORS.get(sig, INK)
    wire([(x0, y0), (mid, y0), (mid, hy), (hx, hy)], col, 3)
    dot(hx, hy, col)

# ---------------------------------------------------------------- panels and chaining
py0 = 860
text(1430, py0 - 40, 'Dalles (choix par le menu serie)', FB)
d.rounded_rectangle((1430, py0, 1640, py0 + 120), 8, outline=INK, width=3, fill=(20, 20, 30))
d.rounded_rectangle((1690, py0, 1900, py0 + 120), 8, outline=INK, width=3, fill=(20, 20, 30))
text(1535, py0 + 45, 'Dalle 1', FB, (240, 240, 240), 'mm')
text(1795, py0 + 45, 'Dalle 2', FB, (240, 240, 240), 'mm')
text(1535, py0 + 80, 'IN  ->  OUT', FS, (200, 200, 200), 'mm')
text(1795, py0 + 80, 'IN', FS, (200, 200, 200), 'mm')
wire([(1640, py0 + 60), (1690, py0 + 60)], ORANGE, 6)
text(1665, py0 + 140, 'nappe OUT -> IN', FS, ORANGE, 'mt')
wire([(1720, hy0 + 400), (1760, hy0 + 400), (1760, py0)], GREY, 4)
text(1770, hy0 + 420, 'nappe 16 fils', FS, GREY)
for i, s in enumerate(['64x64 : 1 dalle 64x64', '2x64x64 : 2 dalles 64x64 -> 128x64', '128x64 : 1 dalle 128x64',
                       '2x128x64 : 2 dalles 128x64 -> 256x64', '128x128 : 2 dalles 128x64 empilees']):
    text(1430, py0 + 170 + i * 28, '- ' + s, FS)
text(1430, py0 + 320, 'Dalles 1/32 : la broche E est obligatoire.', FS, RED)

# ---------------------------------------------------------------- audio inputs
ax0, ay0 = 40, 140
box(ax0, ay0, ax0 + 640, ay0 + 700, 'Entree audio ligne (x2 : gauche et droite)', (240, 250, 242))
text(ax0 + 20, ay0 + 55, 'Meme montage pour chaque voie :', FS, GREY)
for ch, (label, pin_i, y) in enumerate((('G', 2, ay0 + 220), ('D', 3, ay0 + 520))):
    # jack -> 10 uF -> node -> 1 k -> GPIO ; 15 k to 3V3 ; 10 k to GND ; 10 nF to GND at the pin
    text(ax0 + 30, y - 150, 'Voie ' + ('gauche' if label == 'G' else 'droite') + ' (jack / RCA)', FB)
    d.ellipse((ax0 + 30, y - 18, ax0 + 66, y + 18), outline=INK, width=3)
    text(ax0 + 48, y + 30, label, FB, INK, 'mt')
    wire([(ax0 + 66, y), (ax0 + 130, y)], INK)
    d.line((ax0 + 130, y - 20, ax0 + 130, y + 20), fill=BLUE, width=4)
    d.line((ax0 + 142, y - 20, ax0 + 142, y + 20), fill=BLUE, width=4)
    text(ax0 + 136, y - 30, '10 uF', FS, BLUE, 'mb')
    wire([(ax0 + 142, y), (ax0 + 260, y)], INK)
    dot(ax0 + 260, y)
    # divider
    wire([(ax0 + 260, y), (ax0 + 260, y - 40)], INK)
    d.rectangle((ax0 + 248, y - 100, ax0 + 272, y - 40), outline=GREEN, width=3)
    text(ax0 + 282, y - 70, '15 k', FS, GREEN, 'lm')
    wire([(ax0 + 260, y - 100), (ax0 + 260, y - 120)], RED)
    text(ax0 + 260, y - 124, '3,3 V', FS, RED, 'mb')
    wire([(ax0 + 260, y), (ax0 + 260, y + 40)], INK)
    d.rectangle((ax0 + 248, y + 40, ax0 + 272, y + 100), outline=GREEN, width=3)
    text(ax0 + 282, y + 70, '10 k', FS, GREEN, 'lm')
    wire([(ax0 + 260, y + 100), (ax0 + 260, y + 120)], INK)
    d.line((ax0 + 240, y + 120, ax0 + 280, y + 120), fill=INK, width=4)
    # series 1 k + 10 nF
    wire([(ax0 + 260, y), (ax0 + 340, y)], INK)
    d.rectangle((ax0 + 340, y - 12, ax0 + 400, y + 12), outline=GREEN, width=3)
    text(ax0 + 370, y - 18, '1 k', FS, GREEN, 'mb')
    wire([(ax0 + 400, y), (ax0 + 500, y)], INK)
    dot(ax0 + 480, y)
    wire([(ax0 + 480, y), (ax0 + 480, y + 40)], INK)
    d.line((ax0 + 460, y + 40, ax0 + 500, y + 40), fill=BLUE, width=4)
    d.line((ax0 + 460, y + 52, ax0 + 500, y + 52), fill=BLUE, width=4)
    text(ax0 + 508, y + 46, '10 nF', FS, BLUE, 'lm')
    wire([(ax0 + 480, y + 52), (ax0 + 480, y + 80)], INK)
    d.line((ax0 + 460, y + 80, ax0 + 500, y + 80), fill=INK, width=4)
    # to ESP32
    tx, ty = lpin[pin_i]
    wire([(ax0 + 500, y), (ax0 + 600, y), (ax0 + 600, ty), (tx, ty)], ORANGE, 4)
    dot(tx, ty, ORANGE)
    text(ax0 + 610, (y + ty) // 2, 'GPIO ' + ('36' if label == 'G' else '39'), FS, ORANGE, 'lm')
text(ax0 + 20, ay0 + 640, 'Point milieu ~1,3 V. Sortie haut-parleur : ajouter un pont diviseur.', FS, RED)
text(ax0 + 20, ay0 + 665, 'Masse du jack reliee a la masse de l\'ESP32.', FS, GREY)

# ---------------------------------------------------------------- demo jumper (GPIO18 -> GND)
jx, jy = lpin[4]
wire([(jx, jy), (735, jy), (735, jy + 14)], ORANGE)
for yy in (jy + 20, jy + 40):  # two header pins
    d.rectangle((729, yy - 6, 741, yy + 6), fill=INK)
d.rounded_rectangle((724, jy + 12, 746, jy + 48), 4, outline=ORANGE, width=3)  # the shunt
wire([(735, jy + 48), (735, jy + 60)], INK)
d.line((718, jy + 60, 752, jy + 60), fill=INK, width=4)
text(ex0 + 12, jy + 26, 'cavalier = demo', FS, ORANGE, 'lm')

# ---------------------------------------------------------------- power
px0, py0p = 40, 900
box(px0, py0p, px0 + 640, py0p + 300, 'Alimentation 5 V - 10 A mini (2 dalles)', (252, 240, 240))
text(px0 + 30, py0p + 70, '+5 V  ->  borniers d\'alimentation des dalles', F, RED)
text(px0 + 30, py0p + 110, '+5 V  ->  broche 5 V / VIN de l\'ESP32', F, RED)
text(px0 + 30, py0p + 150, 'GND   ->  dalles + ESP32 (masse commune)', F, INK)
text(px0 + 30, py0p + 200, 'Fils de 1,5 mm2 pour le 5 V des dalles.', FS, GREY)
text(px0 + 30, py0p + 230, 'Ne pas alimenter les dalles par l\'USB de l\'ESP32.', FS, RED)
vx, vy = lpin[5]
wire([(px0 + 640, py0p + 110), (720, py0p + 110), (720, vy), (vx, vy)], RED, 4)
gx, gy = lpin[6]
wire([(px0 + 640, py0p + 150), (700, py0p + 150), (700, gy), (gx, gy)], INK, 4)

# ---------------------------------------------------------------- serial menu and demo
ux0, uy0 = 760, 960
box(ux0, uy0, ux0 + 620, uy0 + 380, 'Reglages : moniteur serie 115200 bauds', (245, 245, 255))
for i, s in enumerate(['Menu au demarrage, ou touche ? puis Entree :', '1 a 4 : type de dalle (memorise, redemarre)',
                       'd : demo marche / arret (memorise)', "m : mode d'affichage suivant",
                       'l : luminosite suivante',
                       'Modes : Auto, Spectre, Miroir, Aiguilles,', 'Stereo, Spectre St, Jeu (demo), Puissance, Watts',
                       'Cavalier GPIO18 - GND : demo forcee', '(musique interne, sans source audio)']):
    text(ux0 + 30, uy0 + 60 + i * 34, s, FB if i == 0 else F, ORANGE if i >= 7 else INK)

img.save(OUT)
print(OUT)
