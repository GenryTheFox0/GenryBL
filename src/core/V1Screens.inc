// GenryBL V1 - Ren'Py of the V1-only features (Telltale timed choice, «X это запомнит»,
// relationship meters, inventory, the mod's gallery / achievements, the mod's own main menu).
// Every block is appended to a mod only when the mod uses it. Screen-language rules of
// Ren'Py 7.4 (ES): padding/background only on frame/window/button, no xmaximum on text.
#pragma once

static const char* const kV1TimedChoice = R"rpy(
# GenryBL V1: выбор на время в духе Telltale - полоса тает к центру, не успел = своя ветка
transform genry_timer_shrink(t):
    xanchor 0.5
    xalign 0.5
    xzoom 1.0
    linear t xzoom 0.0

transform genry_timed_in:
    alpha 0.0
    yoffset 40
    easein 0.35 alpha 1.0 yoffset 0

screen genry_choice_timed(items, seconds=10.0):
    zorder 50
    modal True
    $ genry_late = [genry_x for genry_x in items if genry_x.kwargs.get("timeout")]
    if genry_late:
        timer seconds action genry_late[0].action
    add Solid("#00000055")
    vbox:
        at genry_timed_in
        xalign 0.5
        yalign 0.88
        spacing 26
        add Solid("#ffffffee", xsize=900, ysize=6) at genry_timer_shrink(seconds)
        hbox:
            xalign 0.5
            spacing 26
            for genry_it in items:
                if not genry_it.kwargs.get("timeout"):
                    button:
                        action genry_it.action
                        activate_sound "sound/sfx/click_1.ogg"
                        xminimum 320
                        padding (30, 18)
                        background Solid("#000000b8")
                        hover_background Solid("#262018e8")
                        text genry_it.caption size 30 color "#ffffff" hover_color "#ffd27d" xalign 0.5 text_align 0.5 font "fonts/calibri.ttf"
)rpy";

static const char* const kV1Popups = R"rpy(
# GenryBL V1: всплывашки «Алиса это запомнит» и изменения шкал
transform genry_popup_in:
    on show:
        alpha 0.0
        xoffset -30
        easein 0.35 alpha 1.0 xoffset 0
    on hide:
        easeout 0.6 alpha 0.0

screen genry_remember(what, icon=None, where="tl", secs=3.4, accent="#ffd27d"):
    zorder 100
    timer secs action Hide("genry_remember")
    frame:
        at genry_popup_in
        xpos (40 if where == "tl" else (1880 if where == "tr" else 960))
        xanchor (0.0 if where == "tl" else (1.0 if where == "tr" else 0.5))
        ypos (820 if where == "bc" else 40)
        background Solid("#000000b0")
        padding (20, 14)
        hbox:
            spacing 14
            add Solid(accent, xsize=6, ysize=32) yalign 0.5
            if icon:
                add icon yalign 0.5
            text what size 28 color "#ffffff" yalign 0.5 font "fonts/calibri.ttf"

screen genry_memories(mems, called=False):
    modal True
    zorder 90
    add Solid("#000000cc")
    frame:
        xalign 0.5
        yalign 0.5
        background Solid("#0f141bf2")
        padding (44, 32)
        vbox:
            spacing 14
            xmaximum 1100
            text u"Память" size 42 color "#ffd27d" font "fonts/corbel.ttf"
            if not mems:
                text u"Пока никто ничего не запомнил." size 26 color "#a9b9a4" font "fonts/calibri.ttf"
            viewport:
                mousewheel True
                ymaximum 640
                vbox:
                    spacing 10
                    for genry_who, genry_what in mems:
                        hbox:
                            spacing 14
                            add Solid("#ffd27d", xsize=6, ysize=28) yalign 0.5
                            text ((genry_who + u": ") if genry_who else u"") + genry_what size 26 color "#ffffff" font "fonts/calibri.ttf"
            null height 8
            textbutton u"Закрыть" action (Return(True) if called else Hide("genry_memories")) xalign 0.5 text_size 28 text_color "#eef6ff" text_hover_color "#ffd27d" background None hover_background None text_font "fonts/corbel.ttf" activate_sound "sound/sfx/click_1.ogg"
)rpy";

