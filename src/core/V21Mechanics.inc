// GenryBL V2.1 - the 7ДЛ mechanics the old GenryModEngine had written down (docs/CLAUDE_7DLCE_MECHANICS_RECON.md),
// made anew on ES's own parts. Appended to a mod only when it uses them.
//  «сейчасиграет вкл»   the track's name slides in at the top left when music starts
//  «заставкаафк 60 | cg …»   nobody clicks for a minute: a picture over the game, the sound goes quiet; a click - back
//  «статистика»         how much of the mod the player has seen: scenes, endings
//  «терминал …»         an old computer: the player types words, the mod answers or leads to a scene
//  «кнб Алиса | …»      rock-paper-scissors with a heroine, won / lost / a draw lead to their scenes
//  «экранвыхода …»      closing the game inside the mod: a farewell picture and words first
// No lambdas in the screens: a screen's own variables are not seen inside them (the code lock died of that in 2.0).
#pragma once

static const char* const kV21NowPlaying = R"rpy(
# GenryBL V2.1: «сейчас играет» - название трека всплывает слева сверху (как в 7ДЛ)
init python:
    def genry_now_playing(title):
        renpy.show_screen("genry_now_playing", title)

transform genry_np_in:
    on show:
        alpha 0.0 xoffset -40
        easein 0.5 alpha 1.0 xoffset 0
    on hide:
        easeout 0.6 alpha 0.0

screen genry_now_playing(title):
    zorder 150
    timer 3.8 action Hide("genry_now_playing")
    frame at genry_np_in:
        xpos 30
        ypos 28
        background Solid("#000000aa")
        padding (22, 12)
        vbox:
            spacing 2
            text u"Сейчас играет" size 18 color "#cfd8dc" font "fonts/calibri.ttf"
            text title size 28 color "#ffdd7d" font "fonts/calibri.ttf"
)rpy";

static const char* const kV21Afk = R"rpy(
# GenryBL V2.1: заставка, когда игрок отошёл (как в 7ДЛ): история стоит на месте N секунд - картинка поверх, звук стихает
init python:
    genry_afk_state = [None, 0]
    def genry_afk_tick(secs, img):
        here = renpy.get_filename_line()
        if here != genry_afk_state[0]:
            genry_afk_state[0] = here
            genry_afk_state[1] = 0
            return
        genry_afk_state[1] += 1
        if genry_afk_state[1] >= secs and not renpy.get_screen("genry_afk"):
            for genry_ch in ("music", "ambience", "sound"):
                try:
                    renpy.music.set_volume(0.0, delay=2.0, channel=genry_ch)
                except Exception:
                    pass
            renpy.show_screen("genry_afk", img)
            renpy.restart_interaction()

    def genry_afk_wake():
        genry_afk_state[1] = 0
        for genry_ch in ("music", "ambience", "sound"):
            try:
                renpy.music.set_volume(1.0, delay=1.5, channel=genry_ch)
            except Exception:
                pass
        renpy.hide_screen("genry_afk")
        renpy.restart_interaction()

transform genry_afk_in:
    on show:
        alpha 0.0
        linear 2.0 alpha 1.0
    on hide:
        linear 1.0 alpha 0.0

screen genry_afk_watch(secs, img):
    timer 1.0 repeat True action Function(genry_afk_tick, secs, img)

screen genry_afk(img):
    modal True
    zorder 400
    fixed at genry_afk_in:
        add Solid("#000000")
        add img xalign 0.5 yalign 0.5
        text u"Нажми, чтобы вернуться" xalign 0.5 yalign 0.95 size 26 color "#ffffffaa" font "fonts/calibri.ttf"
    button:
        xysize (1920, 1080)
        background None
        action Function(genry_afk_wake)
    key "K_SPACE" action Function(genry_afk_wake)
    key "K_RETURN" action Function(genry_afk_wake)
)rpy";

static const char* const kV21Stats = R"rpy(
# GenryBL V2.1: статистика прохождения - сколько сцен и концовок мода игрок уже видел
init python:
    def genry_stats_count(scenes):
        seen = 0
        endings = 0
        seen_end = 0
        for genry_lab, genry_name, genry_end in scenes:
            genry_s = renpy.seen_label(genry_lab)
            if genry_s:
                seen += 1
            if genry_end:
                endings += 1
                if genry_s:
                    seen_end += 1
        return (seen, len(scenes), seen_end, endings)

