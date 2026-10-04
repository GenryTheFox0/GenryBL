"""The wardrobe lists that ship with GenryBL: data/wardrobe_hidden.txt («Удалённые») and data/wardrobe_faces.txt («поправить лицо»).

Collects them from every place they were made on this PC - the dev build writes to data/ itself, an installed copy
(the test of what people get) and the old dev work/ keep their own - and merges them into data/. make_release.py runs it.
"""
import io
import json
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SOURCES = [os.path.join(ROOT, 'work'),
           os.path.join(os.environ.get('LOCALAPPDATA', ''), 'Programs', 'GenryBL', 'work')]


def lines(path):
    if not os.path.isfile(path):
        return []
    return [l.strip() for l in io.open(path, encoding='utf-8-sig').read().splitlines() if l.strip()]


def main():
    data = os.path.join(ROOT, 'data')
    hidden = set(lines(os.path.join(data, 'wardrobe_hidden.txt')))
    faces = {}
    for l in lines(os.path.join(data, 'wardrobe_faces.txt')):
        c = l.split('\t')
        if len(c) == 3:
            faces[c[0]] = l
    before = len(hidden), len(faces)
    # the game's own sprites never ship hidden (an old list once took every emotion but «smile» off Ольга Дмитриевна;
    # older copies still carry it): «tag|look:emotion outfit» of a sprite the catalog has stays out
    game = set()
    for name in json.load(io.open(os.path.join(data, 'es_catalog.json'), encoding='utf-8')).get('sprites', {}):
        w = name.split()
        if len(w) >= 2:
            game.add(w[0] + '|look:' + ' '.join(w[1:]))
    for src in SOURCES:
        hidden.update(l for l in lines(os.path.join(src, 'wardrobe_hidden.txt')) if l not in game)
        for l in lines(os.path.join(src, 'wardrobe_faces.txt')):
            c = l.split('\t')
            if len(c) == 3:
                faces[c[0]] = l
    io.open(os.path.join(data, 'wardrobe_hidden.txt'), 'w', encoding='utf-8', newline='\n').write(''.join(k + '\n' for k in sorted(hidden)))
    io.open(os.path.join(data, 'wardrobe_faces.txt'), 'w', encoding='utf-8', newline='\n').write(''.join(faces[k] + '\n' for k in sorted(faces)))
    print(f'   wardrobe lists: deleted {before[0]} -> {len(hidden)}, face fixes {before[1]} -> {len(faces)}')


if __name__ == '__main__':
    sys.exit(main())