static const char* const kV1Meters = R"rpy(
# GenryBL V1: шкалы отношений - всплывашка при изменении, экран со всеми шкалами, кнопка «Отношения»
init python:
    def genry_meter_ping(title, delta, color, var, lo, hi):
        v = getattr(renpy.store, var, 0)
        v = max(lo, min(hi, v))
        setattr(renpy.store, var, v)
        renpy.show_screen("genry_meter_ping", title=title, delta=delta, color=color, value=v, lo=lo, hi=hi)

    def genry_best_of(pairs):
        best = None
        top = None
        for label, value in pairs:
            if top is None or value > top:
                best, top = label, value
            elif value == top:
                best = None
        if top is None or top <= 0:
            return None
        return best

screen genry_meter_ping(title, delta, color, value, lo, hi):
    zorder 100
    timer 2.8 action Hide("genry_meter_ping")
    frame:
        at genry_popup_in
        xpos 40
        ypos 120
        background Solid("#000000b8")
        padding (22, 14)
        vbox:
            spacing 8
            hbox:
                spacing 12
                text title size 26 color color font "fonts/calibri.ttf"
                text ("%+g" % delta) size 26 color ("#9bd35a" if delta > 0 else "#ff6b6b") font "fonts/calibri.ttf"
            bar value StaticValue(value - lo, max(1, hi - lo)) xsize 280 ysize 8 left_bar Solid(color) right_bar Solid("#ffffff22")

screen genry_meters(meters, called=False, title=u"Отношения"):
    modal True
    zorder 90
    add Solid("#000000cc")
    frame:
        xalign 0.5
        yalign 0.5
        background Solid("#0f141bf2")
        padding (44, 32)
        vbox:
            spacing 16
            text title size 42 color "#ffd27d" font "fonts/corbel.ttf"
            null height 4
            for genry_t, genry_c, genry_v, genry_lo, genry_hi in meters:
                $ genry_val = getattr(renpy.store, genry_v, 0)
                hbox:
                    spacing 22
                    fixed:
                        xysize (280, 38)
                        text genry_t size 28 color genry_c yalign 0.5 font "fonts/calibri.ttf"
                    bar value StaticValue(genry_val - genry_lo, max(1, genry_hi - genry_lo)) xsize 380 ysize 14 yalign 0.5 left_bar Solid(genry_c) right_bar Solid("#ffffff1a")
                    text ("%g" % genry_val) size 26 color "#ffffff" yalign 0.5 font "fonts/calibri.ttf"
            null height 8
            textbutton u"Закрыть" action (Return(True) if called else Hide("genry_meters")) xalign 0.5 text_size 28 text_color "#eef6ff" text_hover_color "#ffd27d" background None hover_background None text_font "fonts/corbel.ttf" activate_sound "sound/sfx/click_1.ogg"

screen genry_meters_button(meters):
    zorder 80
    textbutton u"Отношения" action Show("genry_meters", meters=meters) xalign 0.985 ypos 24 text_size 26 text_color "#ffd27dcc" text_hover_color "#ffffff" background None hover_background None text_font "fonts/corbel.ttf" activate_sound "sound/sfx/click_1.ogg"
)rpy";

static const char* const kV1Inventory = R"rpy(
# GenryBL V1: инвентарь на прохождение (предмет / убратьпредмет / еслипредмет / инвентарь)
screen genry_inventory(items, inv_var, called=False):
    modal True
    zorder 90
    $ genry_inv = getattr(renpy.store, inv_var, [])
    add Solid("#000000cc")
    frame:
        xalign 0.5
        yalign 0.5
        background Solid("#0f141bf2")
        padding (44, 32)
        vbox:
            spacing 16
            xmaximum 900
            text u"Инвентарь" size 42 color "#ffd27d" font "fonts/corbel.ttf"
            if not genry_inv:
                text u"Пусто." size 26 color "#a9b9a4" font "fonts/calibri.ttf"
            for genry_k in genry_inv:
                $ genry_i = items.get(genry_k, (genry_k, None, u""))
                hbox:
                    spacing 18
                    if genry_i[1]:
                        add Transform(genry_i[1], fit="contain", xysize=(76, 76))
                    else:
                        add Solid("#27382f", xsize=76, ysize=76)
                    vbox:
                        spacing 4
                        yalign 0.5
                        text genry_i[0] size 28 color "#ffffff" font "fonts/calibri.ttf"
                        if genry_i[2]:
                            text genry_i[2] size 20 color "#a9b9a4" font "fonts/calibri.ttf"
            null height 8
            textbutton u"Закрыть" action (Return(True) if called else Hide("genry_inventory")) xalign 0.5 text_size 28 text_color "#eef6ff" text_hover_color "#ffd27d" background None hover_background None text_font "fonts/corbel.ttf" activate_sound "sound/sfx/click_1.ogg"

