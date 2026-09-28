"""Snapshot the OLD GenryModEngine builder.py as the reference oracle for GenryBL V1.

Writes tests/golden/:
  lines.json      every alias x argument variants through compile_line (fresh state each)
  stories/*.txt   story sources, *.rpy the old compile_story output (or *.error)
  tables.json     RENAMES, SPEAKERS, EFFECTS, ... for the C++ table check
and src/core/LegacyHeader.inc (the fixed .rpy header as C string arrays).

Run:  py -3 tools/make_golden.py
"""
import io
import json
import os
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
V1 = os.path.abspath(os.path.join(HERE, ".."))
OLD = r"E:\SteamLibrary\steamapps\common\Everlasting Summer\GenryModEngine"
GOLD = os.path.join(V1, "tests", "golden")

# empty engine root: find_custom_images() must not walk the 20 GB asset folder
FAKE_ROOT = tempfile.mkdtemp(prefix="genry_oracle_")
os.environ["GENRY_MOD_ENGINE_ROOT"] = FAKE_ROOT
os.environ.pop("GENRY_DECLARE_ALL_ASSETS", None)
sys.path.insert(0, OLD)
import builder  # noqa: E402
import selfcheck_core  # noqa: E402

assert builder.ASSETS_DIR.startswith(FAKE_ROOT), builder.ASSETS_DIR

