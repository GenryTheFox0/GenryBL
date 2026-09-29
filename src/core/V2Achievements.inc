// GenryBL V2 - «Достижения 2.0»: the mod's achievements the way Everlasting Summer itself shows its own.
// The popup is ES's achievement plate (the ring with the heart and the capsule cut out of images/misc/achievement2.png,
// the text on it), slid in by ES's own achievement_trans with sfx_achievement, and only when the game's own setting
// «Показывать достижения» is on. The list is a twin of ES's gallery screen: history_bg, «★ Достижения 7/12 ★» in
// settings_link, sections by heroine switched like «CG / BG», 336x196 thumbnails in ES's frames, the closed eye of an
// unseen picture for a hidden one, «Назад» and pages at the bottom; the info line tells what it is, when it came and what
// it still needs. Appended to a mod only when it has achievements.
#pragma once

static const char* const kV2Achievements = R"rpy(
# GenryBL V2: достижения как родные БЛ - табличка achievement2.png, выезд achievement_trans, звук sfx_achievement,
# настройка игры «Показывать достижения»; список - близнец галереи БЛ
init python:
    import time as genry_ach_clock

    def genry_ach_plate(title, sub=u"Achievement unlocked"):
        base = "images/misc/achievement2.png"
        w = max(443, 92 + int(len(title) * 13.5) + 48)
        return Fixed(
            im.Crop(base, (0, 0, 86, 95)),
            Transform(im.Scale(im.Crop(base, (80, 0, 4, 95)), w - 86 - 43, 95), xpos=86),
            Transform(im.Crop(base, (400, 0, 43, 95)), xpos=w - 43),
            Text(sub, font="fonts/calibri.ttf", size=22, color="#ffffff", xpos=91, ypos=14),
            Text(title, font="fonts/calibri.ttf", size=26, color="#b6ff00", xpos=91, ypos=44),
            xysize=(w, 95))

    # one after another, never two plates on top of each other (no `default`: two GenryBL mods would declare it twice)
    def genry_ach_queue():
        q = getattr(store, "genry_ach_q", None)
        if not isinstance(q, list):
            q = []
            store.genry_ach_q = q
        return q

    def genry_ach_show(title):
        if persistent.show_achievements is False:
            return
        q = genry_ach_queue()
        q.append(title)
        if len(q) == 1:
            genry_ach_next(True)

    def genry_ach_next(first=False):
        q = genry_ach_queue()
        if not first and q:
            q.pop(0)
        if q:
            renpy.play(sfx_achievement, channel="audio")
            renpy.show("genry_ach_plate", what=genry_ach_plate(q[0]), at_list=[achievement_trans], layer="overlay")
            renpy.show_screen("genry_ach_clock")
        else:
            renpy.hide("genry_ach_plate", layer="overlay")

    # unlock: the flag for good (persistent), the day it came, the popup; then whatever it opened by itself
    def genry_ach_unlock(achs, dates, key, title):
        if getattr(persistent, key, False):
            return
        setattr(persistent, key, True)
        d = getattr(persistent, dates, None) or {}
        d[key] = genry_ach_clock.strftime("%d.%m.%Y")
        setattr(persistent, dates, d)
        genry_ach_show(title)
        genry_ach_auto(achs, dates)

    # «платина»: comes by itself when every other one of its section (or of the whole mod) is there
    def genry_ach_auto(achs, dates):
        for a in achs:
            if not a.get("p") or getattr(persistent, a["k"], False):
                continue
            rest = [b for b in achs if b is not a and not b.get("p") and (not a.get("s") or b.get("s") == a.get("s"))]
            if rest and all(getattr(persistent, b["k"], False) for b in rest):
                genry_ach_unlock(achs, dates, a["k"], a["t"])

    def genry_ach_sections(achs):
        out = []
        for a in achs:
            if a.get("s") and a["s"] not in out:
                out.append(a["s"])
        return [u"Все"] + out

    # the card's picture: its CG / background / the heroine's face; none - ES's ring with the heart; not yet - grey
    def genry_ach_thumb(a, got):
        pic = a.get("i")
        if pic and a.get("ik") == "sprite":
            th = Transform(pic, crop=(290, 200, 320, 180))
        elif pic:
            th = Transform(pic, size=(320, 180))
        else:
            th = Fixed(Solid("#1c1f20"), Transform(im.Crop("images/misc/achievement2.png", (0, 0, 78, 95)), align=(0.5, 0.5), zoom=1.5), xysize=(320, 180))
        if not got:
            th = Transform(th, matrixcolor=SaturationMatrix(0.0) * BrightnessMatrix(-0.25), alpha=0.55)
        return Fixed(Transform(th, xpos=8, ypos=8), get_image("gui/gallery/thumbnail_idle.png"), xysize=(336, 196))

transform genry_ach_card_in(d=0.0):
    alpha 0.0
    pause d
    easein 0.3 alpha 1.0

