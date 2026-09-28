"""Rejoin C++ string literals that got split by a real newline (shell heredoc escape loss)."""
import io
import sys

BS = chr(92)


def open_quote(line):
    n, esc = 0, False
    for ch in line:
        if esc:
            esc = False
            continue
        if ch == BS:
            esc = True
            continue
        if ch == '"':
            n += 1
    return n % 2 == 1


p = sys.argv[1]
lines = io.open(p, encoding='utf-8').read().split('\n')
out, i = [], 0
while i < len(lines):
    cur = lines[i]
    while open_quote(cur) and i + 1 < len(lines):
        i += 1
        cur = cur + BS + 'n' + lines[i]
    out.append(cur)
    i += 1
io.open(p, 'w', encoding='utf-8', newline='\n').write('\n'.join(out))