screen genry_stats(scenes, called=False):
    modal True
    zorder 90
    $ genry_st = genry_stats_count(scenes)
    add Solid("#000000cc")
    frame:
        xalign 0.5
        yalign 0.5
        background Solid("#0f141bf2")
        padding (44, 32)
        vbox:
            spacing 18
            text u"Статистика" size 42 color "#ffd27d" font "fonts/corbel.ttf"
            null height 4
            text (u"Сцены: %d из %d" % (genry_st[0], genry_st[1])) size 30 color "#eef6ff" font "fonts/calibri.ttf"
            bar value StaticValue(genry_st[0], max(1, genry_st[1])) xsize 620 ysize 14 left_bar Solid("#9bd35a") right_bar Solid("#ffffff1a")
            if genry_st[3]:
                text (u"Концовки: %d из %d" % (genry_st[2], genry_st[3])) size 30 color "#eef6ff" font "fonts/calibri.ttf"
                bar value StaticValue(genry_st[2], max(1, genry_st[3])) xsize 620 ysize 14 left_bar Solid("#ffd27d") right_bar Solid("#ffffff1a")
            text (u"Пройдено: %d%%" % (100 * genry_st[0] // max(1, genry_st[1]))) size 26 color "#a9b9a4" font "fonts/calibri.ttf"
            null height 8
            textbutton u"Закрыть" action (Return(True) if called else Hide("genry_stats")) xalign 0.5 text_size 28 text_color "#eef6ff" text_hover_color "#ffd27d" background None hover_background None text_font "fonts/corbel.ttf" activate_sound "sound/sfx/click_1.ogg"
)rpy";

static const char* const kV21Terminal = R"rpy(
# GenryBL V2.1: терминал - старый компьютер: игрок печатает слово, мод отвечает или ведёт в сцену
init python:
    def genry_term_find(cmds, typed):
        t = (typed or u"").strip().lower()
        for genry_c in cmds:
            if genry_c[0] == t:
                return genry_c
        return None

    def genry_terminal_run(title, hint, cmds):
        log = [hint] if hint else []
        while True:
            typed = renpy.call_screen("genry_terminal", title=title, log=log)
            if typed is None or typed == u"\x00":
                return u""
            found = genry_term_find(cmds, typed)
            log = log + [u"> " + typed]
            if found is None:
                if typed.strip():
                    log = log + [u"Такой команды нет."]
            elif found[1]:
                log = log + [found[1]]
            log = log[-30:]
            if found is not None and found[2] is not None:
                if found[1]:
                    renpy.call_screen("genry_terminal", title=title, log=log, done=True)
                return found[2]

screen genry_terminal(title, log, done=False):
    modal True
    zorder 200
    default genry_typed = u""
    add Solid("#030803")
    text title xpos 140 ypos 80 size 40 color "#7dff9a" font "fonts/calibri.ttf"
    viewport:
        xpos 140
        ypos 150
        xsize 1640
        ysize 720
        yinitial 1.0
        vbox:
            spacing 8
            for genry_l in log:
                text genry_l size 28 color "#b8ffc8" font "fonts/calibri.ttf"
    if done:
        textbutton u"[[ Дальше ]" xpos 140 ypos 900 action Return(u"") text_size 30 text_color "#7dff9a" text_hover_color "#eaffef" background None hover_background None text_font "fonts/calibri.ttf"
        key "K_RETURN" action Return(u"")
    else:
        hbox:
            xpos 140
            ypos 900
            text u"> " size 30 color "#7dff9a" font "fonts/calibri.ttf"
            input value ScreenVariableInputValue("genry_typed", returnable=True) size 30 color "#eaffef" length 48 font "fonts/calibri.ttf"
    textbutton u"Выйти" xalign 0.96 yalign 0.95 action Return(u"\x00") text_size 28 text_color "#7dff9a99" text_hover_color "#eaffef" background None hover_background None text_font "fonts/calibri.ttf"
)rpy";