ARGS = {
    "bg": ["ext_camp_entrance_day", "ext_camp_entrance_day dissolve", "black fade", "White", "int_dining_hall_day none", ""],
    "showbg": ["ext_square_day dissolve", "black", ""],
    "cg": ["d1_food_normal dissolve", "d3_un_dance", ""],
    "scene": ["bg ext_road_day", ""],
    "show": ["dv smile pioneer", "dv smile pioneer center", "dv smile pioneer at left dissolve", "un normal dress right none",
             "genry_custom", "black", "blink", "prologue_dream", "", "sl smile pioneer far fleft moveinleft", "genry_x at center"],
    "hide": ["dv", "dv dissolve", "un none", ""],
    "walk": ["dv smile pioneer left right 2.5", "dv smile pioneer badpos worse abc", "un normal pioneer fleft fright", "a b c", "x left right 2"],
    "bigshow": ["dv smile pioneer", "dv smile pioneer left 1.4 dissolve", "dv scared pioneer center cleft 3.0 0.45 0.58",
                "dv scared pioneer center cleft 0 0.45 0.58", "center 1.5", "", "genry_a fright 2"],
    "mirror": ["dv smile pioneer", "un shy pioneer left 0.9 fade", "", "1.2", "sl normal pioneer none"],
    "mirrorbig": ["dv smile pioneer right", "blink"],
    "conflictfocus": ["dv angry pioneer | un scared pioneer", "dv angry pioneer | un scared pioneer | right | left | 1.3 | 0.8 | fade",
                      "one", "a | b | bad | worse | x | y | zz"],
    "fullheight": ["dv smile pioneer | right | 1.3 | auto | 0.9 | fade", "dv smile pioneer right 1.3 mirror 0.8 dissolve", "sl normal pioneer",
                   "un smile pioneer | cright | x | normal", "", "mi smile pioneer | left | 1.1 | yes", "us grin sport flip", "genry_a 0.7"],
    "crowdshow": ["dv smile pioneer | un smile pioneer | sl smile pioneer", "a|b|c|d|e|f|g", "a|b|c|d|e|f|g|h|i|j|k|l", "", "a|b|c|d|e"],
    "enterleft": ["dv smile pioneer center 1.5", "dv smile pioneer left 1.5 1.2", "a b", "x zz 2"],
    "enterright": ["un normal pioneer right 2", "a"],
    "zoomshow": ["cg d1_food 3 1.4 0.4 0.6", "dv a b", "img abc 1.2 x y"],
    "dim": ["", "0.7", "abc"],
    "undim": [""],
    "exitleft": ["dv smile pioneer 1.2", "dv smile pioneer 1.2 0.9", "x"],
    "exitright": ["un 2", "a b c"],
    "pulse": ["dv smile pioneer", "dv smile pioneer left 0.2 1.1", "dv 0.3", ""],
    "timeofday": ["день", "вечер", "ночь", "пролог", "Night", "утро", "закат", "abc", ""],
    "eyesclose": ["", "1.5", "0.3", "abc"],
    "eyesopen": ["", "1.5", "0.3"],
    "eyesblink": ["", "0.2 0.4", "1 2", "x", "0.5"],
    "sleepyeyes": [""],
    "stopsleepyeyes": [""],
    "ghostmove": ["un normal pioneer left right 3 0.4 1.1", "x a b 1 2 3", "short", "dv q w e r t y"],
    "flash": ["", "0.3", "z"],
    "titlecard": ["Глава 1", ""],
    "creditsroll": ["Конец | 20 | 40 | fade", "\"Спасибо\\nза игру\"", "", "Текст | abc", "a | 5 | 30 | nope"],
    "note": ["Текст заметки", "\"В кавычках\"", ""],
    "monologue": ["Думаю", ""],
    "diary": ["Запись дня"],
    "memorynote": ["Помню это", "'одинарные'"],
    "bigtext": ["КРУПНО"],
    "notedim": ["", "0.8", "2", "-1", "x"],
    "notebg": ["bg ext_road_day dissolve", "cg d1 fade", "black", "white none", "bg black", "something else dissolve", ""],
    "notenvl": [""],
    "noteadv": [""],
    "closenote": [""],
    "staticfx": ["", "0.5", "1", "0.9 fade", "dissolve", "none", "2"],
    "noisefx": ["", "0.4 dissolve"],
    "glitchfx": ["", "0.6"],
    "vhsfx": ["", "1.0 fade"],
    "memoryfx": ["", "0.3"],
    "dreamfx": ["", "0.8 fade", "none"],
    "pixelfx": ["", "0.3"],
    "clearfx": [""],
    "dream": ["", "fade", "none", "xyz"],
    "stopdream": ["", "fade", "none"],
    "chapterpng": ["genry_day1 | 1 | Первый день", "img | 2 | Title | back | 5 | 0.5 | 1.0 | -0.2 | 0.3 | 0.6", "a | b",
                   "img | x | | b | q | 0.8 | 1.1"],
    "music": ["everlasting_summer", "lightness radio fadein 3 loop", "x fadeout 1 noloop", "fadein", "", "a FADEIN 4"],
    "musicfile": ["audio/track.ogg", "audio/x.ogg fadein 1 loop", "mods/other/a.ogg", ""],
    "musicqueue": ["a | b | c fadein 1 loop", "one", ""],
    "soundqueue": ["sfx/a.ogg | sound/sfx/b.ogg | c.ogg loop", "", "mods/x.ogg"],
    "sound": ["sfx_achievement", "\"sound/sfx/door.ogg\"", "u'x.ogg'", ""],
    "voice": ["v_intro", ""],
    "soundfile": ["audio/s.ogg", ""],
    "voicefile": ["audio/v.ogg"],
    "voicedsay": ["Алиса | audio/v1.ogg | Привет | мир", "dv audio/v.ogg Текст тут", "a b", "Неизвестный | a.ogg | t"],
    "punchedsay": ["Алиса | hpunch | Эй!", "dv vpunch Стой", "me xx Текст", "a b"],
    "ambience": ["ambience_camp", "sound/ambience/x.ogg fadein 1", "", "mods/a.ogg"],
    "windowhide": ["", "dissolve", "none", "xyz"],
    "windowshow": ["", "fade"],
    "nvlstart": ["", "fade", "none", "xyz"],
    "nvlend": ["", "fade", "none"],
    "stopmusic": [""],
    "stopsound": [""],
    "stopambience": [""],
    "stopallaudio": [""],
    "stop": ["", "музыка", "звук", "атмосфера", "видео", "всё", "все", "music", "стопмузыка", "channelx", "Музыка громко", "all", "video"],
    "hideall": [""],
    "effect": ["", "fade", "none", "xyz"],
    "shake": ["", "hpunch", "vpunch", "1.0 сильно", "0.2 слабо", "vertical", "5 strong", "abc", "0.75", "HPUNCH"],
    "video": ["video/intro.webm", ""],
    "videobg": ["video/bg.webm dissolve", "", "fade"],
    "stopvideobg": ["", "fade"],
    "chapter": ["", "days", "days 2", "days 3 Новый день"],
    "timeskip": ["", "3 дня", "Прошла неделя"],
    "achievement": ["images/ach.png 5", "images/ach.png", ""],
    "map": ["ext_house_of_mt_day: dorm_scene @dv, ext_square_day -> square", "a:b chibi un normal, bad", "", "z: My Scene"],
    "zone": ["house dorm", "x"],
    "chibi": ["house dv", "x"],
    "disablezone": ["", "house"],
    "resetzone": ["", "house"],
    "setvar": ["love 5", "x", "Любовь 3"],
    "addvar": ["love 1", "x"],
    "ifjump": ["love >= 3 -> good_end", "bad"],
    "jump": ["good_end", "Хорошая Концовка", ""],
    "callscene": ["side_scene", ""],
    "return": [""],
    "endgame": [""],
    "pause": ["", "2.5", "abc"],
    "renpy": ["$ x = 1"],
    "unlockgallery": ["cg01", "", "Мой CG"],
    "itemget": ["key | Ключ от дома", "apple", ""],
    "ifitem": ["key -> has_key", "bad", " -> x"],
    "replayunlock": ["scene1 | Первая сцена", "s2", ""],
    "persistentvar": ["seen_end True", "x"],
    "persistentadd": ["lp_dv 1", "x"],
    "weather": ["снег", "дождь dissolve", "стоп", "стоп fade", "unknown", "", "none", "Листья"],
    "notify": ["Текст", "Текст | Заголовок | images/ic.png | #223344 | слева | 4", "a | | | | сверху | x", ""],
    "unlockachievement": ["first | Первое", "second | Второе | images/a.png | 2", "", "x | | | "],
    "musicplayer": ["Лето | audio/summer.ogg | Гроза | audio/storm.ogg", "only", "", "a | b | c"],
    "phonestart": ["Алиса", ""],
    "sms": ["Алиса: Привет", "я: Иду", "без двоеточия", "+: ok", ": пусто", "ME: hi"],
    "phoneend": [""],
    "floatingthought": ["Мысль | 0.3 | 0.2 | 2", "Просто", "", "x | a | b | c", "t | 1e-7 | 12345678901234567 | 0.1"],
    "hidethought": ["", "dissolve", "xyz"],
    "colorfilter": ["сепия", "ночь fade", "нет", "", "abc", "none dissolve", "ЧБ"],
    "ifpersistent": ["lp_dv >= 1 -> good", "flag -> x", "bad", " -> y"],
    "say": ["Привет \"мир\" \\ слэш", ""],
    "screenmenu": [
        "Меню лагеря | style neon | bg ext_square_day | music everlasting_summer | logo images/logo.png | sprite dv smile pioneer right"
        " | subtitle Подзаг | hint Подсказка | footer Низ | side left | dim 0.5 | alpha 0.7 | width 600 | title_size 44"
        " | button #111111 #222222 #333333 | panel center #000000 #ffffff 0.9 #121212 #343434 | Пойти -> go_scene | Остаться -> stay",
        "Просто", "", "T | music audio/m.ogg | персонаж un smile x 0.4 | стиль blood | лого logo_img | dim 5 | width 10",
        "T | фон cg d1 | sprite a fright | panel 0.3 | Кнопка -> Сцена Два | -> x | y ->"],
    "label": ["Scene One", "start", ""],
    "character": ["gg Главный #ff0000"],
    "variable": ["love 0"],
    "choice": [""],
    "endchoice": [""],
}

