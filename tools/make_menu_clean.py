"""The menu board without its Russian lettering (data/menu/board_<v>_clean.jpg): for every other language the
board text is drawn over the clean paper in that language (qml/LiveBoard.qml). OpenCV inpainting inside the
text zones only: the badge, the flag, the camera, the drawings and the photos stay.

    python tools/make_menu_clean.py [--show]
"""
import json, os, sys
import cv2
import numpy as np

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MENU = os.path.join(ROOT, 'data', 'menu')

# the sheets and plaques of each picture (1920x1080), same as qml/MainMenuScreen.qml
SPOTS = {
 'day':     dict(new=(314,238,661,840), mods=(679,267,981,832), gallery=(995,303,1267,825), help=(384,88,1208,232), settings=(1056,888,1344,1072), quit=(1512,712,1656,880)),
 'day2':    dict(new=(334,238,683,870), mods=(701,251,1002,860), gallery=(1019,283,1297,849), help=(408,96,1232,232), settings=(1069,909,1352,1072), quit=(1533,733,1680,896)),
 'evening': dict(new=(341,250,686,840), mods=(705,280,998,830), gallery=(1012,311,1270,820), help=(408,88,1232,244), settings=(1064,880,1328,1072), quit=(1496,704,1648,880)),
 'morning': dict(new=(322,258,674,859), mods=(691,291,990,849), gallery=(1006,325,1274,842), help=(392,96,1216,252), settings=(1064,899,1328,1072), quit=(1517,728,1664,896)),
 'night':   dict(new=(330,244,681,829), mods=(698,274,990,821), gallery=(1005,311,1271,813), help=(400,88,1232,238), settings=(1064,864,1328,1072), quit=(1488,696,1632,856)),
}
# text zones inside each sheet, as fractions of the sheet: title band, body band
ZONES = {
    'new':     dict(title=(0.04, 0.07, 0.625, 0.215), body=(0.02, 0.245, 0.98, 0.725)),
    'mods':    dict(title=(0.04, 0.08, 0.615, 0.225), body=(0.02, 0.25, 0.98, 0.745)),
    'gallery': dict(title=(0.03, 0.05, 0.545, 0.235), body=(0.02, 0.24, 0.98, 0.70)),
}

# where one picture's text runs lower than the others
ZONE_FIX = {('day2', 'gallery', 'body'): (0.02, 0.24, 0.98, 0.716)}


def frac(r, f):
    x0, y0, x1, y1 = r
    w, h = x1 - x0, y1 - y0
    return (int(x0 + f[0] * w), int(y0 + f[1] * h), int(x0 + f[2] * w), int(y0 + f[3] * h))


def text_mask(img, box, kind):
    """pixels of lettering inside box: dark ink on paper, red titles, gold letters on the plank, pale letters on signs"""
    x0, y0, x1, y1 = box
    roi = img[y0:y1, x0:x1].astype(np.int32)
    lum = roi.mean(axis=2)
    b, g, r = roi[..., 0], roi[..., 1], roi[..., 2]
    if kind in ('body', 'title'):
        # the paper around, smoothed: lettering is clearly darker than it (or clearly red)
        paper = cv2.medianBlur(img[y0:y1, x0:x1], 31).astype(np.int32).mean(axis=2)
        m = (lum < paper - 28)
        if kind == 'title':
            m |= (r - g > 45) & (r - b > 40)
        else:
            m &= ~((r - g > 50) & (r - b > 60))         # the red border lines of the sheet stay
    elif kind == 'help':
        # gold letters (warm, saturated) + their dark outline and shadow on the grey plank
        hsv = cv2.cvtColor(img[y0:y1, x0:x1], cv2.COLOR_BGR2HSV).astype(np.int32)
        gold = (hsv[..., 0] >= 8) & (hsv[..., 0] <= 35) & (hsv[..., 1] > 70) & (hsv[..., 2] > 60)
        dark = lum < cv2.medianBlur(img[y0:y1, x0:x1], 61).astype(np.int32).mean(axis=2) - 30
        m = gold | dark
        m = cv2.morphologyEx(m.astype(np.uint8), cv2.MORPH_CLOSE, np.ones((9, 9), np.uint8)).astype(bool)
    elif kind == 'sign_top':
        # «ВСЕГДА ГОТОВ!»: pale letters on red
        m = ((r - np.maximum(g, b)) < 70) & (lum > 95)
    elif kind == 'sign_panel':
        # «ВЫХОД»: red letters on a pale panel
        m = (r - np.maximum(g, b)) > 45
    else:
        # the stool plate / the red sign: pale or gold letters
        m = (lum > cv2.medianBlur(img[y0:y1, x0:x1], 31).astype(np.int32).mean(axis=2) + 30)
    m = m.astype(np.uint8) * 255
    m = cv2.dilate(m, np.ones((3, 3), np.uint8), iterations=2)
    return m


