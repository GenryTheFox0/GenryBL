# GenryBL: the player's status in Discord while Everlasting Summer runs - the day and time of day of the original
# story and whose route it is, the main menu, or the mod they are playing (its name, its chapter). GenryBL puts this
# folder into game/mods (Центр → «Статус в Discord в самой игре») and the same switch takes it away.
#
# Discord's local pipe (\\.\pipe\discord-ipc-0…9) straight from Python, no libraries: frames are {uint32 op, uint32
# length} little-endian + JSON, HANDSHAKE -> READY -> SET_ACTIVITY. One worker thread does all the pipe work (a pipe
# opened like this is synchronous: a read waiting in one thread would block the writes of another), the game only
# hands it the newest status. Python 2 (the game's Ren'Py 7) and Python 3 (the renpy8 branch) alike.
# genry_presence.json next to this file: {"client_id": "...", "lang": "ru"} (GenryBL writes it).

init 999 python:
    def _genry_presence_install(cfg=None):
        import os, json, time, struct, threading, re
        try:
            import Queue as queue_mod
        except ImportError:
            import queue as queue_mod

        if cfg is None:
            cfg = {}
            try:
                here = os.path.join(config.gamedir, "mods", "genry_presence", "genry_presence.json")
                with open(here, "rb") as f:
                    cfg = json.loads(f.read().decode("utf-8"))
            except Exception:
                cfg = {}
        client_id = str(cfg.get("client_id") or "451544422711033856")   # Discord's own «Everlasting Summer»
        gap = float(cfg.get("gap", 15.0))                                 # Discord takes about one update in 15 s
        pipe_override = cfg.get("pipe") or ""
        every = float(cfg.get("tick", 3.0))                               # how often the game looks where the player is

        T = {
            "ru": ["В главном меню", "Мод «{0}»", "День {0}", "День {0} · {1}", "день", "вечер", "ночь", "пролог", "Пролог", "Эпилог", "Путь: {0}", "Оригинальная история", "В игре", "Алиса", "Славя", "Лена", "Ульяна", "Мику", "Юля"],
            "en": ["In the main menu", "Mod «{0}»", "Day {0}", "Day {0} · {1}", "day", "evening", "night", "prologue", "Prologue", "Epilogue", "Route: {0}", "The original story", "Playing", "Alisa", "Slavya", "Lena", "Ulyana", "Miku", "Yulya"],
            "uk": ["У головному меню", "Мод «{0}»", "День {0}", "День {0} · {1}", "день", "вечір", "ніч", "пролог", "Пролог", "Епілог", "Шлях: {0}", "Оригінальна історія", "У грі", "Аліса", "Славя", "Лена", "Уляна", "Міку", "Юля"],
            "be": ["У галоўным меню", "Мод «{0}»", "Дзень {0}", "Дзень {0} · {1}", "дзень", "вечар", "ноч", "пралог", "Пралог", "Эпілог", "Шлях: {0}", "Арыгінальная гісторыя", "У гульні", "Аліса", "Славя", "Лена", "Ульяна", "Міку", "Юля"],
            "kk": ["Басты мәзірде", "«{0}» моды", "{0}-күн", "{0}-күн · {1}", "күндіз", "кеш", "түн", "пролог", "Пролог", "Эпилог", "Жол: {0}", "Түпнұсқа оқиға", "Ойында", "Алиса", "Славя", "Лена", "Ульяна", "Мику", "Юля"],
            "pl": ["W menu głównym", "Mod «{0}»", "Dzień {0}", "Dzień {0} · {1}", "dzień", "wieczór", "noc", "prolog", "Prolog", "Epilog", "Ścieżka: {0}", "Oryginalna historia", "W grze", "Alisa", "Sławia", "Lena", "Ulana", "Miku", "Julia"],
            "cs": ["V hlavním menu", "Mod «{0}»", "Den {0}", "Den {0} · {1}", "den", "večer", "noc", "prolog", "Prolog", "Epilog", "Cesta: {0}", "Původní příběh", "Ve hře", "Alisa", "Slavja", "Lena", "Uljana", "Miku", "Julja"],
            "de": ["Im Hauptmenü", "Mod «{0}»", "Tag {0}", "Tag {0} · {1}", "Tag", "Abend", "Nacht", "Prolog", "Prolog", "Epilog", "Route: {0}", "Die Originalgeschichte", "Im Spiel", "Alisa", "Slavya", "Lena", "Ulyana", "Miku", "Yulya"],
            "fr": ["Dans le menu principal", "Mod «{0}»", "Jour {0}", "Jour {0} · {1}", "jour", "soir", "nuit", "prologue", "Prologue", "Épilogue", "Route : {0}", "L’histoire originale", "En jeu", "Alisa", "Slavya", "Lena", "Ulyana", "Miku", "Yulya"],
            "es": ["En el menú principal", "Mod «{0}»", "Día {0}", "Día {0} · {1}", "día", "tarde", "noche", "prólogo", "Prólogo", "Epílogo", "Ruta: {0}", "La historia original", "Jugando", "Alisa", "Slavya", "Lena", "Ulyana", "Miku", "Yulya"],
            "pt_BR": ["No menu principal", "Mod «{0}»", "Dia {0}", "Dia {0} · {1}", "dia", "fim de tarde", "noite", "prólogo", "Prólogo", "Epílogo", "Rota: {0}", "A história original", "Jogando", "Alisa", "Slavya", "Lena", "Ulyana", "Miku", "Yulya"],
            "it": ["Nel menu principale", "Mod «{0}»", "Giorno {0}", "Giorno {0} · {1}", "giorno", "sera", "notte", "prologo", "Prologo", "Epilogo", "Percorso: {0}", "La storia originale", "In gioco", "Alisa", "Slavya", "Lena", "Ulyana", "Miku", "Yulya"],
            "tr": ["Ana menüde", "«{0}» modu", "{0}. gün", "{0}. gün · {1}", "gündüz", "akşam", "gece", "önsöz", "Önsöz", "Sonsöz", "Rota: {0}", "Orijinal hikâye", "Oyunda", "Alisa", "Slavya", "Lena", "Ulyana", "Miku", "Yulya"],
            "ja": ["メインメニュー", "MOD「{0}」", "{0}日目", "{0}日目 · {1}", "昼", "夕方", "夜", "プロローグ", "プロローグ", "エピローグ", "ルート:{0}", "オリジナルストーリー", "プレイ中", "アリサ", "スラーヴャ", "レーナ", "ウリヤーナ", "ミク", "ユーリャ"],
            "zh_CN": ["主菜单", "模组「{0}」", "第{0}天", "第{0}天 · {1}", "白天", "傍晚", "夜晚", "序章", "序章", "尾声", "路线:{0}", "原版故事", "游戏中", "阿丽莎", "斯拉维娅", "列娜", "乌里扬娜", "未来", "尤莉娅"],
            "zh_TW": ["主選單", "模組「{0}」", "第{0}天", "第{0}天 · {1}", "白天", "傍晚", "夜晚", "序章", "序章", "尾聲", "路線:{0}", "原版故事", "遊戲中", "阿麗莎", "斯拉維婭", "列娜", "烏里揚娜", "未來", "尤莉婭"],
            "ko": ["메인 메뉴", "모드 «{0}»", "{0}일째", "{0}일째 · {1}", "낮", "저녁", "밤", "프롤로그", "프롤로그", "에필로그", "루트: {0}", "원작 이야기", "플레이 중", "알리사", "슬라뱌", "레나", "울리야나", "미쿠", "율랴"],
            "vi": ["Ở menu chính", "Mod «{0}»", "Ngày {0}", "Ngày {0} · {1}", "ban ngày", "chiều tối", "đêm", "mở đầu", "Mở đầu", "Kết", "Tuyến: {0}", "Câu chuyện gốc", "Đang chơi", "Alisa", "Slavya", "Lena", "Ulyana", "Miku", "Yulya"],
            "id": ["Di menu utama", "Mod «{0}»", "Hari {0}", "Hari {0} · {1}", "siang", "sore", "malam", "prolog", "Prolog", "Epilog", "Rute: {0}", "Cerita asli", "Sedang main", "Alisa", "Slavya", "Lena", "Ulyana", "Miku", "Yulya"],
            "th": ["อยู่ที่เมนูหลัก", "ม็อด «{0}»", "วันที่ {0}", "วันที่ {0} · {1}", "กลางวัน", "เย็น", "กลางคืน", "บทนำ", "บทนำ", "บทส่งท้าย", "เส้นทาง: {0}", "เรื่องต้นฉบับ", "กำลังเล่น", "อลิซา", "สลาวยา", "เลนา", "อุลยานา", "มิกุ", "ยูลยา"],
        }
        keys = ["menu", "mod", "day", "daytime", "tod_day", "tod_sunset", "tod_night", "tod_prolog", "prologue", "epilogue", "route", "orig", "playing", "dv", "sl", "un", "us", "mi", "uv"]
        es_lang = {None: "ru", "english": "en", "spanish": "es", "italian": "it", "chinese": "zh_CN", "french": "fr", "portuguese": "pt_BR", "turkish": "tr"}

        def words():
            lang = cfg.get("lang") or ""
            if lang not in T:
                try:
                    lang = es_lang.get(_preferences.language, "en")
                except Exception:
                    lang = "ru"
            row = T.get(lang, T["en"])
            return dict(zip(keys, row))

        tags = re.compile(r"\{[^}]*\}")
        def clean(s):
            try:
                s = s if isinstance(s, type(u"")) else s.decode("utf-8")
            except Exception:
                s = u"%s" % (s,)
            return tags.sub(u"", s).strip()

        def store(name, default=None):
            return getattr(renpy.store, name, default)

        # the mod a script file belongs to: "…/workshop/content/331470/<id>/" or "…mods/<folder>/"
        workshop = re.compile(r"workshop/content/331470/(\d+)/")
        local = re.compile(r"(?:^|/)mods/([^/]+)/")
        def mod_key(fn):
            m = workshop.search(fn)
            if m:
                return "ws:" + m.group(1)
            m = local.search(fn)
            return ("dir:" + m.group(1)) if m else ""

        names = {}
        def mod_name(key):
            if not names:
                for lbl, nm in dict(store("mods", {}) or {}).items():
                    node = renpy.game.script.namemap.get(lbl)
                    k = mod_key((getattr(node, "filename", "") or "").replace("\\", "/"))
                    if k and k not in names:
                        names[k] = clean(nm) or lbl
                if not names:
                    names[""] = ""
            return names.get(key) or key.split(":", 1)[-1]

        def where():
            try:
                node = renpy.game.script.namemap.get(renpy.game.contexts[0].current)
                return (getattr(node, "filename", "") or "").replace("\\", "/")
            except Exception:
                return ""

        day_re = re.compile(r"scenario/day(\d)")
        def status():
            W = words()
            if store("main_menu", False):
                return {"details": W["menu"]}
            fn = where()
            key = mod_key(fn)
            if key:
                act = {"details": W["mod"].format(mod_name(key))}
                chapter = clean(store("save_name", u"") or u"")
                if chapter:
                    act["state"] = chapter
                return act
            if "scenario/" in fn:
                tod = W.get("tod_" + str(store("time_of_day", "")), "")
                m = day_re.search(fn)
                if m:
                    details = W["daytime"].format(m.group(1), tod) if tod else W["day"].format(m.group(1))
                elif "prologue" in fn:
                    details = W["prologue"]
                else:
                    details = W["epilogue"]
                who = str(store("backdrop", ""))
                return {"details": details, "state": W["route"].format(W[who]) if who in ("dv", "sl", "un", "us") else W["orig"]}
            return {"details": W["playing"]}

        # ---- the pipe (one worker thread)
        class Pipe(object):
            def __init__(self):
                self.fd = None
            def close(self):
                if self.fd is not None:
                    try:
                        os.close(self.fd)
                    except Exception:
                        pass
                self.fd = None
            def send(self, op, obj):
                body = json.dumps(obj, separators=(",", ":")).encode("ascii")   # \uXXXX: the same bytes on 2 and 3
                os.write(self.fd, struct.pack("<II", op, len(body)) + body)
            def waiting(self):
                import ctypes, msvcrt
                n = ctypes.c_ulong(0)
                if not ctypes.windll.kernel32.PeekNamedPipe(ctypes.c_void_p(msvcrt.get_osfhandle(self.fd)), None, 0, None, ctypes.byref(n), None):
                    raise IOError("the pipe is gone")
                return n.value
            def read(self, n, until):
                data = b""
                while len(data) < n:
                    if not self.waiting():
                        if time.time() > until:
                            raise IOError("Discord does not answer")
                        time.sleep(0.05)
                        continue
                    chunk = os.read(self.fd, n - len(data))
                    if not chunk:
                        raise IOError("the pipe is closed")
                    data += chunk
                return data
            def frame(self, timeout):
                until = time.time() + timeout
                op, size = struct.unpack("<II", self.read(8, until))
                return op, json.loads(self.read(size, until).decode("utf-8") or "{}")
            def answer(self, timeout):
                # the reply to what was sent; PING gets its PONG, CLOSE ends the talk
                while True:
                    op, data = self.frame(timeout)
                    if op == 3:
                        self.send(4, data)
                        continue
                    if op == 2:
                        raise IOError("closed: %s" % data.get("message", ""))
                    return op, data
            def open(self):
                for name in ([pipe_override] if pipe_override else [r"\\.\pipe\discord-ipc-%d" % i for i in range(10)]):
                    try:
                        self.fd = os.open(name, os.O_RDWR | getattr(os, "O_BINARY", 0))
                    except OSError:
                        self.fd = None
                        continue
                    try:
                        self.send(0, {"v": 1, "client_id": client_id})
                        op, data = self.answer(5.0)
                        if op == 1 and data.get("evt") == "READY":
                            return True
                    except Exception:
                        pass
                    self.close()
                return False

        inbox = queue_mod.Queue()
        started = int(time.time())

        def worker():
            pipe = Pipe()
            pending, last, nonce, retry = None, 0.0, 0, 0.0
            while True:
                try:
                    item = inbox.get(timeout=1.0)
                    while True:                        # only the newest status matters
                        pending = item
                        item = inbox.get_nowait()
                except queue_mod.Empty:
                    pass
                try:
                    if pipe.fd is None:
                        if time.time() < retry or not pipe.open():
                            retry = max(retry, time.time() + 20)
                            continue
                        last = 0.0
                    elif pipe.waiting():
                        pipe.answer(1.0)               # a PING between our updates
                    if pending is not None and time.time() - last >= gap:
                        act = dict(pending)
                        for k in ("details", "state"):
                            if k in act:
                                act[k] = act[k][:128] if len(act[k]) >= 2 else act[k] + u"  "
                        act["timestamps"] = {"start": started}
                        nonce += 1
                        pipe.send(1, {"cmd": "SET_ACTIVITY", "args": {"pid": os.getpid(), "activity": act}, "nonce": str(nonce)})
                        pipe.answer(5.0)
                        pending, last = None, time.time()
                except Exception:
                    pipe.close()
                    retry = time.time() + 20

        thread = threading.Thread(target=worker, name="genry_presence")
        thread.daemon = True

        # the game side: every few seconds, the status of the moment - to the worker only when it changed
        state = {"last": None, "at": 0.0}
        def tick():
            now = time.time()
            if now - state["at"] < every:
                return
            state["at"] = now
            try:
                st = status()
            except Exception:
                return
            if st != state["last"]:
                state["last"] = st
                inbox.put(st)
            if not thread.is_alive() and not state.get("started"):
                state["started"] = True
                thread.start()
        config.periodic_callbacks.append(tick)
        return tick

    if renpy.windows:
        _genry_presence_install()
