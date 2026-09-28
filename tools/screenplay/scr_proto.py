# -*- coding: utf-8 -*-
"""Prototype of GenryBL V1 «пиши как сценарий» (Goal A expander + Goal B converter).
Mirrors the proposed src/core/Screenplay.cpp so the tables and the test expectations are verified
against the real ES sprite/background catalog. Run: py -3 scr_proto.py"""
import json, re, sys, copy
sys.stdout.reconfigure(encoding='utf-8')
from scr_tables import *

import os
CAT = json.load(open(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'data', 'es_catalog.json'), encoding='utf-8'))
SPRITES = set(CAT['sprites'])
BGS = set(k[3:] for k in CAT['images'] if k.startswith('bg '))
SPRITE_TAGS = sorted(set(k.split()[0] for k in SPRITES))
# kSpeakers (Tables.inc)
K_SPEAKERS = {'алиса': 'dv', 'виола': 'cs', 'виолетта': 'cs', 'вожатая': 'mt', 'генри': 'genry', 'дв': 'dv', 'женя': 'mz',
              'лена': 'un', 'медсестра': 'cs', 'мику': 'mi', 'ольга': 'mt', 'ольга дмитриевна': 'mt', 'сем': 'me',
              'семен': 'me', 'семён': 'me', 'славя': 'sl', 'славяна': 'sl', 'сём': 'me', 'ульяна': 'us', 'уля': 'us',
              'ун': 'un', 'шрам': 'scar', 'шурик': 'sh', 'электроник': 'el', 'юля': 'uv', 'я': 'me'}
ES_IDS = {'dv', 'sl', 'un', 'us', 'mi', 'mt', 'el', 'sh', 'mz', 'uv', 'cs', 'me', 'pi', 'th', 'narrator', 'genry', 'scar'}
COMMANDS = {'фон', 'показать', 'убрать', 'текст', 'смс', 'время', 'звонок', 'фото', 'выбор', 'конецвыбора', 'переход',
            'музыка', 'звук', 'крик', 'голос', 'если', 'сцена', 'пауза', 'цг', 'скачок', 'титр'}   # enough for tests


def norm(s):
    return s.lower().replace('ё', 'е').strip()


# ------------------------------------------------------------------ lexicon
class Entry:
    def __init__(self, pat, cat, val):
        self.words = norm(pat).split()
        self.cat, self.val = cat, val
        self.score = (len(self.words), sum(len(w.rstrip('*')) for w in self.words), sum(1 for w in self.words if not w.endswith('*')))


LEX = []
for p, c in EMO_WORDS: LEX.append(Entry(p, 'emo', c))
for p, c in OUTFIT_WORDS: LEX.append(Entry(p, 'outfit', c))
for p, c in ACC_WORDS: LEX.append(Entry(p, 'acc', c))
for p, c in DIST_WORDS: LEX.append(Entry(p, 'dist', c))
for p, c in POS_PHRASES: LEX.append(Entry(p, 'pos', c))
for p in ENTER_WORDS: LEX.append(Entry(p, 'enter', 'fast' if p in FAST_EXIT else ''))
for p in EXIT_WORDS: LEX.append(Entry(p, 'exit', 'fast' if p in FAST_EXIT else ''))
for p in OFFSCREEN: LEX.append(Entry(p, 'offscreen', 1))
for p in THOUGHT: LEX.append(Entry(p, 'thought', 1))
for p in PUNCH: LEX.append(Entry(p, 'punch', 1))
for p in IGNORE: LEX.append(Entry(p, 'ignore', 1))
for p in STRONG_WORDS: LEX.append(Entry(p, 'strong', 1))
for p in WEAK_WORDS: LEX.append(Entry(p, 'weak', 1))
for p in NEGATE: LEX.append(Entry(p, 'neg', 1))
STOP = {'в', 'во', 'на', 'с', 'со', 'и', 'а', 'но', 'же', 'уже', 'снова', 'опять', 'тоже', 'еще', 'так', 'как', 'вдруг',
        'одета', 'одет', 'одетая', 'одетый', 'стоит', 'стоя', 'сидит', 'сидя', 'вся', 'весь', 'своей', 'своем', 'своих',
        'по-прежнему', 'все', 'еще', 'будто', 'словно', 'явно', 'тон', 'тоном', 'голосом', 'взглядом', 'лицом', 'видом',
        'глазами', 'сама', 'сам', 'и', 'к', 'от', 'из', 'за', 'до', 'для', 'при', 'ей', 'ему', 'нам', 'мне', 'нему', 'ней'}