def sheet_mask(img, sheet, f, kind):
    """the lettering of one text zone of a sheet, without what stands next to it: the red border lines of the
    sheet, the badge with its ribbon, the banner, the camera. A title's last letter may run past its zone - the
    zone is looked at a bit wider, and a spot there counts as a letter when most of it lies inside the zone."""
    core = frac(sheet, f)
    sx0, sy0, sx1, sy1 = sheet
    w = sx1 - sx0
    ext = list(core)
    if kind == 'title':
        ext[2] = min(core[2] + int(0.07 * w), sx1 - int(0.05 * w))
    x0, y0, x1, y1 = ext
    roi = img[y0:y1, x0:x1]
    ri = roi.astype(np.int32)
    lum = ri.mean(axis=2)
    b, g, r = ri[..., 0], ri[..., 1], ri[..., 2]
    bg = cv2.medianBlur(roi, 31)
    paper = bg.astype(np.int32).mean(axis=2)
    m = lum < paper - 28
    # red against this paper (the evening paper itself is orange): clearly redder than the zone's usual
    rg = r - g
    red = (rg - np.median(rg) > 35) & (lum < paper - 10)
    if kind == 'title':
        m |= red
    else:
        m &= ~red
        # ink stands on paper; the badge, its ribbon and the banner stand on red and gold
        sat = cv2.cvtColor(bg, cv2.COLOR_BGR2HSV)[..., 1].astype(np.int32)
        m &= sat < np.median(sat) + 45
    # the sheet's border lines: near its left or right edge, columns red almost all the way down the zone
    colf = red.mean(axis=0)
    for c in np.nonzero(colf > 0.6)[0]:
        ax = x0 + c
        if ax < sx0 + 0.12 * w or ax > sx1 - 0.12 * w:
            m[:, max(c - 2, 0):c + 3] = False
    m = m.astype(np.uint8)
    if kind == 'title':
        n, lab, st, _ = cv2.connectedComponentsWithStats(m, connectivity=8)
        keep = np.zeros(n, bool)
        cx1 = core[2] - x0
        for i in range(1, n):
            comp = lab == i
            inside = comp[:, :cx1].sum()
            keep[i] = inside >= 0.5 * st[i, cv2.CC_STAT_AREA]
        m = keep[lab].astype(np.uint8)
    m = cv2.dilate(m * 255, np.ones((3, 3), np.uint8), iterations=2)
    full = np.zeros(img.shape[:2], np.uint8)
    full[y0:y1, x0:x1] = m
    return full, core


# the header plank between its two wooden rails, measured at its letter-free ends:
# (x, y of the upper rail's lower edge, y of the lower rail's upper edge) on the left and on the right
PLANK = {
    'day':     ((354, 75, 183), (1242, 187, 280)),
    'day2':    ((354, 83, 198), (1260, 196, 296)),
    'evening': ((345, 82, 192), (1240, 192, 280)),
    'morning': ((354, 91, 202), (1236, 208, 300)),
    'night':   ((400, 80, 184), (1220, 182, 278)),
}


