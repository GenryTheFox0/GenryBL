"""Collect every string GenryBL shows, for data/i18n/<code>.json.

    python tools/i18n_extract.py            -> data/i18n/_source.json (the Russian originals, sorted)
                                              + a report of what each language still misses

Sources: qsTr("…") in qml/*.qml, gbTr("…") / QCoreApplication::translate("GenryBL", "…") in src/**,
and the words people see in data/forms.json (command titles, help, field labels, choices).
"""
import glob, io, json, os, re, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CYR = re.compile(r'[А-Яа-яЁё]')


def unescape(s):
    return s.encode('latin-1', 'backslashreplace').decode('unicode_escape') if '\\' in s else s


def qml_strings():
    out = set()
    rx = re.compile(r'qsTr\("((?:[^"\\]|\\.)*)"\)')
    for f in glob.glob(os.path.join(ROOT, 'qml', '*.qml')):
        for m in rx.finditer(io.open(f, encoding='utf-8').read()):
            s = m.group(1)
            if '\\' in s:
                s = s.replace('\\n', '\n').replace('\\"', '"').replace('\\\\', '\\')
            out.add(s)
    return out


def cpp_strings():
    out = set()
    rx = re.compile(r'(?:gbTr|translate\("GenryBL",\s*)\(?"((?:[^"\\]|\\.)*)"')
    for f in glob.glob(os.path.join(ROOT, 'src', '**', '*.*'), recursive=True):
        if not f.endswith(('.cpp', '.h', '.inc')):
            continue
        for m in rx.finditer(io.open(f, encoding='utf-8', errors='replace').read()):
            s = m.group(1).replace('\\n', '\n').replace('\\"', '"')
            out.add(s)
    return out


FORM_KEYS = {'title', 'short', 'help', 'label', 'hint', 'cat', 'none', 'add', 'sub'}


def forms_strings():
    out = set()
    data = json.load(io.open(os.path.join(ROOT, 'data', 'forms.json'), encoding='utf-8'))

    def walk(x, key=None):
        if isinstance(x, dict):
            for k, v in x.items():
                walk(v, k)
        elif isinstance(x, list):
            # [value, label] pairs of a choice: only the label is words
            if len(x) == 2 and all(isinstance(t, str) for t in x) and key in ('opts', 'options', 'choices', 'values'):
                if CYR.search(x[1]):
                    out.add(x[1])
                return
            for v in x:
                walk(v, key)
        elif isinstance(x, str) and key in FORM_KEYS and CYR.search(x):
            out.add(x)
    walk(data)
    return out


if __name__ == '__main__':
    q, c, f = qml_strings(), cpp_strings(), forms_strings()
    src = sorted(q | c | f)
    d = os.path.join(ROOT, 'data', 'i18n')
    os.makedirs(d, exist_ok=True)
    json.dump(src, io.open(os.path.join(d, '_source.json'), 'w', encoding='utf-8'), ensure_ascii=False, indent=0)
    print(f'qml {len(q)}  cpp {len(c)}  forms {len(f)}  -> {len(src)} strings, {sum(len(s) for s in src)} chars')
    for p in sorted(glob.glob(os.path.join(d, '*.json'))):
        name = os.path.basename(p)
        if name.startswith('_'):
            continue
        tr = json.load(io.open(p, encoding='utf-8'))
        miss = [s for s in src if s not in tr]
        print(f'  {name:12} {len(src) - len(miss)}/{len(src)}')