static const char* const kV21Rps = R"rpy(
# GenryBL V2.1: камень-ножницы-бумага на табличке БЛ (o_rly) - до стольких побед, сколько раундов
init python:
    import random as genry_random
    genry_rps_names = [u"Камень", u"Ножницы", u"Бумага"]

    def genry_rps_round(me):
        them = genry_random.randint(0, 2)
        if me == them:
            return (them, 0)
        return (them, 1 if (me - them) % 3 == 2 else -1)

    def genry_rps_run(who, rounds):
        need = rounds // 2 + 1
        mine = 0
        theirs = 0
        last = u""
        while mine < need and theirs < need:
            me = renpy.call_screen("genry_rps", who=who, mine=mine, theirs=theirs, last=last)
            them, won = genry_rps_round(me)
            if won > 0:
                mine += 1
                last = u"%s против «%s» — твоя взяла!" % (genry_rps_names[me], genry_rps_names[them])
            elif won < 0:
                theirs += 1
                last = u"%s против «%s» — очко у соперника." % (genry_rps_names[me], genry_rps_names[them])
            else:
                last = u"Оба выбрали «%s» — ничья, ещё раз." % genry_rps_names[me]
            if rounds <= 1 and won == 0:
                renpy.call_screen("genry_rps", who=who, mine=mine, theirs=theirs, last=last, done=True)
                return "draw"
        renpy.call_screen("genry_rps", who=who, mine=mine, theirs=theirs, last=last, done=True)
        return "win" if mine > theirs else "lose"

screen genry_rps(who, mine, theirs, last=u"", done=False):
    modal True
    zorder 200
    add get_image("gui/o_rly/base.png")
    text (u"%s — %d : %d" % (who, theirs, mine)) text_align 0.5 xalign 0.5 yalign 0.415 color "#64483c" font header_font size 34
    text last text_align 0.5 xalign 0.5 yalign 0.5 color "#3f2a1d" font header_font size 28
    if done:
        textbutton u"Дальше" xalign 0.5 yalign 0.7 style "log_button" text_style "settings_link" text_size 54 action Return(True) activate_sound "sound/sfx/click_1.ogg"
    else:
        hbox:
            xalign 0.5
            yalign 0.66
            spacing 60
            textbutton u"Камень" style "log_button" text_style "settings_link" text_size 50 action Return(0) activate_sound "sound/sfx/click_1.ogg"
            textbutton u"Ножницы" style "log_button" text_style "settings_link" text_size 50 action Return(1) activate_sound "sound/sfx/click_1.ogg"
            textbutton u"Бумага" style "log_button" text_style "settings_link" text_size 50 action Return(2) activate_sound "sound/sfx/click_1.ogg"
)rpy";

static const char* const kV21Farewell = R"rpy(
# GenryBL V2.1: экран выхода - закрыл игру посреди мода: сначала прощальная картинка и слова (как в 7ДЛ)
init 990 python:
    if not getattr(store, "_genry_quit_wrapped", False):
        _genry_quit_wrapped = True
        _genry_prev_quit = config.quit_action

        def genry_quit():
            fw = getattr(store, "genry_farewell_now", None)
            if fw and not renpy.get_screen("genry_farewell"):
                renpy.show_screen("genry_farewell", fw[0], fw[1])
                renpy.restart_interaction()
                return None
            return renpy.run(_genry_prev_quit)

        config.quit_action = genry_quit

transform genry_farewell_in:
    on show:
        alpha 0.0
        linear 0.8 alpha 1.0

screen genry_farewell(img, words):
    modal True
    zorder 900
    fixed at genry_farewell_in:
        add Solid("#000000")
        add img xalign 0.5 yalign 0.5
        frame:
            xalign 0.5
            yalign 0.86
            background Solid("#000000b0")
            padding (40, 20)
            text words size 34 color "#ffdd7d" text_align 0.5 font "fonts/calibri.ttf"
    hbox:
        xalign 0.5
        yalign 0.97
        spacing 80
        textbutton u"Выйти" action Quit(confirm=False) text_size 34 text_color "#eef6ff" text_hover_color "#ffd27d" background None hover_background None text_font "fonts/corbel.ttf"
        textbutton u"Остаться" action Hide("genry_farewell") text_size 34 text_color "#eef6ff" text_hover_color "#ffd27d" background None hover_background None text_font "fonts/corbel.ttf"
)rpy";