def plank_lines(name):
    (xl, tl, bl), (xr, tr, br) = PLANK[name]
    kt = (tr - tl) / (xr - xl)
    kb = (br - bl) / (xr - xl)
    return (lambda x: tl + kt * (x - xl)), (lambda x: bl + kb * (x - xl))


def plank_fill(out, img, name):
    """the letters are almost as tall as the plank, so the plank is rebuilt from itself: straightened into a
    rectangle, every letter pixel is filled from the same plank row to its left and right (the grain and the
    light run along the plank), and the rectangle is laid back"""
    (xl, _, _), (xr, _, _) = PLANK[name]
    top, bot = plank_lines(name)
    XL, XR = xl - 20, xr + 20
    W, HN = XR - XL, 112
    uu, vv = np.meshgrid(np.arange(W, dtype=np.float32), np.arange(HN, dtype=np.float32))
    sx = XL + uu
    t, b = top(sx) + 3, bot(sx) - 3                     # keep the rails
    sy = t + vv / (HN - 1) * (b - t)
    rect = cv2.remap(img, sx, sy.astype(np.float32), cv2.INTER_LINEAR)
    hsv = cv2.cvtColor(rect, cv2.COLOR_BGR2HSV).astype(np.int32)
    lum = rect.astype(np.int32).mean(axis=2)
    warm = ((hsv[..., 0] <= 35) | (hsv[..., 0] >= 170)) & (hsv[..., 1] > 85) & (hsv[..., 2] > 70)
    ref = np.percentile(lum, 70, axis=1, keepdims=True)
    dark = lum < ref - 38
    m = (warm | dark).astype(np.uint8) * 255
    m = cv2.morphologyEx(m, cv2.MORPH_OPEN, np.ones((3, 3), np.uint8))
    m = cv2.dilate(m, np.ones((7, 9), np.uint8))
    valid = (m == 0).astype(np.float32)
    rf = rect.astype(np.float32)
    fill = rf.copy()
    have = valid.copy()
    for sig in (5, 12, 26, 55, 110):
        kl = int(sig * 3) * 2 + 1
        ws = cv2.GaussianBlur(valid, (kl, 5), sig, sigmaY=1.2)
        vs = cv2.GaussianBlur(rf * valid[..., None], (kl, 5), sig, sigmaY=1.2)
        est = vs / np.maximum(ws, 1e-4)[..., None]
        take = (ws > 0.2) & (have == 0)
        fill[take] = est[take]
        have[take] = 1
    if (have == 0).any():
        ws = cv2.GaussianBlur(valid, (0, 0), 30); vs = cv2.GaussianBlur(rf * valid[..., None], (0, 0), 30)
        est = vs / np.maximum(ws, 1e-4)[..., None]
        fill[have == 0] = est[have == 0]
    # a little along-the-grain texture so the fill is not glassy
    rng = np.random.RandomState(3)
    grain = cv2.GaussianBlur(rng.normal(0, 6, (HN, W)).astype(np.float32), (41, 1), 12, sigmaY=0.5)
    fill = fill + grain[..., None] * (1 - valid[..., None])
    # lay it back: for every picture pixel inside the band, where it is in the rectangle
    Y0, Y1 = int(min(top(XL), top(XR))), int(max(bot(XL), bot(XR))) + 1
    gx, gy = np.meshgrid(np.arange(XL, XR, dtype=np.float32), np.arange(Y0, Y1, dtype=np.float32))
    t, b = top(gx) + 3, bot(gx) - 3
    v = (gy - t) / (b - t) * (HN - 1)
    inside = (v >= 0) & (v <= HN - 1)
    back = cv2.remap(fill, gx - XL, v.astype(np.float32), cv2.INTER_LINEAR, borderMode=cv2.BORDER_REPLICATE)
    mb = cv2.remap(m.astype(np.float32), gx - XL, v.astype(np.float32), cv2.INTER_LINEAR, borderMode=cv2.BORDER_CONSTANT)
    mb = cv2.GaussianBlur(mb * inside, (5, 5), 0)[..., None] / 255
    out = out.copy()
    roi = out[Y0:Y1, XL:XR].astype(np.float32)
    out[Y0:Y1, XL:XR] = np.clip(roi * (1 - mb) + back * mb, 0, 255).astype(np.uint8)
    full = np.zeros(out.shape[:2], np.uint8)
    full[Y0:Y1, XL:XR] = (mb[..., 0] * 255).astype(np.uint8)
    return out, full