EXTRA_LINES = [
    "Алиса: Привет", "Шрам: да", "unknown guy: text", "http://x: y", "https://a.b/c", "фон: x", "  Генри:   отступ  ",
    ": My Label", ":start", ":", "# comment", "", "   ", "pioneer2 test", "показать dv smile pioneer2 center",
    "НЕИЗВЕСТНО что-то", "Показать dv smile pioneer", "ФОН ext_road_day", "Семён: я", "виола: ой", "Ольга Дмитриевна: утро",
]


def compile_line(line, mod_id="genry_golden"):
    state = {"speakers": {}}
    try:
        return {"line": line, "out": builder.compile_line(line, mod_id, state)}
    except Exception as exc:  # noqa: BLE001
        return {"line": line, "error": "%s: %s" % (type(exc).__name__, exc)}


def build_lines():
    by_cmd = {}
    for alias, cmd in builder.RENAMES.items():
        by_cmd.setdefault(cmd, []).append(alias)
    cases = []
    for cmd, variants in ARGS.items():
        aliases = by_cmd.get(cmd, [])
        words = [cmd] + aliases        # the English id always works too
        for rest in variants:
            cases.append(compile_line((words[0] + " " + rest).rstrip()))
            if len(words) > 1:
                cases.append(compile_line((words[1] + " " + rest).rstrip()))
        for alias in aliases[1:]:
            cases.append(compile_line((alias + " " + variants[0]).rstrip()))
    missing = sorted(set(builder.RENAMES.values()) - set(ARGS))
    assert not missing, "no ARGS for: %s" % missing
    for line in EXTRA_LINES:
        cases.append(compile_line(line))
    return cases