def wmatch(pat, tok):
    return tok.startswith(pat[:-1]) if pat.endswith('*') else tok == pat


def lex_at(toks, i):
    best = None
    for e in LEX:
        n = len(e.words)
        if i + n > len(toks): continue
        if all(wmatch(p, toks[i + k]) for k, p in enumerate(e.words)):
            if best is None or e.score > best.score: best = e
    return best


def tokens(s):
    t = norm(s)
    t = re.sub(r'[;/]+', ',', t)
    return re.findall(r"[a-zа-я0-9][a-zа-я0-9.'\-]*|,", t)


class Mods:
    def __init__(self):
        self.emo = None; self.outfit = None; self.acc_add = set(); self.acc_del = set(); self.dist = None
        self.pos = None; self.enter = False; self.exit = False; self.dir = None; self.fast = False
        self.offscreen = False; self.thought = False; self.punch = False; self.unknown = []; self.raw_attrs = []
        self.emos = []; self.positions = []


def parse_mods(text, tag=None):
    m = Mods()
    toks = tokens(text)
    tag_attrs = set()
    if tag:
        for k in SPRITES:
            w = k.split()
            if w[0] == tag: tag_attrs.update(w[1:])
    i, strength, neg = 0, 0, False
    outfits = []
    while i < len(toks):
        t = toks[i]
        if t == ',': strength, neg = 0, False; i += 1; continue
        if tag and t in tag_attrs:            # power user: raw ES attributes «(angry swim close)»
            m.raw_attrs.append(t); i += 1; continue
        e = lex_at(toks, i)
        if e is None:
            if t not in STOP and not t.isdigit(): m.unknown.append(t)
            i += 1; continue
        n = len(e.words)
        c = e.cat
        if c == 'strong': strength = 1
        elif c == 'weak': strength = -1
        elif c == 'neg': neg = True
        elif c == 'emo':
            if not neg:
                v = e.val
                if strength > 0: v = STRONGER.get(v, v)
                if strength < 0: v = WEAKER.get(v, v)
                m.emos.append(v)
            neg = False; strength = 0
        elif c == 'outfit':
            if not neg: outfits.append(e.val)
            neg = False
        elif c == 'acc':
            (m.acc_del if neg else m.acc_add).add(e.val); neg = False
        elif c == 'dist': m.dist = e.val
        elif c == 'pos': m.positions.append(e.val)
        elif c in ('enter', 'exit'):
            setattr(m, c, True)
            if e.val == 'fast': m.fast = True
            j = i + n
            if j < len(toks):
                for p, d in DIRECTION_WORDS:
                    if toks[j] == norm(p): m.dir = d; n += 1; break
        elif c == 'offscreen': m.offscreen = True
        elif c == 'thought': m.thought = True
        elif c == 'punch': m.punch = True
        i += n
    if m.emos:
        s = set(m.emos)
        m.emo = 'crysmile' if ('cry' in s and ('smile' in s or 'smile2' in s or 'happy' in s)) else m.emos[-1]
    if outfits:
        m.outfit = sorted(outfits, key=lambda o: OUTFIT_PRIORITY.index(o))[0]
    if m.positions: m.pos = m.positions[-1]
    if m.pos is None and m.dir and m.enter: m.pos = m.dir
    return m


# ------------------------------------------------------------------ sprite resolver
def compose(tag, emo, acc, outfit, dist):
    parts = [tag, emo] + (['panama'] if 'panama' in acc and tag == 'mt' else []) + \
            (['glasses'] if 'glasses' in acc else []) + (['stethoscope'] if 'stethoscope' in acc else []) + \
            ([outfit] if outfit else []) + ([dist] if dist and dist != 'normal' else [])
    return ' '.join(parts)


