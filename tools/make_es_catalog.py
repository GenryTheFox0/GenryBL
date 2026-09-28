"""Everlasting Summer's own image/audio vocabulary -> data/es_catalog.json.

Source: ES scripts decompiled with unrpyc into tools/es_decompiled/ (sprites.rpy,
resources.rpy, media.rpy). Every sprite keeps its exact im.Composite layer list, so
the preview draws precisely what the game draws.

Run:  py -3 tools/make_es_catalog.py
"""
import io
import json
import os
import re

V1 = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
SRC = os.path.join(V1, "tools", "es_decompiled")

IMAGE_START = re.compile(r"^\s*image\s+([^=:]+?)\s*(=|:)\s*(.*)$")
COMPOSITE = re.compile(r"im\.Composite\(\s*\((\d+)\s*,\s*(\d+)\)\s*,(.*?)\)\s*(?:,\s*im\.matrix|\)|$)", re.S)
LAYER = re.compile(r"\(\s*(-?\d+)\s*,\s*(-?\d+)\s*\)\s*,\s*\"([^\"]+)\"")
TINT = re.compile(r"im\.matrix\.tint\(\s*([0-9.]+)\s*,\s*([0-9.]+)\s*,\s*([0-9.]+)\s*\)")
OPACITY = re.compile(r"im\.matrix\.opacity\(\s*([0-9.]+)\s*\)")
QUOTED = re.compile(r"\"([^\"]+)\"")


def blocks(path):
    """image definitions with their full (multi-line) expression."""
    lines = io.open(path, encoding="utf-8").read().split("\n")
    cur = None
    for line in lines:
        m = IMAGE_START.match(line)
        indent = len(line) - len(line.lstrip())
        if m:
            if cur:
                yield cur
            cur = {"name": " ".join(m.group(1).split()), "kind": m.group(2), "expr": m.group(3), "indent": indent}
            continue
        if cur is not None:
            if line.strip() and indent <= cur["indent"] and not line.lstrip().startswith(("im.", "\"", ")")) and cur["kind"] == "=" \
                    and cur["expr"].count("(") <= cur["expr"].count(")"):
                yield cur
                cur = None
                continue
            if cur["kind"] == ":" and line.strip() and indent <= cur["indent"]:
                yield cur
                cur = None
                continue
            cur["expr"] += "\n" + line
    if cur:
        yield cur


def default_branch(expr):
    """ConditionSwitch(...): the 'True,' branch is the day version."""
    i = expr.find("True,")
    return expr[i + 5:] if "ConditionSwitch" in expr and i >= 0 else expr


def parse_sprites():
    sprites = {}
    for b in blocks(os.path.join(SRC, "sprites.rpy")):
        expr = default_branch(b["expr"])
        m = COMPOSITE.search(expr)
        if not m:
            continue
        layers = [[int(x), int(y), p] for x, y, p in LAYER.findall(m.group(3))]
        if not layers:
            continue
        item = {"w": int(m.group(1)), "h": int(m.group(2)), "layers": layers}
        tint = TINT.search(expr[m.end() - 20:]) if "MatrixColor" in expr.split("im.Composite")[0] else None
        if tint:
            item["tint"] = [float(tint.group(i)) for i in (1, 2, 3)]
        op = OPACITY.search(expr)
        if op and "MatrixColor" in expr.split("im.Composite")[0]:
            item["opacity"] = float(op.group(1))
        sprites[b["name"]] = item
    return sprites