screen genry_inventory_button(items, inv_var):
    zorder 80
    textbutton u"Инвентарь" action Show("genry_inventory", items=items, inv_var=inv_var) xalign 0.985 ypos 72 text_size 26 text_color "#eef6ffcc" text_hover_color "#ffd27d" background None hover_background None text_font "fonts/corbel.ttf" activate_sound "sound/sfx/click_1.ogg"
)rpy";

static const char* const kV1Gallery = R"rpy(
# GenryBL V1: галерея CG мода - открывается то, что игрок уже видел
screen genry_gallery(cgs, called=False):
    modal True
    zorder 90
    default genry_big = None
    add Solid("#000000e6")
    vbox:
        xalign 0.5
        yalign 0.5
        spacing 20
        text u"Галерея" size 42 color "#ffd27d" xalign 0.5 font "fonts/corbel.ttf"
        vpgrid:
            cols 4
            spacing 16
            draggable True
            mousewheel True
            ymaximum 700
            for genry_cg in cgs:
                if renpy.seen_image(genry_cg):
                    imagebutton idle Transform(genry_cg, zoom=0.18) hover Transform(genry_cg, zoom=0.18, alpha=0.8) action SetScreenVariable("genry_big", genry_cg)
                else:
                    add Solid("#1b2a22", xsize=345, ysize=194)
        textbutton u"Закрыть" action (Return(True) if called else Hide("genry_gallery")) xalign 0.5 text_size 28 text_color "#eef6ff" text_hover_color "#ffd27d" background None hover_background None text_font "fonts/corbel.ttf" activate_sound "sound/sfx/click_1.ogg"
    if genry_big:
        button:
            xysize (config.screen_width, config.screen_height)
            background Solid("#000000")
            action SetScreenVariable("genry_big", None)
            add genry_big
)rpy";

static const char* const kV1Achievements = R"rpy(
# GenryBL V1: экран достижений мода
screen genry_achievements(achs, called=False):
    modal True
    zorder 90
    add Solid("#000000cc")
    frame:
        xalign 0.5
        yalign 0.5
        background Solid("#0f141bf2")
        padding (44, 32)
        vbox:
            spacing 12
            xmaximum 900
            text u"Достижения" size 42 color "#ffd27d" font "fonts/corbel.ttf"
            for genry_key, genry_name in achs:
                $ genry_got = getattr(persistent, genry_key, False)
                hbox:
                    spacing 16
                    add Solid(("#9bd35a" if genry_got else "#33483c"), xsize=18, ysize=18) yalign 0.5
                    text genry_name size 28 color ("#ffffff" if genry_got else "#65796a") yalign 0.5 font "fonts/calibri.ttf"
            null height 8
            textbutton u"Закрыть" action (Return(True) if called else Hide("genry_achievements")) xalign 0.5 text_size 28 text_color "#eef6ff" text_hover_color "#ffd27d" background None hover_background None text_font "fonts/corbel.ttf" activate_sound "sound/sfx/click_1.ogg"
)rpy";

static const char* const kV1MenuButton = R"rpy(
# GenryBL V1: главное меню мода - кнопки выезжают по очереди, наведённая сдвигается с золотой чертой
transform genry_menu_btn:
    on idle:
        easein 0.2 xoffset 0
    on hover:
        easein 0.2 xoffset 18

transform genry_menu_in(d=0.0):
    alpha 0.0
    xoffset -60
    pause d
    easein 0.45 alpha 1.0 xoffset 0

transform genry_waifu_in:
    alpha 0.0
    xoffset 90
    easein 0.6 alpha 1.0 xoffset 0
)rpy";