def tag_outfits(tag):
    s = set()
    for k in SPRITES:
        w = k.split()
        if w[0] == tag:
            for a in w[1:]:
                if a in ('pioneer', 'pioneer2', 'swim', 'body', 'dress', 'sport'): s.add(a)
    return s


def resolve(tag, mods, cur, notes):
    """cur = {'emo','outfit','acc','dist'} of the character (sticky look). Returns (image, newlook)."""
    look = dict(cur)
    chain = list(CHAINS.get(mods.emo, [mods.emo])) if mods.emo else [look['emo']]
    raw = [a for a in mods.raw_attrs]
    for a in raw:
        if a in ('pioneer', 'pioneer2', 'swim', 'body', 'dress', 'sport'): mods.outfit = a
        elif a in ('close', 'far'): mods.dist = a
        elif a in ('panama', 'glasses', 'stethoscope'): mods.acc_add.add(a)
        else: chain = [a]
    if chain[-1] != 'normal': chain.append('normal')
    acc = (set(look['acc']) | mods.acc_add) - mods.acc_del
    dist = mods.dist if mods.dist else look['dist']
    want_outfit = mods.outfit or look['outfit']
    groups = []
    if want_outfit: groups.append(OUTFIT_GROUPS.get(want_outfit, [want_outfit]))
    if look['outfit'] and OUTFIT_GROUPS.get(look['outfit'], [look['outfit']]) not in groups:
        groups.append(OUTFIT_GROUPS.get(look['outfit'], [look['outfit']]))
    d0 = DEFAULT_LOOK.get(tag, ('', ''))[0]
    if [d0] not in groups and OUTFIT_GROUPS.get(d0, [d0]) not in groups: groups.append(OUTFIT_GROUPS.get(d0, [d0]))
    groups.append([''])
    for gi, grp in enumerate(groups):
        for e in chain:
            for o in grp:
                for a in ([acc, acc - {'panama'}, set()] if acc else [set()]):
                    for d in ([dist, 'normal'] if dist and dist != 'normal' else ['normal']):
                        name = compose(tag, e, a, o, d)
                        if name in SPRITES:
                            if gi > 0 and mods.outfit:
                                notes.append(('info', 'нет одежды «%s» у %s — оставил «%s»' % (mods.outfit, tag, o or '-')))
                            elif mods.emo and e != chain[0] and chain[0] in CHAINS.get(mods.emo, []):
                                notes.append(('info', '%s: нет «%s» — взял «%s»' % (tag, compose(tag, chain[0], acc, want_outfit, dist), name)))
                            look.update(emo=e, outfit=o, acc=sorted(a), dist=d)
                            return name, look
    return None, look


# ------------------------------------------------------------------ backgrounds
def bg_for(base, time):
    order = {'day': ['day', 'sunset', 'night'], 'sunset': ['sunset', 'day', 'night'],
             'night': ['night', 'night2', 'night_without_light', 'sunset', 'day'], 'prolog': ['night', 'sunset', 'day']}[time]
    if base in BGS: return base, base != base   # timeless (int_mine, semen_room, ext_bus by day)
    for t in order:
        if base + '_' + t in BGS: return base + '_' + t, t != order[0]
    return None, False


def parse_heading(line):
    s = line.strip().strip('*#').strip()
    s = re.sub(r'^(?:СЦЕНА|Сцена|сцена|SCENE|Scene)\s*\d+\s*[.:)]?\s*', '', s)
    s = re.sub(r'^\d+\s*[.)]\s+', '', s)
    m = re.match(r'^' + KIND_RE + r'\.?\s+(.+)$', s, re.I)
    kind = None
    if m:
        k = norm(s[:m.start(1)])
        kind = 'int' if k.startswith(('инт', 'int', 'i/e')) else 'ext'
        rest = m.group(1)
    else:
        if not (s.upper() == s and re.search('[А-ЯA-Z]', s) and re.search(r'\s[—–-]{1,2}\s', s)): return None
        rest = s
    parts = [p.strip(' .') for p in re.split(r'\s*[—–]\s*|\s+-{1,2}\s+|\s*,\s*|\.\s+', rest) if p.strip(' .')]
    if not parts: return None
    time = None
    place_parts = []
    for p in parts:
        tn = norm(p)
        tv = None
        for w, v in sorted(TIME_WORDS, key=lambda x: -len(x[0])):
            if tn == w or tn.startswith(w + ' '): tv = v; break
        if tv and place_parts: time = tv
        else: place_parts.append(p)
    if not m and time is None: return None
    place_text = ' '.join(place_parts)
    toks = tokens(place_text)
    best, bestscore = None, None
    for pat, key in PLACE_WORDS:
        words = norm(pat).split()
        for i in range(len(toks) - len(words) + 1):
            if all(wmatch(w, toks[i + k]) for k, w in enumerate(words)):
                sc = (len(words), sum(len(w.rstrip('*')) for w in words))
                if bestscore is None or sc > bestscore: best, bestscore = key, sc
    return {'kind': kind, 'place': best, 'place_text': place_text, 'time': time}


