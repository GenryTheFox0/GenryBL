"""GenryBL gate: every piece of Python in the given .rpy files must compile under Python 3 too.

Everlasting Summer has a «renpy8» branch in Steam (Ren'Py 8, Python 3) beside the usual one (Ren'Py 7, Python 2):
a mod GenryBL writes has to run on both. This pulls the Python out of the mods the gate installed - «$» lines,
python blocks, define / default values, if / elif / while / for conditions - and compiles each with Python 3.
Prints «file:line: what» for every piece that does not compile; exit 1 if any.

    python py3check.py <mod.rpy> [<mod.rpy> …]
"""
import io
import re
import sys

BLOCK = re.compile(r'^(\s*)(?:init(?:\s+[-\d]+)?\s+)?python(?:\s+early)?(?:\s+hide)?(?:\s+in\s+\w+)?\s*:\s*$')
DOLLAR = re.compile(r'^\s*\$\s?(.*)$')
DEFINE = re.compile(r'^\s*(?:define|default)\s+[\w.]+\s*=\s*(.+)$')
COND = re.compile(r'^\s*(?:if|elif|while)\s+(.+):\s*$')
FOR = re.compile(r'^\s*for\s+.+?\s+in\s+(.+):\s*$')


def depth(text):
    """open brackets left at the end of the text (strings skipped): Ren'Py lets a «$» line go on while they are open"""
    d, quote, i = 0, None, 0
    while i < len(text):
        ch = text[i]
        if quote:
            if ch == '\\':
                i += 2
                continue
            if ch == quote:
                quote = None
        elif ch in '"\'':
            quote = ch
        elif ch == '#':
            break
        elif ch in '([{':
            d += 1
        elif ch in ')]}':
            d -= 1
        i += 1
    return d


def check(path):
    bad = []
    lines = io.open(path, encoding='utf-8', errors='replace').read().split('\n')
    i = 0
    while i < len(lines):
        line = lines[i]
        m = BLOCK.match(line)
        if m:
            indent = len(m.group(1))
            body, start = [], i + 1
            j = i + 1
            while j < len(lines) and (not lines[j].strip() or len(lines[j]) - len(lines[j].lstrip()) > indent):
                body.append(lines[j])
                j += 1
            real = [b for b in body if b.strip()]
            cut = min((len(b) - len(b.lstrip()) for b in real), default=0)
            code = '\n'.join(b[cut:] if b.strip() else '' for b in body)
            try:
                compile(code, path, 'exec')
            except SyntaxError as e:
                bad.append((start + (e.lineno or 1) - 1, e.msg))
            i = j
            continue
        for rx, mode in ((DOLLAR, 'exec'), (DEFINE, 'eval'), (COND, 'eval'), (FOR, 'eval')):
            m = rx.match(line)
            if not m:
                continue
            src = m.group(1).strip()
            if not src:
                break
            # a statement Ren'Py reads on over the next lines while its brackets are open
            k = i
            while depth(src) > 0 and k + 1 < len(lines):
                k += 1
                src += '\n' + lines[k].strip()
            if rx is COND or rx is FOR:
                src = src.rstrip().rstrip(':')
            try:
                compile(src, path, mode)
            except SyntaxError as e:
                bad.append((i + 1, '%s: %s' % (e.msg, src[:80])))
            break
        i += 1
    return bad


def main():
    failed = 0
    for path in sys.argv[1:]:
        for line, what in check(path):
            print('%s:%d: Python 3: %s' % (path.replace('\\', '/'), line, what))
            failed += 1
    print('PY3 CHECKED %d files, %d problems' % (len(sys.argv) - 1, failed))
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main())