static const char* const kV1Chapters = R"rpy(
# GenryBL V1: экран глав мода - открытые ведут в свою сцену, закрытые серые
screen genry_chapters(chs, called=False):
    modal True
    zorder 90
    add Solid("#000000e6")
    vbox:
        xalign 0.5
        yalign 0.5
        spacing 22
        text u"Главы" size 46 color "#ffd27d" xalign 0.5 font "fonts/corbel.ttf"
        vpgrid:
            cols 3
            spacing 22
            draggable True
            mousewheel True
            ymaximum 760
            for genry_lab, genry_title, genry_img, genry_key in chs:
                $ genry_open = getattr(persistent, genry_key, False)
                button:
                    xysize (480, 320)
                    background Solid("#0f141b")
                    hover_background Solid("#27382f")
                    activate_sound "sound/sfx/click_1.ogg"
                    action (Return(genry_lab) if genry_open else NullAction())
                    vbox:
                        spacing 8
                        xalign 0.5
                        if genry_open and genry_img:
                            add Transform(genry_img, fit="cover", xysize=(460, 258)) xalign 0.5
                        else:
                            add Solid(("#1b2a22" if genry_open else "#111111"), xsize=460, ysize=258) xalign 0.5
                        text (genry_title if genry_open else u"???") size 28 color ("#ffffff" if genry_open else "#555555") hover_color "#ffd27d" xalign 0.5 font "fonts/corbel.ttf"
        textbutton u"Закрыть" action (Return(None) if called else Hide("genry_chapters")) xalign 0.5 text_size 28 text_color "#eef6ff" text_hover_color "#ffd27d" background None hover_background None text_font "fonts/corbel.ttf" activate_sound "sound/sfx/click_1.ogg"
)rpy";

static const char* const kV1SaySkins = R"rpy(
# GenryBL V1: скины окна диалога («окно нуар / кино / пиксель / книга / бл»). Меняются только
# реплики, пока мод держит genry_say_skin; сама игра, чужие моды и NVL остаются как были.
init 5 python:
    # not `default`: two GenryBL mods would give it a default twice - a Ren'Py error
    genry_say_skin = None
    if not getattr(config.say_arguments_callback, "genry_skin", False):
        genry_prev_say_args = config.say_arguments_callback
        def genry_say_args(who, *args, **kwargs):
            if genry_prev_say_args is not None:
                args, kwargs = genry_prev_say_args(who, *args, **kwargs)
            skin = getattr(renpy.store, "genry_say_skin", None)
            if skin and "screen" not in kwargs and getattr(who, "screen", "say") == "say":
                kwargs["screen"] = "genry_say_" + skin
            return args, kwargs
        genry_say_args.genry_skin = True
        config.say_arguments_callback = genry_say_args

screen genry_say_noir(who, what):
    window:
        id "window"
        xfill True
        yalign 1.0
        ysize 250
        background Solid("#000000dc")
        padding (240, 30)
        vbox:
            spacing 10
            if who:
                text who id "who" size 30 color "#b8b8b8" font "fonts/times.ttf"
            text what id "what" size 32 color "#f2f2f2" font "fonts/times.ttf"

screen genry_say_kino(who, what):
    window:
        id "window"
        background None
        xalign 0.5
        yalign 0.94
        xmaximum 1500
        vbox:
            xalign 0.5
            spacing 4
            if who:
                text who id "who" size 26 color "#ffd27d" xalign 0.5 outlines [(2, "#000000cc", 0, 0)] font "fonts/calibri.ttf"
            text what id "what" size 36 color "#ffffff" text_align 0.5 xalign 0.5 outlines [(3, "#000000dd", 0, 0)] font "fonts/calibri.ttf"

screen genry_say_pixel(who, what):
    window:
        id "window"
        xalign 0.5
        yalign 0.97
        xsize 1600
        ysize 230
        background Solid("#101030f2")
        padding (40, 28)
        vbox:
            spacing 16
            if who:
                text who id "who" size 22 color "#ffcc00" font "fonts/PressStart2P.ttf"
            text what id "what" size 22 color "#e0f8d0" line_spacing 10 font "fonts/PressStart2P.ttf"