# ------------------------------------------------------------------ state
class State:
    def __init__(self):
        self.look = {}        # tag -> dict(emo, outfit, acc, dist)
        self.pos = {}         # tag -> (pos, auto)
        self.onscreen = []
        self.time = 'day'
        self.snap = {}


def look_of(st, tag):
    if tag not in st.look:
        o, a = DEFAULT_LOOK.get(tag, ('', ''))
        st.look[tag] = {'emo': 'normal', 'outfit': o, 'acc': [a] if a else [], 'dist': 'normal'}
    return st.look[tag]


def observe(line, st):
    s = line.strip()
    if not s: return
    if s.startswith(':'):
        name = s[1:].strip()
        if name in st.snap: st.onscreen, st.pos = copy.deepcopy(st.snap[name])
        return
    w = s.split()
    c = w[0].lower()
    if c in ('фон', 'цг', 'скачок', 'bg', 'cg', 'scene', 'timeskip', 'убратьвсех', 'hideall', 'день', 'видеофон'):
        st.onscreen, st.pos = [], {}
    elif c in ('показать', 'show') and len(w) > 1 and w[1] in SPRITE_TAGS:
        tag = w[1]
        args = [x for x in w[2:] if x not in ('dissolve', 'dspr', 'fade', 'moveinleft', 'moveinright', 'none', 'at')]
        p = [x for x in args if x in ('left', 'right', 'center', 'fleft', 'fright', 'cleft', 'cright', 'truecenter')]
        attrs = [x for x in args if x not in p]
        lk = look_of(st, tag)
        if attrs:
            lk['emo'] = attrs[0]
            lk['acc'] = [a for a in attrs[1:] if a in ('panama', 'glasses', 'stethoscope')]
            outs = [a for a in attrs[1:] if a in ('pioneer', 'pioneer2', 'swim', 'body', 'dress', 'sport')]
            lk['outfit'] = outs[0] if outs else ''
            lk['dist'] = 'close' if 'close' in attrs else 'far' if 'far' in attrs else 'normal'
        if p: st.pos[tag] = (p[0], st.pos.get(tag, ('', False))[1] if False else False)
        if tag not in st.onscreen: st.onscreen.append(tag)
    elif c in ('убрать', 'hide', 'выходслева', 'выходсправа') and len(w) > 1:
        if w[1] in st.onscreen: st.onscreen.remove(w[1])
        st.pos.pop(w[1], None)
    elif c in ('переход', 'jump') and len(w) > 1:
        st.snap.setdefault(w[1], copy.deepcopy((st.onscreen, st.pos)))
    elif s.startswith('-') and '->' in s:
        st.snap.setdefault(s.split('->', 1)[1].split('|')[0].strip(), copy.deepcopy((st.onscreen, st.pos)))


SLOTS = {1: ['center'], 2: ['cleft', 'cright'], 3: ['left', 'center', 'right'], 4: ['fleft', 'cleft', 'cright', 'fright']}


def speaker_tag(name, ctx):
    n = norm(name)
    for d in (ctx.get('speakers', {}), K_SPEAKERS, EXTRA_SPEAKERS):
        if n in d: return d[n]
        if n.replace('е', 'ё') in d: return d[n.replace('е', 'ё')]
    if n in ES_IDS: return n
    return None


