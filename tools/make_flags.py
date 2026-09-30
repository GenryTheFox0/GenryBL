"""Flags of GenryBL's 20 languages -> data/flags/<code>.png (96x64, drawn at 4x and scaled down).
Windows shows flag emoji as two letters («RU»), so the language chooser draws its own.

    python tools/make_flags.py
"""
import math, os
from PIL import Image, ImageDraw

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
W, H, K = 96, 64, 4
w, h = W * K, H * K


def new(bg='white'):
    im = Image.new('RGB', (w, h), bg)
    return im, ImageDraw.Draw(im)


def hstripes(cols, weights=None):
    im, d = new()
    weights = weights or [1] * len(cols)
    tot, y = sum(weights), 0.0
    for c, wt in zip(cols, weights):
        y2 = y + h * wt / tot
        d.rectangle([0, round(y), w, round(y2)], fill=c)
        y = y2
    return im, d


def vstripes(cols):
    im, d = new()
    for i, c in enumerate(cols):
        d.rectangle([round(w * i / len(cols)), 0, round(w * (i + 1) / len(cols)), h], fill=c)
    return im, d


def star(d, cx, cy, r, fill, rot=-90):
    pts = []
    for i in range(10):
        a = math.radians(rot + i * 36)
        rr = r if i % 2 == 0 else r * 0.382
        pts.append((cx + rr * math.cos(a), cy + rr * math.sin(a)))
    d.polygon(pts, fill=fill)


def uk():
    im, d = new('#012169')
    t = h * 0.2
    d.line([(0, 0), (w, h)], fill='white', width=int(t * 0.9)); d.line([(0, h), (w, 0)], fill='white', width=int(t * 0.9))
    d.line([(0, 0), (w, h)], fill='#C8102E', width=int(t * 0.35)); d.line([(0, h), (w, 0)], fill='#C8102E', width=int(t * 0.35))
    d.rectangle([w / 2 - t, 0, w / 2 + t, h], fill='white'); d.rectangle([0, h / 2 - t, w, h / 2 + t], fill='white')
    d.rectangle([w / 2 - t * 0.6, 0, w / 2 + t * 0.6, h], fill='#C8102E'); d.rectangle([0, h / 2 - t * 0.6, w, h / 2 + t * 0.6], fill='#C8102E')
    return im


def belarus():
    im, d = hstripes(['#C8313E', '#4AA657'], [2, 1])
    band = w * 0.13
    d.rectangle([0, 0, band, h], fill='white')
    n = 6
    for i in range(n):
        cy = h * (i + 0.5) / n
        s = band * 0.36
        d.polygon([(band / 2, cy - s * 1.3), (band / 2 + s, cy), (band / 2, cy + s * 1.3), (band / 2 - s, cy)], fill='#C8313E')
    return im


def kazakhstan():
    im, d = new('#00AFCA')
    gold = '#FEC50C'
    cx, cy, r = w * 0.55, h * 0.42, h * 0.17
    for i in range(32):
        a = math.radians(i * 360 / 32)
        d.polygon([(cx + r * 1.25 * math.cos(a - 0.05), cy + r * 1.25 * math.sin(a - 0.05)),
                   (cx + r * 1.9 * math.cos(a), cy + r * 1.9 * math.sin(a)),
                   (cx + r * 1.25 * math.cos(a + 0.05), cy + r * 1.25 * math.sin(a + 0.05))], fill=gold)
    d.ellipse([cx - r, cy - r, cx + r, cy + r], fill=gold)
    # the eagle under the sun, a soft wing line
    d.arc([cx - r * 2.6, cy + r * 1.3, cx + r * 2.6, cy + r * 3.4], 200, 340, fill=gold, width=int(h * 0.035))
    # ornament at the hoist
    x0 = w * 0.05
    for i in range(7):
        y = h * (i + 0.5) / 7
        d.rectangle([x0, y - h * 0.03, x0 + w * 0.06, y + h * 0.03], fill=gold)
        d.ellipse([x0 + w * 0.01, y - h * 0.055, x0 + w * 0.05, y + h * 0.055], outline=gold, width=int(K * 2))
    return im


def czech():
    im, d = hstripes(['white', '#D7141A'])
    d.polygon([(0, 0), (w * 0.5, h / 2), (0, h)], fill='#11457E')
    return im


def spain():
    im, d = hstripes(['#AA151B', '#F1BF00', '#AA151B'], [1, 2, 1])
    x, y = w * 0.27, h * 0.5
    d.rounded_rectangle([x - w * 0.06, y - h * 0.16, x + w * 0.06, y + h * 0.14], radius=K * 3, fill='#AA151B', outline='#7a5a00', width=K)
    d.rectangle([x - w * 0.045, y - h * 0.22, x + w * 0.045, y - h * 0.17], fill='#AD8B26')
    return im


def brazil():
    im, d = new('#009C3B')
    m = w * 0.085
    d.polygon([(m, h / 2), (w / 2, h * 0.1), (w - m, h / 2), (w / 2, h * 0.9)], fill='#FFDF00')
    r = h * 0.26
    d.ellipse([w / 2 - r, h / 2 - r, w / 2 + r, h / 2 + r], fill='#002776')
    d.arc([w / 2 - r * 1.6, h / 2 - r * 0.5, w / 2 + r * 1.2, h / 2 + r * 2.6], 215, 300, fill='white', width=int(K * 3))
    for sx, sy in ((0.46, 0.58), (0.54, 0.62), (0.5, 0.66), (0.43, 0.64), (0.57, 0.55)):
        star(d, w * sx, h * sy, K * 2.6, 'white')
    return im