screen genry_ach_clock():
    timer 4.0 action [Hide("genry_ach_clock"), Function(genry_ach_next)]

screen genry_achievements(achs, called=False, dates=None):
    tag menu
    modal True
    default genry_sec = 0
    default genry_page = 0
    default genry_hov = None
    $ genry_secs = genry_ach_sections(achs)
    $ genry_list = [a for a in achs if genry_sec == 0 or a.get("s") == genry_secs[genry_sec]]
    $ genry_got = len([a for a in genry_list if getattr(persistent, a["k"], False)])
    $ genry_cells = 9
    $ genry_pages = max(1, (len(genry_list) + genry_cells - 1) // genry_cells)
    $ genry_dates = (getattr(persistent, dates, None) or {}) if dates else {}
    frame background get_image("gui/settings/history_bg.jpg"):
        if len(genry_secs) > 1:
            textbutton genry_secs[(genry_sec - 1) % len(genry_secs)] style "log_button" text_style "settings_link" xalign 0.02 yalign 0.08 action [SetScreenVariable("genry_sec", (genry_sec - 1) % len(genry_secs)), SetScreenVariable("genry_page", 0)]
            textbutton genry_secs[(genry_sec + 1) % len(genry_secs)] style "log_button" text_style "settings_link" xalign 0.98 yalign 0.08 action [SetScreenVariable("genry_sec", (genry_sec + 1) % len(genry_secs)), SetScreenVariable("genry_page", 0)]
        hbox xalign 0.5 yalign 0.08:
            add get_image("gui/settings/star.png") yalign 0.65
            text (u" Достижения " if genry_sec == 0 else u" " + genry_secs[genry_sec] + u" ") + u"%d/%d " % (genry_got, len(genry_list)) style "settings_link" yalign 0.5 color "#ffffff"
            add get_image("gui/settings/star.png") yalign 0.65
        grid 3 3 xpos 0.09 ypos 0.18:
            for genry_i in range(genry_cells):
                $ genry_n = genry_page * genry_cells + genry_i
                if genry_n < len(genry_list):
                    $ genry_a = genry_list[genry_n]
                    $ genry_on = getattr(persistent, genry_a["k"], False)
                    $ genry_hidden = genry_a.get("h") and not genry_on
                    button at genry_ach_card_in(genry_i * 0.04):
                        style "blank_button"
                        xysize (386, 246)
                        action NullAction()
                        hovered SetScreenVariable("genry_hov", genry_a)
                        unhovered SetScreenVariable("genry_hov", None)
                        if genry_hidden:
                            add get_image("gui/gallery/not_opened_idle.png")
                        else:
                            add genry_ach_thumb(genry_a, genry_on)
                else:
                    null width 386 height 246
        # what the hovered one is: the title, what it is about, when it came / what it still needs
        if genry_hov is not None:
            $ genry_on = getattr(persistent, genry_hov["k"], False)
            vbox xalign 0.5 yalign 0.86 spacing 4:
                text (u"???" if genry_hov.get("h") and not genry_on else genry_hov["t"]) style "settings_link" size 42 color ("#ffffff" if genry_on else "#909ca3") xalign 0.5
                if genry_hov.get("d") and not (genry_hov.get("h") and not genry_on):
                    text genry_hov["d"] style "settings_text" size 26 color "#c8c8c8" xalign 0.5
                if genry_on and genry_hov["k"] in genry_dates:
                    text u"получено " + genry_dates[genry_hov["k"]] style "settings_text" size 22 color "#9dcd55" xalign 0.5
                elif genry_hov.get("n"):
                    text u"сначала: " + u", ".join([(b["t"] if getattr(persistent, b["k"], False) or not b.get("h") else u"???") for b in achs if b["k"] in genry_hov["n"]]) style "settings_text" size 22 color "#909ca3" xalign 0.5
                elif genry_hov.get("p") and not genry_on:
                    text u"придёт само, когда соберёшь остальные" style "settings_text" size 22 color "#909ca3" xalign 0.5
        textbutton u"Назад" style "log_button" text_style "settings_link" xalign 0.015 yalign 0.92 action (Return() if called else Hide("genry_achievements"))
        if genry_pages > 1:
            if genry_page > 0:
                imagebutton auto get_image("gui/dialogue_box/day/backward_%s.png") yalign 0.5 xalign 0.01 action SetScreenVariable("genry_page", genry_page - 1)
            imagebutton auto get_image("gui/dialogue_box/day/forward_%s.png") yalign 0.5 xalign 0.99 action SetScreenVariable("genry_page", (genry_page + 1) % genry_pages)
            text "%d/%d" % (genry_page + 1, genry_pages) style "settings_link" xalign 0.985 yalign 0.92
    key "game_menu" action (Return() if called else Hide("genry_achievements"))
)rpy";