HEAD_RE = re.compile(r'^(?P<name>[^():|#@\-\s][^():|]{0,60}?)\s*\((?P<mods>[^()]*)\)\s*(?:(?P<colon>:)\s*(?P<text>.*?))?\s*\.?$', re.S)


def expand(line, st, ctx=None, notes=None):
    """-> list of story commands, or None (not a screenplay line; the caller keeps it and observe()s it)"""
    ctx = ctx or {}
    notes = notes if notes is not None else []
    s = line.strip()
    if not s or s[0] in ':#@-': observe(s, st); return None
    first = norm(s.split()[0])
    if first in COMMANDS: observe(s, st); return None
    if norm(s).strip(' .!') in [norm(b).strip(' .') for b in BLACK_WORDS]:
        out = ['фон black fade']
        for l in out: observe(l, st)
        return out
    h = parse_heading(s)
    if h:
        if not h['place']:
            notes.append(('warn', 'место «%s» не знаю — фон не сменится' % h['place_text']))
            return ['# ' + s]
        dk, ext, intb = PLACES[h['place']]
        kind = h['kind'] or dk
        base = (intb or ext) if kind == 'int' else (ext or intb)
        t = st.time if h['time'] in (None, 'same') else h['time']
        bg, fb = bg_for(base, t)
        if fb: notes.append(('info', 'у «%s» нет времени «%s» — взят %s' % (h['place_text'], t, bg)))
        out = ['время ' + TIME_RU[t], 'фон %s fade' % bg]
        st.time = t
        for l in out: observe(l, st)
        return out
    m = HEAD_RE.match(s)
    if not m: observe(s, st); return None
    name, modtext, text = m.group('name').strip(), m.group('mods'), m.group('text')
    names = [x.strip() for x in re.split(r'\s+и\s+|\s*,\s*', name)]
    tags = [speaker_tag(x, ctx) for x in names]
    if not all(tags):
        if not m.group('colon') or not (name.upper() == name and len(name.split()) <= 3): observe(s, st); return None
    out, after = [], []
    for nm, tag in zip(names, tags):
        mods = parse_mods(modtext, tag if tag in SPRITE_TAGS else None)
        if mods.unknown: notes.append(('warn', 'в скобках не понял: ' + ', '.join(mods.unknown)))
        if tag not in SPRITE_TAGS or mods.offscreen or mods.thought:
            if tag and tag not in SPRITE_TAGS and (mods.emo or mods.outfit or mods.pos):
                notes.append(('info', '%s: спрайта в БЛ нет — только реплика' % nm))
            continue
        shown = tag in st.onscreen
        if mods.exit and not (mods.emo or mods.outfit or mods.dist):
            fx = {'left': 'moveoutleft', 'right': 'moveoutright'}.get(mods.dir, 'dissolve')
            after.append('убрать %s %s' % (tag, fx))
            if not shown: notes.append(('info', '%s уходит, но её не было на экране' % nm))
            continue
        if not (mods.emo or mods.outfit or mods.acc_add or mods.acc_del or mods.dist or mods.pos or mods.enter or mods.raw_attrs) and shown:
            if mods.exit: after.append('убрать %s dissolve' % tag)
            continue
        cur = look_of(st, tag)
        img, newlook = resolve(tag, mods, cur, notes)
        if img is None: notes.append(('warn', 'нет подходящего спрайта для ' + nm)); continue
        pos = mods.pos
        fx = 'dspr' if shown else 'dissolve'
        if mods.enter and not shown:
            fx = {'left': 'moveinleft', 'right': 'moveinright'}.get(mods.dir, 'dissolve')
        if not shown and not pos:        # auto layout: re-slot the auto-placed ones
            autos = [t for t in st.onscreen if st.pos.get(t, ('', True))[1]]
            fixed = [st.pos[t][0] for t in st.onscreen if not st.pos.get(t, ('', True))[1]]
            n = len(autos) + 1
            slots = [x for x in SLOTS.get(n, SLOTS[4]) if x not in fixed] or ['center']
            for t, sl in zip(autos, slots):
                if st.pos.get(t, ('', True))[0] != sl:
                    out.append('показать %s %s' % (compose(t, st.look[t]['emo'], set(st.look[t]['acc']), st.look[t]['outfit'], st.look[t]['dist']), sl))
                    st.pos[t] = (sl, True)
            pos = slots[len(autos)] if len(autos) < len(slots) else slots[-1]
            out.append('показать %s %s %s' % (img, pos, fx))
            st.look[tag] = newlook
            if tag not in st.onscreen: st.onscreen.append(tag)
            st.pos[tag] = (pos, True)
        else:
            out.append('показать %s%s %s' % (img, (' ' + pos) if pos else '', fx))
            st.look[tag] = newlook
            if tag not in st.onscreen: st.onscreen.append(tag)
            if pos: st.pos[tag] = (pos, False)
        if mods.exit:
            after.append('убрать %s %s' % (tag, {'left': 'moveoutleft', 'right': 'moveoutright'}.get(mods.dir, 'dissolve')))
    if m.group('colon'):
        tag0 = tags[0] if len(tags) == 1 else None
        who = CANON_NAME.get(tag0, name) if tag0 else (name.title() if name.upper() == name else name)
        allm = parse_mods(modtext)
        if allm.thought and tag0 == 'me': out.append('th: ' + text)
        elif allm.thought: out.append('%s: {i}%s{/i}' % (who, text))
        elif allm.punch: out.append('крик %s | hpunch | %s' % (who, text))
        else: out.append('%s: %s' % (who, text))
    for l in after:
        if l.split()[1] in st.onscreen: st.onscreen.remove(l.split()[1])
        st.pos.pop(l.split()[1], None)
    return out + after


