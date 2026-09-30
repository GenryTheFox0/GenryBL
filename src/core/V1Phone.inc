// GenryBL V1 - Телефон 3.0 (Ren'Py 7.4). Every picture is drawn by PhoneAssets.cpp and written by the
// builder into mods/<mod>/genry/phone/; the compiler declares them as «image genry_ph <name>».
// Message tuples stay the old five: (side, name, body, time, status); side = "me"/"them" + "_photo",
// "_photo18", "_photo1" (once), "_voice"; or "call" for a centred system line.
// Split into several literals: MSVC refuses a single string literal over ~16 KB.

static const char* const kV1Phone = R"rpy(
# GenryBL V1: телефон 3.0 - чат, фото (18+ и одноразовые), голосовые, ответы кнопками, звонки, лента
init 2 python:
    if not hasattr(store, "genry_phone_open"):
        store.genry_phone_open = {}
    if not hasattr(store, "_genry_phone"):
        store._genry_phone = []
    if not hasattr(store, "_genry_phone_typing"):
        store._genry_phone_typing = ""
    if not hasattr(store, "_genry_phone_contact"):
        store._genry_phone_contact = u""
    if not hasattr(store, "genry_ph_photos"):
        store.genry_ph_photos = []
    if not hasattr(store, "genry_ph_ava"):
        store.genry_ph_ava = {}

    class GenryFit(renpy.Displayable):
        # the picture scaled to fit w x h (keeps proportions); blur=True: a soft unreadable preview
        def __init__(self, child, w, h, blur=False, **kw):
            super(GenryFit, self).__init__(**kw)
            self.child = renpy.displayable(child)
            self.w, self.h, self.blur = w, h, blur
        def render(self, width, height, st, at):
            cr = renpy.render(self.child, 4000, 4000, st, at)
            cw, ch = cr.get_size()
            z = min(float(self.w) / max(1, cw), float(self.h) / max(1, ch))
            d = Transform(self.child, zoom=z)
            if self.blur:
                try:
                    d = Transform(Flatten(Transform(self.child, zoom=z * 0.05)), zoom=20.0)
                except Exception:
                    d = Solid("#1c2733", xsize=int(cw * z), ysize=int(ch * z))
            r = renpy.render(d, width, height, st, at)
            rv = renpy.Render(int(cw * z), int(ch * z))
            rv.blit(r, (0, 0))
            return rv
        def visit(self):
            return [self.child]

    def genry_ph_next_time():
        t = _genry_phone[-1][3] if _genry_phone else u"14:25"
        try:
            h, m = [int(x) for x in t.split(u":")]
        except Exception:
            h, m = 14, 25
        m += 1
        return u"%02d:%02d" % ((h + m // 60) % 24, m % 60)

    def genry_ph_said(caption):
        # a reply picked in the phone becomes the hero's own message («(не отвечать)» stays silent)
        if caption and not caption.startswith(u"("):
            _genry_phone.append((u"me", u"Я", caption, genry_ph_next_time(), u"√√"))

    def genry_ph_avatar(name, size):
        face = genry_ph_ava.get((name or u"").lower())
        if face:
            return Transform(face, zoom=size / 72.0)
        letter = (name or u"?")[:1].upper()
        return Fixed(Transform("genry_ph circle", zoom=size / 200.0),
                     Text(letter, font="fonts/calibrib.ttf", size=int(size * 0.5), color="#0b1018", xalign=0.5, yalign=0.5),
                     xysize=(size, size))

transform genry_ph_none:
    alpha 1.0

transform genry_ph_rise:
    yoffset 60
    alpha 0.0
    easein 0.35 yoffset 0 alpha 1.0

transform genry_ph_pop:
    zoom 0.92
    alpha 0.0
    easein 0.18 zoom 1.0 alpha 1.0

transform genry_ph_typing(delay=0.0):
    alpha 0.35
    pause delay
    block:
        linear 0.3 alpha 1.0
        linear 0.3 alpha 0.35
        pause 0.3
        repeat

transform genry_voice_wave:
    alpha 0.6
    linear 0.5 alpha 1.0
    linear 0.5 alpha 0.6
    repeat

transform genry_ph_ripple(delay=0.0):
    xalign 0.5
    yalign 0.5
    zoom 0.7
    alpha 0.0
    pause delay
    block:
        alpha 0.8
        zoom 0.7
        linear 1.8 zoom 1.25 alpha 0.0
        repeat

transform genry_ph_banner_in:
    yoffset -160
    easein 0.35 yoffset 0
    pause 3.0
    easeout 0.35 yoffset -160

screen genry_ph_status(t):
    text t pos (36, 11) size 22 color "#ffffff" font "fonts/calibrib.ttf"
    add "genry_ph status" pos (338, 0)

screen genry_ph_msg(side, who, body, t, status):
    $ mine = side.startswith("me")
    $ kind = "call" if side == "call" else (side.split("_", 1)[1] if "_" in side else "text")
    if kind == "call":
        text body xalign 0.5 size 16 color "#9fb3c8" font "fonts/calibri.ttf"
    else:
        hbox:
            at genry_ph_pop
            xalign (1.0 if mine else 0.0)
            xoffset (-18 if mine else 12)
            spacing 8
            if not mine:
                add genry_ph_avatar(who, 36) yalign 1.0
            if kind == "text":
                frame:
                    background Frame(("genry_ph bubble_me" if mine else "genry_ph bubble_them"), 22, 22)
                    padding (18, 10, 18, 8)
                    xmaximum 330
                    vbox:
                        spacing 2
                        text body size 23 color "#ffffff" font "fonts/calibri.ttf"
                        hbox:
                            xalign 1.0
                            spacing 5
                            text t size 14 color "#ffffffa0" font "fonts/calibri.ttf" yalign 0.5
                            if mine:
                                add "genry_ph checks" yalign 0.5
            elif kind == "voice":
                frame:
                    background Frame(("genry_ph bubble_me" if mine else "genry_ph bubble_them"), 22, 22)
                    padding (10, 8, 14, 8)
                    hbox:
                        spacing 10
                        imagebutton idle ("genry_ph play_me" if mine else "genry_ph play_them") action Play("sound", body) yalign 0.5
                        add "genry_ph wave" yalign 0.5 at genry_voice_wave
                        vbox:
                            yalign 0.5
                            text status size 17 color "#ffffffdd" font "fonts/calibri.ttf"
                            text t size 13 color "#ffffffa0" font "fonts/calibri.ttf"
            else:
                $ seen = genry_phone_open.get(body)
                if kind == "photo1" and seen:
                    frame:
                        background Frame(("genry_ph bubble_me" if mine else "genry_ph bubble_them"), 22, 22)
                        padding (18, 10, 18, 10)
                        text "Фото просмотрено" size 20 color "#ffffff90" italic True font "fonts/calibri.ttf"
                else:
                    button:
                        background Frame(("genry_ph bubble_me" if mine else "genry_ph bubble_them"), 22, 22)
                        padding (7, 7, 7, 8)
                        action [SetDict(genry_phone_open, body, True), Show("genry_phone_view", img=body, caption=status)]
                        vbox:
                            spacing 4
                            fixed:
                                fit_first True
                                add GenryFit(body, 300, 250, blur=(kind != "photo" and not seen))
                                if kind != "photo" and not seen:
                                    frame:
                                        xalign 0.5
                                        yalign 0.5
                                        background Frame("genry_ph date_pill", 14, 14)
                                        padding (16, 6)
                                        text ("18+  нажми, чтобы открыть" if kind == "photo18" else "1 раз  нажми, чтобы открыть") size 19 color "#ffffff" font "fonts/calibrib.ttf"
                            if status:
                                text status size 21 color "#ffffff" font "fonts/calibri.ttf" xoffset 10 xmaximum 290
                            text t size 13 color "#ffffffa0" font "fonts/calibri.ttf" xalign 1.0 xoffset -8
)rpy";

static const char* const kV1PhoneChat = R"rpy(
screen genry_ph_chat(messages, contact, items=None, anim=True):
    fixed:
        at (genry_phone_in if anim else genry_ph_none)
        xysize (520, 1020)
        xalign 0.5
        yalign 0.5
        fixed:
            pos (24, 19)
            xysize (472, 982)
            add "genry_ph wall"
            $ _rh = (52 + 62 * len(items)) if items else 0
            viewport:
                ypos 136
                xysize (472, 846 - (_rh if _rh else 22))
                mousewheel True
                yinitial 1.0
                vbox:
                    xsize 472
                    spacing 10
                    null height 8
                    frame:
                        xalign 0.5
                        background Frame("genry_ph date_pill", 14, 14)
                        padding (16, 4)
                        text "Сегодня" size 16 color "#ffffffaa" font "fonts/calibri.ttf"
                    for side, who, body, t, status in messages:
                        use genry_ph_msg(side, who, body, t, status)
                    if _genry_phone_typing:
                        hbox:
                            xoffset 12
                            spacing 8
                            add genry_ph_avatar(_genry_phone_typing, 36) yalign 1.0
                            frame:
                                background Frame("genry_ph bubble_them", 22, 22)
                                padding (20, 16)
                                hbox:
                                    spacing 7
                                    add "genry_ph dot" at genry_ph_typing(0.0)
                                    add "genry_ph dot" at genry_ph_typing(0.2)
                                    add "genry_ph dot" at genry_ph_typing(0.4)
                    null height 14
            add "genry_ph head"
            use genry_ph_status(messages[-1][3] if messages else "14:25")
            add "genry_ph back" pos (18, 78)
            add genry_ph_avatar(contact, 52) pos (46, 66)
            text contact pos (110, 64) size 25 color "#ffffff" font "fonts/calibrib.ttf"
            text ("печатает…" if _genry_phone_typing else "в сети") pos (110, 96) size 17 color "#7dd3fc" font "fonts/calibri.ttf"
            add Solid("#ffffff16", xsize=472, ysize=1) ypos 133
            if items:
                frame:
                    at genry_ph_rise
                    ypos 982 - _rh
                    xysize (472, _rh)
                    background Frame("genry_ph panel", 60, 60)
                    padding (24, 10, 24, 12)
                    vbox:
                        spacing 10
                        text "Ответить" size 17 color "#7dd3fc" font "fonts/calibrib.ttf"
                        for i in items:
                            textbutton i.caption:
                                xsize 424
                                ysize 52
                                background Frame("genry_ph pill_line", 26, 26)
                                hover_background Frame("genry_ph pill", 26, 26)
                                text_xalign 0.5
                                text_yalign 0.5
                                text_size 22
                                text_color ("#ffffff90" if i.caption.startswith("(") else "#ffffff")
                                text_font "fonts/calibri.ttf"
                                activate_sound "sound/sfx/click_1.ogg"
                                action [Function(genry_ph_said, i.caption), i.action]
        add "genry_ph body"

screen genry_phone2(messages, contact="СМС"):
    zorder 60
    use genry_ph_chat(messages, contact, None, True)

screen genry_phone_reply(items):
    zorder 61
    modal True
    use genry_ph_chat(_genry_phone, _genry_phone_contact, items, False)

screen genry_phone_view(img, caption=""):
    modal True
    zorder 90
    button:
        xfill True
        yfill True
        background Solid("#000000e6")
        action Hide("genry_phone_view")
        vbox:
            xalign 0.5
            yalign 0.5
            spacing 14
            add GenryFit(img, 1500, 880) xalign 0.5
            if caption:
                text caption xalign 0.5 size 26 color "#ffffff" font "fonts/calibri.ttf"
            text "нажми, чтобы закрыть" xalign 0.5 size 16 color "#9fb3c8" font "fonts/calibri.ttf"

screen genry_ph_banner(who, what):
    zorder 95
    frame:
        at genry_ph_banner_in
        xalign 0.5
        ypos 24
        xsize 620
        background Frame("genry_ph banner", 26, 26)
        padding (18, 14)
        hbox:
            spacing 14
            add genry_ph_avatar(who, 52) yalign 0.5
            vbox:
                yalign 0.5
                spacing 2
                hbox:
                    spacing 10
                    text who size 22 color "#ffffff" font "fonts/calibrib.ttf"
                    text "сейчас" size 16 color "#9fb3c8" font "fonts/calibri.ttf" yalign 0.5
                text what size 20 color "#dde8f5" font "fonts/calibri.ttf" xmaximum 520
    timer 3.8 action Hide("genry_ph_banner")
)rpy";

