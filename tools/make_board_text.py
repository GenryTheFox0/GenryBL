"""Write where every word of the menu board stands (and its colour) into qml/BoardText.qml, between
// <place> and // </place>, from work/menu_text_boxes.json (tools/make_menu_clean.py measures it).

    python tools/make_menu_clean.py && python tools/make_board_text.py
"""
import io, json, math, os, re, sys
import cv2
import numpy as np

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, 'tools'))
from make_menu_clean import SPOTS

# the ink of a sheet keeps off its red border lines: fractions of the sheet width
BODY_X = (0.095, 0.905)


def camera_bottom(name, sheet):
    """the lowest row of the camera in the gallery sheet's upper right corner (the ink must start below it)"""
    img = cv2.imread(os.path.join(ROOT, 'data', 'menu', f'board_{name}_clean.jpg'))   # no ink to mistake
    x0, y0, x1, y1 = sheet
    w, h = x1 - x0, y1 - y0
    roi = img[y0:y0 + int(0.42 * h), x0 + int(0.55 * w):x0 + int(0.93 * w)].astype(np.int32).mean(axis=2)
    rows = np.nonzero((roi < 85).sum(axis=1) >= 6)[0]
    return y0 + int(rows.max()) if len(rows) else y0


def main():
    b = json.load(io.open(os.path.join(ROOT, 'work', 'menu_text_boxes.json'), encoding='utf-8'))
    out = []
    for n in ('day', 'day2', 'evening', 'morning', 'night'):
        t, z = b[n]['text'], b[n]
        (xl, tl, bl), (xr, tr, br) = t['help']['left'], t['help']['right']
        cyl, cyr = (tl + bl) / 2, (tr + br) / 2
        ang = math.degrees(math.atan2(cyr - cyl, xr - xl))
        length = math.hypot(xr - xl, cyr - cyl)
        h = ((bl - tl) + (br - tr)) / 2
        parts = [f'help: [{(xl + xr) / 2:.0f}, {(cyl + cyr) / 2:.1f}, {length - 40:.0f}, {h:.0f}, {ang:.2f}, "{t["help"]["color"]}"]']
        for k in ('new', 'mods', 'gallery'):
            sx0, _, sx1, _ = SPOTS[n][k]
            w = sx1 - sx0
            tb, zone, bb = t[k + '.title']['box'], z[k + '.title'], t[k + '.body']['box']
            bx0 = max(bb[0], round(sx0 + BODY_X[0] * w))
            bx1 = min(bb[2], round(sx0 + BODY_X[1] * w))
            by0 = bb[1]
            if k == 'gallery':
                by0 = max(by0, camera_bottom(n, SPOTS[n][k]) + 8)
            parts.append(f'{k}: {{ t: [{round(sx0 + 0.07 * w)}, {tb[1]}, {zone[2] - 6}, {tb[3]}], tc: "{t[k + ".title"]["color"]}", '
                         f'b: [{bx0}, {by0}, {bx1}, {bb[3]}], bc: "{t[k + ".body"]["color"]}" }}')
        for k, nm in (('settings', 'plate'), ('quit.frame', 'frame'), ('quit.panel', 'panel')):
            bx = t[k]['box']
            parts.append(f'{nm}: [{bx[0]}, {bx[1]}, {bx[2]}, {bx[3]}, "{t[k]["color"]}"]')
        out.append(f'        {n}: {{\n            ' + ',\n            '.join(parts) + '\n        }')
    lit = '    readonly property var place: ({\n' + ',\n'.join(out) + '\n    })\n'
    p = os.path.join(ROOT, 'qml', 'BoardText.qml')
    s = io.open(p, encoding='utf-8').read()
    if '// <place>' not in s:
        sys.exit('no // <place> markers in BoardText.qml')
    s2 = re.sub(r'(    // <place>[^\n]*\n).*?(    // </place>\n)', lambda m: m.group(1) + lit + m.group(2), s, flags=re.S)
    io.open(p, 'w', encoding='utf-8', newline='\n').write(s2)
    print('BoardText.qml: 5 pictures placed')


if __name__ == '__main__':
    main()