def turkey():
    im, d = new('#E30A17')
    cx, cy, r = w * 0.38, h / 2, h * 0.25
    d.ellipse([cx - r, cy - r, cx + r, cy + r], fill='white')
    r2 = r * 0.8
    d.ellipse([cx - r2 + r * 0.25, cy - r2, cx + r2 + r * 0.25, cy + r2], fill='#E30A17')
    star(d, cx + r * 1.02, cy, r * 0.42, 'white', rot=180)
    return im


def japan():
    im, d = new('white')
    r = h * 0.3
    d.ellipse([w / 2 - r, h / 2 - r, w / 2 + r, h / 2 + r], fill='#BC002D')
    return im


def china():
    im, d = new('#EE1C25')
    y = '#FFFF00'
    u = h / 20
    star(d, u * 5, u * 5, u * 3, y)
    for sx, sy in ((10, 2), (12, 4), (12, 7), (10, 9)):
        a = math.degrees(math.atan2(5 - sy, 5 - sx))
        star(d, u * sx, u * sy, u, y, rot=a)
    return im


def taiwan():
    im, d = new('#FE0000')
    d.rectangle([0, 0, w / 2, h / 2], fill='#000095')
    cx, cy, r = w / 4, h / 4, h * 0.13
    for i in range(12):
        a = math.radians(i * 30 - 90)
        d.polygon([(cx + r * 0.9 * math.cos(a - 0.26), cy + r * 0.9 * math.sin(a - 0.26)),
                   (cx + r * 1.7 * math.cos(a), cy + r * 1.7 * math.sin(a)),
                   (cx + r * 0.9 * math.cos(a + 0.26), cy + r * 0.9 * math.sin(a + 0.26))], fill='white')
    d.ellipse([cx - r * 1.05, cy - r * 1.05, cx + r * 1.05, cy + r * 1.05], fill='#000095')
    d.ellipse([cx - r * 0.88, cy - r * 0.88, cx + r * 0.88, cy + r * 0.88], fill='white')
    return im


def korea():
    im, d = new('white')
    cx, cy, r = w / 2, h / 2, h * 0.25
    d.pieslice([cx - r, cy - r, cx + r, cy + r], 180, 360, fill='#CD2E3A')
    d.pieslice([cx - r, cy - r, cx + r, cy + r], 0, 180, fill='#0047A0')
    d.ellipse([cx - r, cy - r / 2, cx, cy + r / 2], fill='#CD2E3A')
    d.ellipse([cx, cy - r / 2, cx + r, cy + r / 2], fill='#0047A0')
    # the four trigrams, as short bars
    bw, bh, gap = w * 0.1, h * 0.04, h * 0.065
    for (tx, ty, ang, pat) in ((0.2, 0.25, -34, (1, 1, 1)), (0.8, 0.75, -34, (0, 0, 0)), (0.8, 0.25, 34, (1, 0, 1)), (0.2, 0.75, 34, (0, 1, 0))):
        tri = Image.new('RGBA', (int(bw * 1.4), int(gap * 3.4)), (0, 0, 0, 0))
        td = ImageDraw.Draw(tri)
        for i, full in enumerate(pat):
            y0 = gap * 0.4 + i * gap
            if full:
                td.rectangle([bw * 0.2, y0, bw * 1.2, y0 + bh], fill='black')
            else:
                td.rectangle([bw * 0.2, y0, bw * 0.65, y0 + bh], fill='black')
                td.rectangle([bw * 0.75, y0, bw * 1.2, y0 + bh], fill='black')
        tri = tri.rotate(ang, expand=True, resample=Image.BICUBIC)
        im.paste(tri, (int(w * tx - tri.width / 2), int(h * ty - tri.height / 2)), tri)
    return im


def vietnam():
    im, d = new('#DA251D')
    star(d, w / 2, h / 2, h * 0.3, '#FFFF00')
    return im


FLAGS = {
    'ru': lambda: hstripes(['white', '#0039A6', '#D52B1E'])[0],
    'en': uk,
    'uk': lambda: hstripes(['#0057B7', '#FFD700'])[0],
    'be': belarus,
    'kk': kazakhstan,
    'pl': lambda: hstripes(['white', '#DC143C'])[0],
    'cs': czech,
    'de': lambda: hstripes(['#000000', '#DD0000', '#FFCE00'])[0],
    'fr': lambda: vstripes(['#0055A4', 'white', '#EF4135'])[0],
    'es': spain,
    'pt_BR': brazil,
    'it': lambda: vstripes(['#009246', 'white', '#CE2B37'])[0],
    'tr': turkey,
    'ja': japan,
    'zh_CN': china,
    'zh_TW': taiwan,
    'ko': korea,
    'vi': vietnam,
    'id': lambda: hstripes(['#FF0000', 'white'])[0],
    'th': lambda: hstripes(['#A51931', '#F4F5F8', '#2D2A4A', '#F4F5F8', '#A51931'], [1, 1, 2, 1, 1])[0],
}

if __name__ == '__main__':
    out = os.path.join(ROOT, 'data', 'flags')
    os.makedirs(out, exist_ok=True)
    for code, fn in FLAGS.items():
        im = fn().convert('RGBA')
        # rounded corners + a thin shade line, like a sticker
        mask = Image.new('L', (w, h), 0)
        ImageDraw.Draw(mask).rounded_rectangle([0, 0, w - 1, h - 1], radius=K * 5, fill=255)
        im.putalpha(mask)
        ImageDraw.Draw(im).rounded_rectangle([0, 0, w - 1, h - 1], radius=K * 5, outline=(0, 0, 0, 60), width=K)
        im.resize((W, H), Image.LANCZOS).save(os.path.join(out, code + '.png'), optimize=True)
    print(len(FLAGS), 'flags ->', out)