STORIES = {
    "eyes_closed": "закрытьглаза\nфон ext_road_day dissolve\nпоказать dv smile pioneer fade\nпоказать un normal pioneer with dissolve\n"
                   "переход next\n: next\nфон ext_square_day fade\nоткрытьглаза\nтекст ok\n",
    "eyes_close_return": "закрытьглаза\nфон ext_road_day dissolve\nконец\n: after\nфон ext_road_day fade\n",
    "self_jump": ": finish\nтекст a\nпереход finish\nфон next_room\nтекст b\n",
    "self_jump_empty": ": a\nтекст\nпереход a\n: b\nтекст\n",
    "self_jump_twice": ": s\nтекст\nпереход s\nтекст 2\n: s_next\nтекст x\n: t\nпереход t\nтекст y\n",
    "literal_return": ": a\nтекст\nreturn\n",
    "eyesopen_before_jump": "открытьглаза\n\nпереход x\n: x\nтекст\n",
    "static_after_bg": "фон ext_road_day fade\nпомехи 0.5\nтекст\nсменитьфон ext_square_day dissolve\nvhs\nфон a\nшум fade\n",
    "prelude_labels": "фон x\nтекст пролог\n: first\nтекст\n: second\nтекст\n",
    "prelude_terminal": "фон x\nпереход second\n: first\nтекст\n: second\nтекст\n",
    "labels_only": ": first\nтекст\n: first_next\nтекст\n",
    "comment_prelude": "# просто коммент\n: first\nтекст\n",
    "own_label": "@mod_id mymod\n: mymod\nтекст\n: other\nтекст\n",
    "start_label": ": start\nтекст\nпереход start\n",
    "characters": "персонаж gg Главный #ff0000\nперсонаж x\nперсонаж\nпеременная love 0\nпеременная\nпеременная love 0\n"
                  "gg: Привет\nГлавный: снова\nтекст\nкрик gg | hpunch | Эй\nозвученнаяреплика главный | a.ogg | т\n",
    "choice": "выбор\n- Да -> yes_scene\n- Нет -> no scene\n\n# c\nконецвыбора\n: yes_scene\nтекст да\n",
    "choice_unclosed": "текст\nвыбор\n- A -> a\n- B -> b\n",
    "choice_bad": "выбор\n- без стрелки\nконецвыбора\n",
    "screenmenu_targets": "экранменю Хаб | Раз -> one | Два -> two\n: one\nтекст\n",
    "map_targets": "карта house: dorm, square -> sq\n",
    "meta_title": "@mod_id Title Mod!\n@mod_name Мой \"мод\"\n@author Я\n@mod_title_font fonts/x.ttf\n@mod_title_color #ff0000\n"
                  "@mod_title_size 40\n@unknown_key z\nтекст\n",
    "meta_title_mods": "@mod_title_font mods/other/f.ttf\n@mod_title_color red\n@mod_title_size 4000\nтекст\n",
    "empty": "# только коммент\n",
    "phone_story": "телефонначать Алиса\nсмс Алиса: Ты где?\nсмс я: Иду\nсмс Алиса: Жду\nтелефонконец\nтелефонначать\nсмс я: снова\n",
    "call_story": "вызов side\nтекст после\n: side\nтекст внутри\nконец\n",
    "endgame_story": ": a\nтекст\nконецигры\n: b\nтекст\n",
    "autojump_choice": "выбор\n- A -> a\nконецвыбора\n: a\nтекст\n: a_next\nтекст\n",
    "eyes_carry": "закрытьглаза\nпереход far\n: near\nфон x fade\n: far\nфон y dissolve\nпоказать dv fade\nоткрытьглаза\nфон z fade\n",
}