# the exit sign: the white frame around «ВСЕГДА ГОТОВ!» (outer edge of its line) and the white panel of «ВЫХОД»
SIGN = {
    'day':     ((1529, 761, 1643, 806), (1530, 812, 1645, 857)),
    'day2':    ((1547, 781, 1666, 830), (1550, 833, 1665, 880)),
    'evening': ((1516, 754, 1631, 806), (1522, 814, 1634, 860)),
    'morning': ((1533, 776, 1648, 830), (1534, 834, 1651, 875)),
    'night':   ((1500, 739, 1616, 787), (1500, 789, 1619, 834)),
}


def region_fill(out, img, rect, letters):
    """fill the letter pixels of a flat painted area from the rest of that area (normalized convolution),
    so its light and shade stay; letters(roi) -> bool mask"""
    x0, y0, x1, y1 = rect
    roi = img[y0:y1, x0:x1]
    m = letters(roi.astype(np.int32)).astype(np.uint8) * 255
    m = cv2.dilate(m, np.ones((3, 3), np.uint8), iterations=1)
    valid = (m == 0).astype(np.float32)
    rf = roi.astype(np.float32)
    fill = rf.copy()
    have = valid.copy()
    for sig in (2.5, 5, 10, 20, 40):
        ws = cv2.GaussianBlur(valid, (0, 0), sig)
        vs = cv2.GaussianBlur(rf * valid[..., None], (0, 0), sig)
        est = vs / np.maximum(ws, 1e-4)[..., None]
        take = (ws > 0.25) & (have == 0)
        fill[take] = est[take]
        have[take] = 1
    soft = cv2.GaussianBlur(m, (3, 3), 0).astype(np.float32)[..., None] / 255
    o = out.copy()
    o[y0:y1, x0:x1] = np.clip(o[y0:y1, x0:x1].astype(np.float32) * (1 - soft) + fill * soft, 0, 255).astype(np.uint8)
    full = np.zeros(out.shape[:2], np.uint8)
    full[y0:y1, x0:x1] = m
    return o, full


def sign_fill(out, img, name):
    (fx0, fy0, fx1, fy1), (px0, py0, px1, py1) = SIGN[name]

    def pale(r):
        # light letters on the red: clearly brighter than the darkest red nearby (strokes are ~6 px)
        lum = r.mean(axis=2).astype(np.float32)
        return lum > cv2.erode(lum, np.ones((15, 15), np.uint8)) + 34

    def reddish(r):
        # red letters on the white panel: clearly darker than the brightest white nearby
        lum = r.mean(axis=2).astype(np.float32)
        return lum < cv2.dilate(lum, np.ones((15, 15), np.uint8)) - 38

    out, m1 = region_fill(out, img, (fx0 + 5, fy0 + 7, fx1 - 5, fy1 - 7), pale)
    out, m2 = region_fill(out, img, (px0 + 2, py0 + 3, px1 - 2, py1 - 5), reddish)
    return out, (m1, m2)