static const char* const kV1PhoneCall = R"rpy(
screen genry_phone_call(who, face=None, secs=0):
    modal True
    zorder 80
    add Solid("#000000aa")
    fixed:
        at genry_phone_in
        xysize (520, 1020)
        xalign 0.5
        yalign 0.5
        fixed:
            pos (24, 19)
            xysize (472, 982)
            add "genry_ph wall"
            if face:
                add AlphaMask(GenryFit(face, 472, 472, blur=True), "genry_ph softmask") xalign 0.5 yalign 0.35 alpha 0.55
            add Frame("genry_ph shade", 60, 60) xysize (472, 982)
            use genry_ph_status("14:26")
            text "Входящий вызов" xalign 0.5 ypos 70 size 20 color "#ffffffaa" font "fonts/calibri.ttf"
            text who xalign 0.5 ypos 98 size 46 color "#ffffff" font "fonts/calibrib.ttf"
            text "мобильный" xalign 0.5 ypos 156 size 18 color "#ffffff80" font "fonts/calibri.ttf"
            fixed:
                xysize (320, 320)
                xalign 0.5
                ypos 250
                add "genry_ph ring" at genry_ph_ripple(0.0)
                add "genry_ph ring" at genry_ph_ripple(0.6)
                add "genry_ph ring" at genry_ph_ripple(1.2)
                if face:
                    add AlphaMask(Transform(face, zoom=0.667), Transform("genry_ph mask", zoom=1.0)) xalign 0.5 yalign 0.5
                else:
                    add genry_ph_avatar(who, 200) xalign 0.5 yalign 0.5
            vbox:
                xpos 70
                ypos 770
                spacing 8
                imagebutton idle "genry_ph call_no" hover Transform("genry_ph call_no", zoom=1.06) action Return("decline") activate_sound "sound/sfx/click_1.ogg"
                text "Сбросить" xalign 0.5 size 18 color "#ffffff" font "fonts/calibri.ttf"
            vbox:
                xpos 306
                ypos 770
                spacing 8
                imagebutton idle "genry_ph call_yes" hover Transform("genry_ph call_yes", zoom=1.06) action Return("accept") activate_sound "sound/sfx/click_1.ogg"
                text "Принять" xalign 0.5 size 18 color "#ffffff" font "fonts/calibri.ttf"
        add "genry_ph body"
    if secs:
        timer secs action Return("missed")
)rpy";