def main():
    os.makedirs(os.path.join(GOLD, "stories"), exist_ok=True)
    lines = build_lines()
    with io.open(os.path.join(GOLD, "lines.json"), "w", encoding="utf-8") as f:
        json.dump(lines, f, ensure_ascii=False, indent=0)

    stories = dict(STORIES)
    for i, check in enumerate(selfcheck_core.CHECKS):
        stories["selfcheck_%02d" % i] = check[1]
    everything = [": everything"]
    for case in lines:
        if "error" in case:
            continue
        w = builder.normalize_command(builder.first_word(case["line"]))
        if w in ("choice", "endchoice", "character", "variable", "label"):
            continue
        everything.append(case["line"])
    stories["everything"] = "\n".join(everything) + "\n"

    for name, src in stories.items():
        text = (src if src.lstrip().startswith("@") else u"@mod_id genry_golden\n@mod_name Golden\n" + src)
        with io.open(os.path.join(GOLD, "stories", name + ".txt"), "w", encoding="utf-8", newline="\n") as f:
            f.write(text)
        meta, body = builder.parse_meta(text.splitlines())
        err_path = os.path.join(GOLD, "stories", name + ".error")
        rpy_path = os.path.join(GOLD, "stories", name + ".rpy")
        for p in (err_path, rpy_path):
            if os.path.exists(p):
                os.remove(p)
        try:
            rpy = builder.compile_story(meta, body)
            with io.open(rpy_path, "w", encoding="utf-8", newline="\n") as f:
                f.write(rpy)
        except Exception as exc:  # noqa: BLE001
            with io.open(err_path, "w", encoding="utf-8", newline="\n") as f:
                f.write("%s: %s" % (type(exc).__name__, exc))
        meta_out = dict(meta)
        meta_out["playable"] = builder.has_playable_body(body)
        with io.open(os.path.join(GOLD, "stories", name + ".meta.json"), "w", encoding="utf-8") as f:
            json.dump(meta_out, f, ensure_ascii=False, indent=1)

    tables = {
        "RENAMES": builder.RENAMES,
        "SPEAKERS": builder.SPEAKERS,
        "EFFECTS": sorted(builder.EFFECTS),
        "POSITIONS": sorted(builder.POSITIONS),
        "WALK_XALIGNS": builder.WALK_XALIGNS,
        "COMMAND_NAMES": sorted(builder.COMMAND_NAMES),
        "GENRY_SPRITE_PREFIXES": sorted(builder.GENRY_SPRITE_PREFIXES),
        "GENRY_WEATHER": {k: list(v) for k, v in builder.GENRY_WEATHER.items()},
        "GENRY_WEATHER_ORDER": list(builder.GENRY_WEATHER),
        "GENRY_WEATHER_ALIASES": builder.GENRY_WEATHER_ALIASES,
        "GENRY_WEATHER_STOP": sorted(builder.GENRY_WEATHER_STOP),
        "GENRY_FILTERS": builder.GENRY_FILTERS,
        "GENRY_FILTER_ORDER": list(builder.GENRY_FILTERS),
        "GENRY_FILTER_ALIASES": builder.GENRY_FILTER_ALIASES,
        "GENRY_FILTER_STOP": sorted(builder.GENRY_FILTER_STOP),
    }
    with io.open(os.path.join(GOLD, "tables.json"), "w", encoding="utf-8") as f:
        json.dump(tables, f, ensure_ascii=False, indent=1, sort_keys=True)

    write_legacy_header()
    print("golden: %d lines, %d stories" % (len(lines), len(stories)))


def c_array(name, lines):
    out = ["static const char* const %s[] = {" % name]
    for line in lines:
        out.append("    %s," % json.dumps(line, ensure_ascii=False))
    out.append("    nullptr,")
    out.append("};")
    return out


def write_legacy_header():
    meta = {"mod_id": "zzmodidzz", "mod_name": "ZZNAME", "author": "a", "mod_title_font": "", "mod_title_color": "", "mod_title_size": ""}
    out = builder.compile_story(meta, [u"# x"]).split("\n")
    i_init = out.index("init:")
    assert out[i_init + 1] == u'    $ mods["zzmodidzz"] = u"ZZNAME"', out[i_init + 1]
    j = out.index("", i_init)                 # blank line before the screens
    k = out.index("label zzmodidzz:")
    head_a = out[:i_init + 1]
    head_b = [l.replace("zzmodidzz", "@@MODID@@") for l in out[i_init + 2:j]]
    head_c = out[j:k]
    assert all("zzmodidzz" not in l for l in head_a + head_c)
    # prove the split: reassembly with the dynamic mods line must give the old header
    again = head_a + [out[i_init + 1]] + [l.replace("@@MODID@@", "zzmodidzz") for l in head_b] + head_c
    assert again == out[:k]
    lines = ["// GENERATED by tools/make_golden.py from the old GenryModEngine builder.py. Do not hand-edit.",
             "// .rpy header = kHeadA + mods line + kHeadB(@@MODID@@) + init lines + custom images + kHeadC",
             "#pragma once"]
    lines += c_array("kHeadA", head_a) + c_array("kHeadB", head_b) + c_array("kHeadC", head_c)
    with io.open(os.path.join(V1, "src", "core", "LegacyHeader.inc"), "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(lines) + "\n")


if __name__ == "__main__":
    main()
