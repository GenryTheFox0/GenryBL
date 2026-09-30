"""Build data/i18n/<code>.json from a translation list against data/i18n/_source.json.

    python tools/i18n_merge.py <code> <list.txt> [<list2.txt> …]

list.txt: one line per string, «<id>|<translation>» (ids from _source.json), \\n = a line break.
The leading / trailing spaces and line breaks of the Russian original are put back around the translation,
so a translator never has to count them; %1, %2 … must all be there or the line is refused.
Lines of an already existing <code>.json that the list does not cover are kept.
"""
import io, json, os, re, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PH = re.compile(r'%\d')
# story text / demo / patterns that must stay Russian: never translated
SKIP_IF = re.compile(r'^(@mod_id|выбор\n|НАТ\. |ИНТ\. |АЛИСА\n|- Позвать)|\n\s*Славя: |\nконецвыбора|naked\|nude')


def main():
    code, lists = sys.argv[1], sys.argv[2:]
    src = json.load(io.open(os.environ.get("I18N_SNAPSHOT") or os.path.join(ROOT, "data", "i18n", "_source.json"), encoding="utf-8"))
    out_path = os.path.join(ROOT, 'data', 'i18n', code + '.json')
    out = json.load(io.open(out_path, encoding='utf-8')) if os.path.exists(out_path) else {}
    bad, n = [], 0
    for lp in lists:
        for raw in io.open(lp, encoding='utf-8').read().split('\n'):
            if not raw.strip() or raw.startswith('#'):
                continue
            i, _, t = raw.partition('|')
            if not i.strip().isdigit():
                bad.append('no id: ' + raw[:60]); continue
            i = int(i)
            if i >= len(src):
                bad.append(f'{i}: out of range'); continue
            s = src[i]
            if SKIP_IF.search(s):
                continue
            t = t.replace('\\n', '\n').strip()
            if not t:
                continue
            if sorted(PH.findall(s)) != sorted(PH.findall(t)):
                bad.append(f'{i}: placeholders {PH.findall(s)} vs {PH.findall(t)}'); continue
            lead = s[:len(s) - len(s.lstrip())]
            trail = s[len(s.rstrip()):]
            out[s] = lead + t + trail
            n += 1
    out = {k: v for k, v in out.items() if k in set(src)}
    json.dump(dict(sorted(out.items())), io.open(out_path, 'w', encoding='utf-8'), ensure_ascii=False, indent=1)
    print(f'{code}: {n} lines merged, {len(out)}/{len(src)} strings')
    for b in bad[:40]:
        print('  !', b)


if __name__ == '__main__':
    main()