screen genry_say_book(who, what):
    window:
        id "window"
        xalign 0.5
        yalign 0.97
        xsize 1500
        ysize 240
        background Solid("#f4ecd2f5")
        padding (60, 30)
        vbox:
            spacing 8
            if who:
                text who id "who" size 30 color "#8b3a1a" font "fonts/corbel.ttf"
            text what id "what" size 30 color "#4a3b28" font "fonts/calibri.ttf"
)rpy";

static const char* const kV1Flashback = R"rpy(
# GenryBL V1: флешбэк - киношные полосы (под окном диалога), сепия и помехи ставит сам сценарий
screen genry_flashback_frame():
    zorder -5
    add Solid("#000000", xsize=config.screen_width, ysize=96)
    add Solid("#000000", xsize=config.screen_width, ysize=96) yalign 1.0
)rpy";

// «параллакс» - the "3D режим" of Workshop mod 3130079690, as a command: the background moves
// less than the sprites when the mouse moves («close» sprites the most, «far» the least)
static const char* const kV1Parallax = R"rpy(
# GenryBL V1: параллакс от мыши (идея - мод «3D режим», 3130079690). genry_parallax = сила, 0 = выкл
init 2 python:
    import math as _genry_math
    if not hasattr(store, "genry_parallax"):
        store.genry_parallax = 0.0

    def genry_parallax_offsets(xo, yo):
        # every picture on the master layer moves by its depth: bg 0.5, sprites 0.8, close 1, far 0.6
        layer = "master"
        for t in renpy.get_showing_tags(layer):
            attrs = renpy.get_attributes(t, layer) or ()
            if t == "widget" or t.startswith("genry_") and not t.startswith("genry_weather"):
                m = 0.0
            elif t.startswith("bg") or t.startswith("cg"):
                m = 0.5
            elif t.startswith("genry_weather"):
                m = 1.2
            elif "close" in attrs:
                m = 1.0
            elif "far" in attrs:
                m = 0.6
            else:
                m = 0.8
            d = [renpy.scene_lists().get_displayable_by_tag(layer, t)]
            for i in d:
                if getattr(i, "target", False):
                    i = i.target
                if not getattr(i, "offset", False) and getattr(i, "children", False):
                    d += i.children
                    continue
                if getattr(i, "offset", False):
                    o = (round(xo * m, 1), round(yo * m, 1))
                    if i.offset != o:
                        i.offset = o
                        i.subpixel = True
                        i.update()

    class GenryParallax(renpy.Displayable):
        # eases towards the mouse every frame (no jumps when a sprite appears, no shaking),
        # pauses while the camp map is open (its places are drawn on another layer)
        def __init__(self):
            super(GenryParallax, self).__init__()
            self.cx = self.cy = 0.0
            self.last = None
            self.zoom = 1.0
        def render(self, width, height, st, at):
            s = store.genry_parallax or 0.0
            if getattr(store, "map_enabled", False):
                s = 0.0
            dt = 0.016 if self.last is None or st < self.last else min(0.1, st - self.last)
            self.last = st
            tx = ty = 0.0
            if s:
                mx, my = renpy.get_mouse_pos()
                if mx is not None and my is not None:
                    tx = (config.screen_width / 2.0 - max(0, min(config.screen_width, mx))) / 36.0 * s
                    ty = (config.screen_height / 2.0 - max(0, min(config.screen_height, my))) / 36.0 * s
            k = 1.0 - _genry_math.exp(-dt * 5.0)
            self.cx += (tx - self.cx) * k
            self.cy += (ty - self.cy) * k
            moving = abs(self.cx) > 0.05 or abs(self.cy) > 0.05 or s
            if moving or getattr(store, "_genry_parallax_was", False):
                genry_parallax_offsets(self.cx, self.cy)
            store._genry_parallax_was = bool(moving)
            z = 1.0 + 0.03 * min(s, 2.5) if s else 1.0
            if z != self.zoom:
                self.zoom = z
                renpy.show_layer_at([Transform(zoom=z, xycenter=(0.5, 0.5))], layer="master", reset=False, camera=True)
            if moving:
                renpy.redraw(self, 0)
            return renpy.Render(1, 1)
        def event(self, ev, x, y, st):
            if store.genry_parallax:
                renpy.redraw(self, 0)

    if not getattr(store, "_genry_parallax_hooked", False):
        store._genry_parallax_hooked = True
        config.underlay.append(GenryParallax())
)rpy";