def parse_images():
    images = {}
    for fn in ("resources.rpy", "media.rpy"):
        for b in blocks(os.path.join(SRC, fn)):
            name, expr = b["name"], b["expr"].strip()
            if b["kind"] == ":":
                q = QUOTED.search(expr)            # ATL image: first frame
                if q and re.search(r"\.(png|jpg|jpeg|webp)$", q.group(1)):
                    images[name] = {"path": q.group(1), "atl": True}
                continue
            if expr.startswith("\"#") or expr.startswith("'#"):
                images[name] = {"color": expr.strip("\"'")}
                continue
            m = re.match(r"^get_image\(\"([^\"]+)\"\)$", expr)
            if m:
                images[name] = {"path": "images/" + m.group(1)}
                continue
            m = re.match(r"^\"([^\"]+\.(?:png|jpg|jpeg|webp))\"$", expr)
            if m:
                images[name] = {"path": m.group(1)}
                continue
            m = re.search(r"get_image\(\"([^\"]+)\"\)|\"(images/[^\"]+)\"", expr)
            if m:
                item = {"path": ("images/" + m.group(1)) if m.group(1) else m.group(2)}
                if "Sepia" in expr:
                    item["sepia"] = True
                t = TINT.search(expr)
                if t:
                    item["tint"] = [float(t.group(i)) for i in (1, 2, 3)]
                images[name] = item
    return images


def parse_audio():
    text = io.open(os.path.join(SRC, "resources.rpy"), encoding="utf-8").read()
    music = dict(re.findall(r"music_list\[\"([^\"]+)\"\]\s*=\s*\"([^\"]+)\"", text))
    sounds, ambience = {}, {}
    for var, path in re.findall(r"^\s*\$\s*(\w+)\s*=\s*\"(sound/[^\"]+)\"", text, re.M):
        (ambience if var.startswith("ambience_") else sounds)[var] = path
    return music, sounds, ambience


RU_NAMES = {"me": "Семён", "dv": "Алиса", "un": "Лена", "sl": "Славя", "mi": "Мику", "us": "Ульяна",
            "mt": "Ольга Дмитриевна", "el": "Электроник", "sh": "Шурик", "mz": "Женя", "uv": "Юля", "cs": "Виола",
            "pi": "Пионер", "voice": "Голос", "my": "Я"}


def parse_characters():
    """ES Character ids with their name colour per time of day (media.rpy colors[...])."""
    text = io.open(os.path.join(SRC, "media.rpy"), encoding="utf-8").read()
    chars = {}
    for cid, body in re.findall(r"colors\[\s*'(\w+)'\s*\]\s*=\s*\{([^}]*)\}", text):
        cols = {}
        for tod, r, g, b in re.findall(r"'(\w+)':\s*\((\d+),\s*(\d+),\s*(\d+),\s*\d+\)", body):
            cols[tod] = "#%02x%02x%02x" % (int(r), int(g), int(b))
        chars[cid] = {"colors": cols, "name": RU_NAMES.get(cid, "")}
    return chars


def main():
    sprites = parse_sprites()
    images = parse_images()
    music, sounds, ambience = parse_audio()
    chars = parse_characters()
    cat = {"sprites": sprites, "images": images, "music": music, "sounds": sounds, "ambience": ambience, "characters": chars}
    print("characters:", " ".join(sorted(chars)))
    out = os.path.join(V1, "data", "es_catalog.json")
    os.makedirs(os.path.dirname(out), exist_ok=True)
    with io.open(out, "w", encoding="utf-8") as f:
        json.dump(cat, f, ensure_ascii=False, indent=0, sort_keys=True)
    bgs = [n for n in images if n.startswith("bg ")]
    cgs = [n for n in images if n.startswith("cg ")]
    chars = sorted(set(n.split()[0] for n in sprites))
    print("sprites %d (%s), bg %d, cg %d, other images %d, music %d, sfx %d, ambience %d" %
          (len(sprites), " ".join(chars), len(bgs), len(cgs), len(images) - len(bgs) - len(cgs), len(music), len(sounds), len(ambience)))
    for probe in ("dv smile pioneer", "dv smile pioneer close", "dv smile pioneer far", "mz normal glasses pioneer", "un normal pioneer red", "cs normal"):
        print(probe, "->", sprites.get(probe))
    for probe in ("bg ext_camp_entrance_day", "bg hall", "cg d1_food_normal", "prologue_dream", "blink", "anim blink_up", "bg black"):
        print(probe, "->", images.get(probe))


if __name__ == "__main__":
    main()