static const char* const kV1PhoneFeed = R"rpy(
screen genry_phone_feed(feed, title="Лента лагеря"):
    modal True
    zorder 70
    fixed:
        at genry_phone_in
        xysize (520, 1020)
        xalign 0.5
        yalign 0.5
        fixed:
            pos (24, 19)
            xysize (472, 982)
            add "genry_ph wall"
            viewport:
                ypos 136
                xysize (472, 846)
                mousewheel True
                vbox:
                    xsize 472
                    spacing 14
                    null height 6
                    if not feed:
                        text "Пока ни одного поста" xalign 0.5 size 22 color "#9fb3c8" font "fonts/calibri.ttf"
                    for post in feed:
                        frame:
                            xalign 0.5
                            xsize 448
                            background Frame("genry_ph card", 22, 22)
                            padding (16, 14)
                            vbox:
                                spacing 10
                                hbox:
                                    spacing 12
                                    add genry_ph_avatar(post["who"], 44) yalign 0.5
                                    vbox:
                                        yalign 0.5
                                        text post["who"] size 22 color "#ffffff" font "fonts/calibrib.ttf"
                                        text "только что" size 14 color "#9fb3c8" font "fonts/calibri.ttf"
                                if post["text"]:
                                    text post["text"] size 21 color "#dde8f5" font "fonts/calibri.ttf" xmaximum 416
                                if post["img"]:
                                    button:
                                        padding (0, 0)
                                        background None
                                        action Show("genry_phone_view", img=post["img"], caption=post["text"])
                                        add GenryFit(post["img"], 416, 300)
                                hbox:
                                    spacing 16
                                    imagebutton:
                                        idle ("genry_ph heart_full" if post["liked"] else "genry_ph heart")
                                        yalign 0.5
                                        action [SetDict(post, "likes", post["likes"] + (-1 if post["liked"] else 1)), SetDict(post, "liked", not post["liked"])]
                                    text str(post["likes"]) size 20 color ("#ff6b8b" if post["liked"] else "#9fb3c8") font "fonts/calibri.ttf" yalign 0.5
                                    if post.get("comments"):
                                        text ("комментарии: %d" % len(post["comments"])) size 17 color "#9fb3c8" font "fonts/calibri.ttf" yalign 0.5
                                for cw, ct in post.get("comments", []):
                                    hbox:
                                        spacing 8
                                        add genry_ph_avatar(cw, 28) yalign 0.0
                                        text ("{b}" + cw + "{/b}  " + ct) size 18 color "#c9d6e6" font "fonts/calibri.ttf" xmaximum 370
                    null height 12
            add "genry_ph head"
            use genry_ph_status("14:30")
            text title pos (30, 72) size 30 color "#7dd3fc" font "fonts/calibrib.ttf"
            textbutton "Закрыть":
                pos (360, 78)
                background None
                hover_background None
                text_font "fonts/calibri.ttf"
                text_size 20
                text_color "#9fb3c8"
                text_hover_color "#ffffff"
                activate_sound "sound/sfx/click_1.ogg"
                action Return()
            add Solid("#ffffff16", xsize=472, ysize=1) ypos 133
        add "genry_ph body"
)rpy";