def clean(name):
    img = cv2.imread(os.path.join(MENU, f'board_{name}.jpg'))
    mask = np.zeros(img.shape[:2], np.uint8)
    boxes = {}
    s = SPOTS[name]
    elems = {}
    for key, zones in ZONES.items():
        for kind, f in zones.items():
            em, box = sheet_mask(img, s[key], ZONE_FIX.get((name, key, kind), f), kind)
            boxes[f'{key}.{kind}'] = box
            mask |= em
            elems[f'{key}.{kind}'] = em
    # the header letters: the middle of the plank
    hx0, hy0, hx1, hy1 = s['help']
    boxes['help'] = [hx0, hy0, hx1, hy1]
    boxes['help.plank'] = PLANK[name]
    # the stool plate: its upper board; the exit sign: its lower half («ВСЕГДА ГОТОВ! / ВЫХОД»)
    sx0, sy0, sx1, sy1 = s['settings']
    sb = (sx0 + 90, sy0 + 20, sx1 - 6, sy0 + 58)
    boxes['settings'] = sb
    em = np.zeros(mask.shape, np.uint8)
    em[sb[1]:sb[3], sb[0]:sb[2]] = text_mask(img, sb, 'plate')
    mask |= em
    elems['settings'] = em
    boxes['quit.frame'], boxes['quit.panel'] = SIGN[name]
    out = cv2.inpaint(img, mask, 6, cv2.INPAINT_TELEA)
    out, pm = plank_fill(out, img, name)
    out, sm = sign_fill(out, img, name)
    elems['quit.frame'], elems['quit.panel'] = sm
    # a light paper grain over the inpainted places, so they are not flat
    rng = np.random.RandomState(7)
    grain = rng.normal(0, 3.2, out.shape).astype(np.float32)
    soft = cv2.GaussianBlur(mask, (9, 9), 0).astype(np.float32)[..., None] / 255
    out = np.clip(out.astype(np.float32) + grain * soft, 0, 255).astype(np.uint8)
    cv2.imwrite(os.path.join(MENU, f'board_{name}_clean.jpg'), out, [cv2.IMWRITE_JPEG_QUALITY, 90])
    # where each text stood and its colour (the core of its letters), for qml/BoardText.qml
    text = {}
    for k, em in elems.items():
        ys, xs = np.nonzero(em)
        if len(xs) < 30:
            continue
        px = img[ys, xs].astype(np.int32)
        lum = px.mean(axis=1)
        light = k in ('quit.frame', 'settings')           # light letters on a dark ground
        cut = np.percentile(lum, 85 if light else 15)
        sel = lum >= cut if light else lum <= cut
        b, g, r = np.median(px[sel], axis=0)
        # where the letters themselves stand: the strongest third of the text pixels
        cut3 = np.percentile(lum, 66 if light else 34)
        s3 = lum >= cut3 if light else lum <= cut3
        x0, x1 = np.percentile(xs[s3], [1, 99]); y0, y1 = np.percentile(ys[s3], [1, 99])
        if k.endswith('.title'):
            # the title line only: around the middle of its letters (the red rule above it is thin), caps ~30 px
            yc = float(np.median(ys[s3]))
            y0, y1 = yc - 16, yc + 16
            cols = xs[s3][(ys[s3] >= y0) & (ys[s3] <= y1)]
            x0, x1 = np.percentile(cols, [1, 99])
        text[k] = dict(box=[int(x0), int(y0), int(x1), int(y1)], color='#%02x%02x%02x' % (int(r), int(g), int(b)))
    top, bot = plank_lines(name)
    (xl, _, _), (xr, _, _) = PLANK[name]
    hm = cv2.inRange(cv2.cvtColor(img, cv2.COLOR_BGR2HSV), (12, 110, 150), (30, 255, 255))
    band = np.zeros(mask.shape, np.uint8)
    for x in range(xl, xr):
        band[int(top(x)) + 6:int(bot(x)) - 6, x] = 255
    gold = img[(hm > 0) & (band > 0)]
    b, g, r = np.median(gold, axis=0)
    text['help'] = dict(left=[xl, round(top(xl), 1), round(bot(xl), 1)], right=[xr, round(top(xr), 1), round(bot(xr), 1)],
                        color='#%02x%02x%02x' % (int(r), int(g), int(b)))
    boxes['text'] = text
    return boxes, mask


if __name__ == '__main__':
    allboxes = {}
    for n in SPOTS:
        allboxes[n], m = clean(n)
        if '--show' in sys.argv:
            cv2.imwrite(os.path.join(ROOT, 'work', f'menu_mask_{n}.png'), m)
        print('clean', n)
    json.dump(allboxes, open(os.path.join(ROOT, 'work', 'menu_text_boxes.json'), 'w'), indent=1)