// «табло Текст» - the split-flap board of Samantha (354397869): letters flip, then settle one by one
static const char* const kV1SplitFlap = R"rpy(
# GenryBL V1: табло - буквы перещёлкиваются, как на вокзальном табло (идея - мод «Саманта»)
init 2 python:
    import random as _genry_flap_rnd
    _genry_flap_abc = u"АБВГДЕЖЗИКЛМНОПРСТУФХЦЧШЭЮЯ0123456789"
    def genry_flap_tile(ch, size):
        w, h = int(size * 0.86), int(size * 1.45)
        return Fixed(Solid("#15171b", xsize=w, ysize=h),
                     Solid("#2a2d33", xsize=w, ysize=h // 2),
                     Solid("#000000", xsize=w, ysize=2, ypos=h // 2),
                     Text(ch, font="fonts/corbel.ttf", size=size, color="#f2e6c2", bold=True, xalign=0.5, yalign=0.5),
                     xsize=w, ysize=h)
    def genry_flap_frame(st, at, text=u"", size=64, step=0.06, settle=0.45):
        tiles = []
        done = True
        for i, ch in enumerate(text):
            if ch == u" ":
                tiles.append(Null(width=int(size * 0.45)))
                continue
            if st < settle + i * step:
                done = False
                ch = _genry_flap_rnd.choice(_genry_flap_abc)
            tiles.append(genry_flap_tile(ch, size))
        return HBox(*tiles, spacing=5), (None if done else 0.05)
)rpy";


static const char* const kV1Map = R"rpy(
# GenryBL V1: карта лагеря на рисунке самой игры. Своя: родная берёт места из label start игры (мод из «Моды»
# его не проходит), а другие моды (7ДЛ) вешаются на тот же слой - там карта не нажималась. Открытые места
# жёлтые, под мышкой - подсвечены и подписаны, мордочка героини покачивается рядом с подписью места.
init python:
    def genry_map_pic(kind):
        genry_p = getattr(store, "map_pics", None)
        if isinstance(genry_p, dict) and genry_p.get(kind) and renpy.loadable(genry_p[kind]):
            return genry_p[kind]
        return {"bgpic": "images/maps/map.jpg", "available": "images/maps/map_available.jpg",
                "selected": "images/maps/map_selected.jpg"}[kind]

transform genry_map_chibi(x, y):
    xcenter x
    ycenter y
    yoffset 0
    ease 0.9 yoffset -7
    ease 0.9 yoffset 0
    repeat

screen genry_camp_map(places, title=u"Куда пойти?"):
    modal True
    zorder 40
    default genry_hov = None
    add genry_map_pic("bgpic")
    for genry_z in places:
        imagebutton:
            idle im.Crop(genry_map_pic("available"), genry_z[4], genry_z[5], genry_z[6] - genry_z[4], genry_z[7] - genry_z[5])
            hover im.Crop(genry_map_pic("selected"), genry_z[4], genry_z[5], genry_z[6] - genry_z[4], genry_z[7] - genry_z[5])
            xpos genry_z[4]
            ypos genry_z[5]
            activate_sound "sound/sfx/click_1.ogg"
            hovered SetScreenVariable("genry_hov", genry_z[3])
            unhovered SetScreenVariable("genry_hov", None)
            action Return((genry_z[0], genry_z[1]))
    for genry_z in places:
        if genry_z[2]:
            add genry_z[2] at genry_map_chibi(genry_z[8], genry_z[9])
    frame:
        xalign 0.5
        ypos 34
        background Solid("#000000b4")
        padding (34, 12)
        text (genry_hov or title) size 36 color ("#ffd27d" if genry_hov else "#f1ece0") font "fonts/corbel.ttf"
)rpy";

static const char* const kV1Lids = R"rpy(
# GenryBL V1: закрытые веки без анимации - «фон» под закрытыми глазами стирает blink вместе со сценой (scene),
# веки возвращаются сразу, до первого кадра; ES blink в конце анимации стоит ровно так
image genry_lids = Fixed("anim blink_up", "anim blink_down")
)rpy";
