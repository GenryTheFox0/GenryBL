# GenryBL gate: «genry_smoke» - a command of Everlasting Summer itself (like «lint»), put into the game only while
# `gb_cli gate` runs. The game's own lint never looks inside screens or «$» lines, so a mod could pass it and
# still die with NameError the moment its menu shows (V2.0). Here the game - after its whole init, with every
# workshop mod loaded - checks the listed mods for real:
#   1) every global name the mod's python reads («$», python blocks, define/default, if/while/menu conditions)
#      exists: in the store, in builtins, or the mod sets it somewhere;
#   2) every screen of the mod is built once (ScreenDisplayable.update) with the arguments the mod really calls it
#      with («call screen x(…)», «show screen x(…)»); a screen nobody calls is built with its defaults.
#   3) the game's own lint checks, on the mods' statements only (the whole game with the workshop takes minutes).
# The report goes to GENRY_SMOKE_OUT; the mods are GENRY_SMOKE_MODS (comma separated).
# It runs on both branches of the game in Steam: the usual one (Ren'Py 7, Python 2) and «renpy8» (Python 3).
init 1999 python:
    import os as _genry_gate_os

    def _genry_gate_smoke():
        import io, re, sys, dis, traceback, types
        PY3 = sys.version_info[0] >= 3
        if PY3:
            import builtins as B
        else:
            import __builtin__ as B
        # this function lives in the store, where any mod may have put a Character called «all» or «set»:
        # the builtins it uses are taken from the source, the nested functions see these through the closure
        all, any, set, sorted, list, dict, len, str, repr, type, isinstance, hasattr, getattr, eval, compile, \
            ord, range, enumerate, Exception, SyntaxError, object, sum, max, min = \
            B.all, B.any, B.set, B.sorted, B.list, B.dict, B.len, B.str, B.repr, B.type, B.isinstance, B.hasattr, \
            B.getattr, B.eval, B.compile, B.ord, B.range, B.enumerate, B.Exception, B.SyntaxError, B.object, B.sum, B.max, B.min
        unicode = B.str if PY3 else B.unicode
        out = io.open(_genry_gate_os.environ.get("GENRY_SMOKE_OUT", "genry_smoke.txt"), "w", encoding="utf-8")
        mods = [m for m in _genry_gate_os.environ.get("GENRY_SMOKE_MODS", "").split(",") if m]

        def text(x):
            if isinstance(x, unicode):
                return x
            if PY3:
                return str(x)
            try:
                return unicode(str(x), "utf-8", "replace")
            except Exception:
                return repr(x).decode("ascii", "replace")

        def say(*parts):
            out.write(u" ".join([text(p) for p in parts]) + u"\n")
            out.flush()

        def owner(fn):
            fn = (fn or "").replace("\\", "/")
            for m in mods:
                if ("mods/" + m + "/") in fn:
                    return m
            return None

        try:
            renpy.exports.execute_default_statement(True)
        except Exception as e:
            say(u"WARN defaults:", repr(e))

        # ---- 1) names read by python code
        def code_objects(co):
            yield co
            for c in co.co_consts:
                if isinstance(c, types.CodeType):
                    for x in code_objects(c):
                        yield x

        def names_of(co):
            # «x = 0» defines x, «x += 1» only works if x is there: a name counts as set only when it is stored
            # before it is first read (a list comprehension stores its variable first)
            reads, stores = set(), set()
            if PY3:
                for ins in dis.get_instructions(co):
                    if ins.opname in ("LOAD_NAME", "LOAD_GLOBAL"):
                        if ins.argval not in stores:
                            reads.add(ins.argval)
                    elif ins.opname in ("STORE_NAME", "STORE_GLOBAL", "DELETE_NAME", "IMPORT_NAME"):
                        if ins.argval not in reads:
                            stores.add(ins.argval)
                return reads, stores
            code = co.co_code
            i, n, ext = 0, len(code), 0
            while i < n:
                op = ord(code[i])
                if op >= dis.HAVE_ARGUMENT:
                    arg = ord(code[i + 1]) + ord(code[i + 2]) * 256 + ext
                    ext = 0
                    i += 3
                    if op == dis.EXTENDED_ARG:
                        ext = arg * 65536
                        continue
                    name = dis.opname[op]
                    if name in ("LOAD_NAME", "LOAD_GLOBAL"):
                        if co.co_names[arg] not in stores:
                            reads.add(co.co_names[arg])
                    elif name in ("STORE_NAME", "STORE_GLOBAL", "DELETE_NAME", "IMPORT_NAME"):
                        if co.co_names[arg] not in reads:
                            stores.add(co.co_names[arg])
                else:
                    i += 1
            return reads, stores

        stmts = [s for s in renpy.game.script.all_stmts if owner(getattr(s, "filename", ""))]
        sources = []                                  # (statement, source, mode)
        calls = {}                                    # screen -> [argument text]
        for s in stmts:
            if isinstance(s, (renpy.ast.Python, renpy.ast.EarlyPython)):
                sources.append((s, s.code.source, "exec"))
            elif isinstance(s, (renpy.ast.Define, renpy.ast.Default)):
                sources.append((s, s.code.source, "eval"))
            elif isinstance(s, renpy.ast.If):
                for cond, _block in s.entries:
                    sources.append((s, cond, "eval"))
            elif isinstance(s, renpy.ast.While):
                sources.append((s, s.condition, "eval"))
            elif isinstance(s, renpy.ast.Menu):
                for item in s.items:
                    if len(item) > 1 and item[1]:
                        sources.append((s, item[1], "eval"))
            elif isinstance(s, renpy.ast.UserStatement):
                m = re.match(r"\s*(?:call|show)\s+screen\s+([A-Za-z_][\w]*)\s*(\(.*)?$", s.line or "")
                if m:
                    args = u"()"
                    rest = m.group(2) or u""
                    if rest.startswith(u"("):
                        depth, quote, end = 0, None, -1
                        for k, ch in enumerate(rest):
                            if quote:
                                if ch == quote and rest[k - 1] != u"\\":
                                    quote = None
                            elif ch in u"\"'":
                                quote = ch
                            elif ch == u"(":
                                depth += 1
                            elif ch == u")":
                                depth -= 1
                                if depth == 0:
                                    end = k
                                    break
                        if end > 0:
                            args = rest[:end + 1]
                    calls.setdefault(m.group(1), []).append((s, args))
        reads, sets = [], set()
        for s, src, mode in sources:
            if isinstance(s, (renpy.ast.Define, renpy.ast.Default)):
                sets.add(s.varname)
            try:
                co = compile(src, s.filename, mode)
            except SyntaxError as e:
                say(u"ERROR", s.filename, s.linenumber, u"python syntax:", repr(e))
                continue
            for c in code_objects(co):
                r, st = names_of(c)
                sets.update(st)
                reads.append((s, r if c is co else set(x for x in r if x not in c.co_varnames)))
        known = set(["renpy", "store", "config", "persistent", "_preferences", "ui", "im", "anim", "layout", "theme", "build", "iap", "achievement", "gui", "director"])
        reported = set()
        for s, names in reads:
            for n in sorted(names):
                if n in sets or n in known or hasattr(renpy.store, n) or hasattr(B, n):
                    continue
                if (s.filename, s.linenumber, n) in reported:
                    continue
                reported.add((s.filename, s.linenumber, n))
                say(u"ERROR", s.filename, s.linenumber, u"NameError: name '%s' is not defined" % n)

        # ---- 2) every screen of the mods, built once
        tested = 0
        for key, screen in list(renpy.display.screen.screens.items()):
            ast = getattr(screen, "ast", None)
            loc = getattr(ast, "location", None) if ast is not None else None
            if not loc or not owner(loc[0]):
                continue
            name = key[0]
            samples = [a for (_s, a) in calls.get(name, [])]
            if not samples:
                params = screen.parameters
                if params is None or all(p[1] is not None for p in params.parameters):
                    samples = [u"()"]
            if not samples:
                say(u"SKIP", loc[0], loc[1], u"screen %s: nobody calls it" % name)
                continue
            for args in sorted(set(samples))[:6]:
                try:
                    a, kw = eval(u"(lambda *a, **k: (a, k))" + args, renpy.store.__dict__)
                    kw.pop("_layer", None)
                    kw.pop("_zorder", None)
                    kw.pop("_tag", None)
                    scope = {}
                    if screen.parameters:
                        scope["_kwargs"] = kw
                        scope["_args"] = a
                    else:
                        scope.update(kw)
                    d = renpy.display.screen.ScreenDisplayable(screen, name, "screens", {}, scope)
                    d.phase = renpy.display.screen.SHOW
                    d.update()
                    tested += 1
                except Exception as e:
                    tb = [tuple(t)[:2] for t in traceback.extract_tb(sys.exc_info()[2])]
                    where = [t for t in tb if owner(t[0])]
                    fn, ln = (where[-1][0], where[-1][1]) if where else (loc[0], loc[1])
                    say(u"ERROR", fn, ln, u"screen %s%s: %s: %s" % (name, args, type(e).__name__, text(e)))
                finally:
                    renpy.display.screen.updated_screens.clear()
        # ---- 3) the game's own lint, on the mods' statements only (the whole game + workshop takes minutes)
        L = renpy.lint
        class Catch(object):
            def __init__(self):
                self.parts = []
            def write(self, x):
                self.parts.append(text(x))
            def flush(self):
                pass
        catch, real = Catch(), sys.stdout
        sys.stdout = catch
        try:
            renpy.game.lint = True
            L.image_prefixes = {}
            for k in renpy.display.image.images:
                L.image_prefixes[k[0]] = True
            for node in renpy.game.script.all_stmts:
                if isinstance(node, (renpy.ast.Show, renpy.ast.Scene)):
                    L.precheck_show(node)
            checks = [
                (renpy.ast.Image, lambda n: L.check_image(n)),
                (renpy.ast.Show, lambda n: L.check_show(n, False)),
                (renpy.ast.Scene, lambda n: L.check_show(n, True)),
                (renpy.ast.Hide, L.check_hide),
                (renpy.ast.With, L.check_with),
                (renpy.ast.Say, L.check_say),
                (renpy.ast.Menu, L.check_menu),
                (renpy.ast.Jump, L.check_jump),
                (renpy.ast.Call, L.check_call),
                (renpy.ast.While, L.check_while),
                (renpy.ast.If, L.check_if),
                (renpy.ast.UserStatement, L.check_user),
                (renpy.ast.Label, L.check_label),
                (renpy.ast.Screen, L.check_screen),
                (renpy.ast.Define, lambda n: (L.check_define(n, "define"), L.check_redefined(n, "define"))),
                (renpy.ast.Default, lambda n: (L.check_define(n, "default"), L.check_redefined(n, "default"))),
            ]
            for node in sorted(stmts, key=lambda n: (n.filename, n.linenumber)):
                for kind, fn in checks:
                    if isinstance(node, kind):
                        L.report_node = node
                        try:
                            fn(node)
                        except Exception as e:
                            print(u"")
                            print(u"%s:%d lint crashed on this line: %r" % (node.filename, node.linenumber, e))
                        break
            L.report_node = None
        finally:
            sys.stdout = real
        linted = 0
        for line in u"".join(catch.parts).split(u"\n"):
            line = line.rstrip()
            if not line:
                continue
            if re.match(r"^\S.*\.rpym?:\d+ ", line):
                say(u"LINT", line)
                linted += 1
            else:
                say(u"LINT+", line)
        say(u"SMOKE DONE", len(stmts), u"statements,", tested, u"screens built,", linted, u"lint lines")
        out.close()
        return False

    renpy.arguments.register_command("genry_smoke", _genry_gate_smoke)