static const char* const kV1PhoneHome = R"rpy(
screen genry_phone_home(feed=None):
    modal True
    zorder 70
    default app = "home"
    fixed:
        at genry_phone_in
        xysize (520, 1020)
        xalign 0.5
        yalign 0.5
        fixed:
            pos (24, 19)
            xysize (472, 982)
            add "genry_ph wall"
            use genry_ph_status(_genry_phone[-1][3] if _genry_phone else "14:25")
            if app == "home":
                text (_genry_phone[-1][3] if _genry_phone else "14:25") xalign 0.5 ypos 90 size 92 color "#ffffff" font "fonts/calibrib.ttf"
                text "Лагерь «Совёнок»" xalign 0.5 ypos 200 size 22 color "#ffffffcc" font "fonts/calibri.ttf"
                $ _unread = len([m for m in _genry_phone if m[0].startswith("them")])
                $ _apps = [("app_msg", "Сообщения", "chat", _unread), ("app_feed", "Лента", "feed", len(feed or [])),
                           ("app_gallery", "Галерея", "gallery", len(genry_ph_photos)), ("app_calls", "Звонки", "calls", 0),
                           ("app_camera", "Камера", None, 0), ("app_music", "Музыка", None, 0), ("app_notes", "Заметки", None, 0), ("app_map", "Карта", None, 0)]
                grid 4 2:
                    xalign 0.5
                    ypos 300
                    spacing 26
                    for icon, label, target, count in _apps:
                        vbox:
                            spacing 6
                            fixed:
                                xysize (76, 76)
                                imagebutton idle ("genry_ph " + icon) hover Transform("genry_ph " + icon, zoom=1.06) action (SetScreenVariable("app", target) if target else NullAction()) activate_sound "sound/sfx/click_1.ogg"
                                if count:
                                    add "genry_ph badge" xpos 56 ypos -8
                                    text str(min(count, 99)) xpos 70 ypos -3 xanchor 0.5 size 15 color "#ffffff" font "fonts/calibrib.ttf"
                            text label xalign 0.5 size 15 color "#ffffff" font "fonts/calibri.ttf"
                frame:
                    xalign 0.5
                    ypos 830
                    xysize (430, 110)
                    background Frame("genry_ph dock", 34, 34)
                    textbutton "Закрыть телефон":
                        xalign 0.5
                        yalign 0.5
                        background None
                        text_font "fonts/calibrib.ttf"
                        text_size 22
                        text_color "#ffffff"
                        text_hover_color "#7dd3fc"
                        activate_sound "sound/sfx/click_1.ogg"
                        action Return()
            else:
                add "genry_ph head"
                use genry_ph_status(_genry_phone[-1][3] if _genry_phone else "14:25")
                imagebutton idle "genry_ph back" pos (18, 78) action SetScreenVariable("app", "home")
                $ _ttl = {"chat": "Сообщения", "feed": "Лента", "gallery": "Галерея", "calls": "Звонки"}.get(app, "")
                text _ttl pos (48, 72) size 28 color "#ffffff" font "fonts/calibrib.ttf"
                viewport:
                    ypos 140
                    xysize (472, 842)
                    mousewheel True
                    vbox:
                        xsize 472
                        spacing 10
                        if app == "chat":
                            for side, who, body, t, status in _genry_phone:
                                use genry_ph_msg(side, who, body, t, status)
                        elif app == "feed":
                            for post in (feed or []):
                                frame:
                                    xalign 0.5
                                    xsize 448
                                    background Frame("genry_ph card", 22, 22)
                                    padding (16, 12)
                                    vbox:
                                        spacing 8
                                        text ("{b}" + post["who"] + "{/b}  " + post["text"]) size 20 color "#dde8f5" font "fonts/calibri.ttf" xmaximum 416
                                        if post["img"]:
                                            add GenryFit(post["img"], 416, 260)
                        elif app == "gallery":
                            if not genry_ph_photos:
                                text "Фото пока нет" xalign 0.5 size 22 color "#9fb3c8" font "fonts/calibri.ttf"
                            else:
                              $ _rows = (len(genry_ph_photos) + 2) // 3
                              grid 3 _rows:
                                xalign 0.5
                                spacing 6
                                for img, cap in genry_ph_photos:
                                    button:
                                        padding (0, 0)
                                        background None
                                        action Show("genry_phone_view", img=img, caption=cap)
                                        add GenryFit(img, 146, 146)
                                for _i in range((3 - len(genry_ph_photos) % 3) % 3):
                                    null
                        else:
                            for side, who, body, t, status in _genry_phone:
                                if side == "call":
                                    text (t + "   " + who + " · " + body) size 20 color "#dde8f5" font "fonts/calibri.ttf" xoffset 24
        add "genry_ph body"
)rpy";