# ------------------------------------------------------------------ tests (Goal A)
def run_case(lines, ctx=None):
    st = State()
    out, notes = [], []
    for l in lines:
        r = expand(l, st, ctx, notes)
        out += r if r is not None else [l]
    return out, notes


A_CASES = [
    (['Алиса (злая, слева): Ну и чего ты встал?'],
     ['показать dv angry pioneer left dissolve', 'Алиса: Ну и чего ты встал?']),
    (['АЛИСА (смущённо, в купальнике): Не смотри так!'],
     ['показать dv shy body center dissolve', 'Алиса: Не смотри так!']),
    (['Славя (улыбается, справа): Привет!', 'Славя (уходит)'],
     ['показать sl smile pioneer right dissolve', 'Славя: Привет!', 'убрать sl dissolve']),
    (['ИНТ. СТОЛОВАЯ — ВЕЧЕР'], ['время вечер', 'фон int_dining_hall_sunset fade']),
    (['НАТ. ПЛЯЖ — ДЕНЬ'], ['время день', 'фон ext_beach_day fade']),
    (['НАТ. ЛОДОЧНАЯ СТАНЦИЯ — ВЕЧЕР'], ['время вечер', 'фон ext_boathouse_day fade']),
    (['ИНТ. ДОМИК ВОЖАТОЙ — НОЧЬ'], ['время ночь', 'фон int_house_of_mt_night fade']),
    (['СЦЕНА 3. НАТ. ПЛОЩАДЬ, НОЧЬ'], ['время ночь', 'фон ext_square_night fade']),
    (['смс Славя: Ты где?'], ['смс Славя: Ты где?']),
    (['Алиса: Просто реплика (без скобок у имени).'], ['Алиса: Просто реплика (без скобок у имени).']),
    (['Лена (плачет): Уйди...'], ['показать un cry pioneer center dissolve', 'Лена: Уйди...']),
    (['Лена (улыбается сквозь слёзы): Спасибо.'], ['показать un cry_smile pioneer center dissolve', 'Лена: Спасибо.']),
    (['Ульяна (удивлённо): Чего?!'], ['показать us surp1 pioneer center dissolve', 'Ульяна: Чего?!']),
    (['Ульяна (испуганно): Ай!'], ['показать us fear pioneer center dissolve', 'Ульяна: Ай!']),
    (['Юля (злая): Фр-р.'], ['показать uv rage center dissolve', 'Юля: Фр-р.']),
    (['Ольга Дмитриевна (строго, в панаме): Семён!'],
     ['показать mt normal panama pioneer center dissolve', 'Ольга Дмитриевна: Семён!']),
    (['Ольга Дмитриевна (смеётся, в купальнике): Вода тёплая!'],
     ['показать mt smile swim center dissolve', 'Ольга Дмитриевна: Вода тёплая!']),
    (['Женя (дуется): Не мешай.'], ['показать mz bukal glasses pioneer center dissolve', 'Женя: Не мешай.']),
    (['Женя (без очков, улыбается): Ой.'], ['показать mz smile pioneer center dissolve', 'Женя: Ой.']),
    (['Мику (в платье): Красиво?'], ['показать mi normal pioneer center dissolve', 'Мику: Красиво?']),
    (['Славя (в спортивной форме, серьёзно): Разминка!'],
     ['показать sl serious sport center dissolve', 'Славя: Разминка!']),
    (['Алиса (ухмыляется, близко): Попался.'], ['показать dv grin pioneer close center dissolve', 'Алиса: Попался.']),
    (['Алиса (очень злая): Всё!'], ['показать dv rage pioneer center dissolve', 'Алиса: Всё!']),
    (['Славя (слегка улыбается): Ну...'], ['показать sl smile2 pioneer center dissolve', 'Славя: Ну...']),
    (['Семён (думает): Опять она.'], ['th: Опять она.']),
    (['Алиса (кричит): Стой!'], ['показать dv normal pioneer center dissolve', 'крик Алиса | hpunch | Стой!']),
    (['Алиса (за кадром): Эй!'], ['Алиса: Эй!']),
    (['Алиса (входит слева)'], ['показать dv normal pioneer left moveinleft']),
    (['Алиса (слева): Раз.', 'Алиса (уходит направо)'],
     ['показать dv normal pioneer left dissolve', 'Алиса: Раз.', 'убрать dv moveoutright']),
    (['Алиса (злая): Ты!', 'Славя (улыбается): Тише.'],
     ['показать dv angry pioneer center dissolve', 'Алиса: Ты!', 'показать dv angry pioneer cleft',
      'показать sl smile pioneer cright dissolve', 'Славя: Тише.']),
    (['Алиса (злая): Ты!', 'Алиса (краснеет): ...ладно.'],
     ['показать dv angry pioneer center dissolve', 'Алиса: Ты!', 'показать dv shy pioneer dspr', 'Алиса: ...ладно.']),
    (['Алиса и Ульяна (смеются): Ха-ха!'],
     ['показать dv laugh pioneer center dissolve', 'показать dv laugh pioneer cleft',
      'показать us laugh pioneer cright dissolve', 'Алиса и Ульяна: Ха-ха!']),
    (['Незнакомка (злая): Кто здесь?'], ['Незнакомка (злая): Кто здесь?']),
    (['ВОЖАТЫЙ (строго): Отбой!'], ['Вожатый: Отбой!']),
    (['dv (angry swim close): Ну?'], ['показать dv angry body close center dissolve', 'Алиса: Ну?']),
    (['Лена (краснеет, в купальнике): ...'], ['показать un shy swim center dissolve', 'Лена: ...']),
    (['Славя (вдалеке, машет рукой)'], ['показать sl normal pioneer far center dissolve']),
    (['ЗАТЕМНЕНИЕ.'], ['фон black fade']),
    (['НАТ. ЛЕС — НОЧЬ'], ['время ночь', 'фон ext_path_night fade']),
    (['ИНТ. КУХНЯ — ДЕНЬ'], ['# ИНТ. КУХНЯ — ДЕНЬ']),
    (['Электроник (с фингалом): Я в порядке!'], ['показать el fingal pioneer center dissolve', 'Электроник: Я в порядке!']),
]


def main():
    bad = 0
    for i, (inp, want) in enumerate(A_CASES, 1):
        got, notes = run_case(inp)
        ok = got == want
        if not ok: bad += 1
        print('%s A%02d %s' % ('ok ' if ok else 'BAD', i, ' / '.join(inp)))
        if not ok:
            print('     want:', want)
            print('     got :', got)
        for n in notes: print('     note:', n)
    print('A: %d/%d' % (len(A_CASES) - bad, len(A_CASES)))


if __name__ == '__main__':
    main()
