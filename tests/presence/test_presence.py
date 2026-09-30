# -*- coding: utf-8 -*-
"""The in-game Discord status (data/presence/genry_presence.rpy) without the game and without Discord: its python block
runs against stand-ins for Ren'Py, and a named pipe server here plays Discord. Runs under the game's own Python 2.7
(lib/windows-x86_64/python.exe) and under Python 3 - the mod lives in both.
    python tests/presence/test_presence.py"""
from __future__ import print_function
import ctypes, io, json, os, re, struct, sys, tempfile, textwrap, threading, time
from ctypes import wintypes

HERE = os.path.dirname(os.path.abspath(__file__))
RPY = os.path.join(HERE, '..', '..', 'data', 'presence', 'genry_presence.rpy')
PIPE = r'\\.\pipe\genry-presence-test-%d' % os.getpid()

k32 = ctypes.windll.kernel32
k32.CreateNamedPipeW.restype = wintypes.HANDLE
k32.CreateNamedPipeW.argtypes = [wintypes.LPCWSTR, wintypes.DWORD, wintypes.DWORD, wintypes.DWORD, wintypes.DWORD, wintypes.DWORD, wintypes.DWORD, ctypes.c_void_p]
k32.ConnectNamedPipe.argtypes = [wintypes.HANDLE, ctypes.c_void_p]
k32.ReadFile.argtypes = [wintypes.HANDLE, ctypes.c_void_p, wintypes.DWORD, ctypes.POINTER(wintypes.DWORD), ctypes.c_void_p]
k32.WriteFile.argtypes = [wintypes.HANDLE, ctypes.c_void_p, wintypes.DWORD, ctypes.POINTER(wintypes.DWORD), ctypes.c_void_p]

frames = []

def serve():
    # Discord: READY after the handshake, an answer to every command
    h = k32.CreateNamedPipeW(PIPE, 3, 0, 1, 65536, 65536, 0, None)      # PIPE_ACCESS_DUPLEX, byte mode
    k32.ConnectNamedPipe(h, None)
    def read(n):
        buf = ctypes.create_string_buffer(n)
        got = wintypes.DWORD(0)
        data = b''
        while len(data) < n:
            if not k32.ReadFile(h, buf, n - len(data), ctypes.byref(got), None) or not got.value:
                raise IOError('closed')
            data += buf.raw[:got.value]
        return data
    def write(op, obj):
        body = json.dumps(obj).encode('utf-8')
        out = struct.pack('<II', op, len(body)) + body
        k32.WriteFile(h, out, len(out), ctypes.byref(wintypes.DWORD(0)), None)
    try:
        while True:
            op, size = struct.unpack('<II', read(8))
            body = json.loads(read(size).decode('utf-8'))
            frames.append((op, body))
            if op == 0:
                write(1, {'cmd': 'DISPATCH', 'evt': 'READY', 'data': {'v': 1}})
            else:
                write(1, {'cmd': body.get('cmd'), 'nonce': body.get('nonce'), 'data': {}})
    except IOError:
        pass

class Obj(object):
    pass

def main():
    src = io.open(RPY, encoding='utf-8').read()
    block = src.split('init 999 python:', 1)[1]
    code = textwrap.dedent(block.split('\n    if renpy.windows:', 1)[0])
    # the stand-ins: renpy.store, the script's labels, the game's current statement
    renpy = Obj()
    renpy.windows = True
    renpy.store = Obj()
    renpy.game = Obj()
    renpy.game.script = Obj()
    renpy.game.script.namemap = {}
    ctx = Obj()
    ctx.current = 'here'
    renpy.game.contexts = [ctx]
    config = Obj()
    config.periodic_callbacks = []
    config.gamedir = tempfile.mkdtemp()
    env = {'renpy': renpy, 'config': config}
    # the way the game's Ren'Py compiles python in .rpy (renpy/python.py new_compile_flags)
    import __future__
    flags = (__future__.absolute_import.compiler_flag | __future__.print_function.compiler_flag |
             __future__.unicode_literals.compiler_flag | __future__.with_statement.compiler_flag)
    exec(compile(code, RPY, 'exec', flags, True), env)
    threading.Thread(target=serve).start()
    time.sleep(0.2)
    tick = env['_genry_presence_install']({'client_id': '1234567890', 'lang': 'en', 'pipe': PIPE, 'gap': 0.3, 'tick': 0.1})

    def node(fn):
        n = Obj()
        n.filename = fn
        return n
    def step(until):
        t = time.time() + 8
        while time.time() < t:
            tick()
            time.sleep(0.2)
            acts = [b['args'].get('activity') for op, b in frames if op == 1]
            if acts and until(acts[-1]):
                return acts[-1]
        raise AssertionError('no such status: %r' % (frames[-3:],))

    ok = True
    # the original story: day 3, evening, Slavya's route
    renpy.store.main_menu = False
    renpy.store.time_of_day = 'sunset'
    renpy.store.backdrop = 'sl'
    renpy.game.script.namemap['here'] = node('game/scenario/day3.rpy')
    a = step(lambda a: a and a.get('details') == 'Day 3 · evening')
    ok &= a.get('state') == 'Route: Slavya' and a['timestamps']['start'] > 0
    print('original story:', a['details'], '|', a['state'])
    hand = [b for op, b in frames if op == 0]
    ok &= bool(hand) and hand[0].get('client_id') == '1234567890' and hand[0].get('v') == 1
    # a Workshop mod: its name from the game's «mods» without the text tags, its chapter from save_name
    renpy.store.mods = {'foo_start': u'{b}Foo Mod{/b}'}
    renpy.game.script.namemap['foo_start'] = node('../../workshop/content/331470/999/mods/foo/foo.rpy')
    renpy.game.script.namemap['here'] = node('../../workshop/content/331470/999/mods/foo/scene2.rpy')
    renpy.store.save_name = u'Chapter {i}two{/i}'
    a = step(lambda a: a and a.get('details') == u'Mod «Foo Mod»')
    ok &= a.get('state') == 'Chapter two'
    print('workshop mod:', a['details'], '|', a['state'])
    # the main menu
    renpy.store.main_menu = True
    a = step(lambda a: a and a.get('details') == 'In the main menu')
    print('main menu:', a['details'])
    frames_ok = all(b.get('cmd') == 'SET_ACTIVITY' and b['args'].get('pid') for op, b in frames if op == 1)
    ok &= frames_ok
    print('PRESENCE OK' if ok else 'PRESENCE FAILED', sys.version.split()[0])
    os._exit(0 if ok else 1)

main()
