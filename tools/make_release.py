"""GenryBL V1 - the release for people (GitHub). A developer build (without GB_RELEASE) stays where you build it.

    python tools/make_release.py

1. builds the public edition (-DGB_RELEASE=ON) into build_release/ and runs its selftest
2. stages dist/stage/GenryBL: app/ (GenryBL.exe + the Qt runtime + the Visual C++ runtime, app-local),
   data/ (+ ffmpeg), ПРОЧТИ.txt - and proves with dumpbin that every DLL any exe/dll of it needs is either
   inside or a part of Windows itself (a clean Windows has no VC++ Redistributable)
3. dist/github/: GenryBL_Setup.exe (the Win32 bootstrap + the zipped program + footer),
   GenryBL_V1_portable.zip, README.md - nothing else (no .cmd, no sources, no dev junk)
"""
import hashlib
import os
import shutil
import struct
import subprocess
import sys
import zipfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
QT = os.environ.get('GENRYBL_QT', r'E:\Qt\6.8.3\msvc2022_64')
BUILD = os.path.join(ROOT, 'build_release')
DIST = os.path.join(ROOT, 'dist')
STAGE = os.path.join(DIST, 'stage', 'GenryBL')
# --workshop: the installer that rides in the Steam Workshop item (tools/workshop): no copy of the 18+ patch inside -
# in the Workshop the patch is its author's own item, GenryBL takes it from the subscription
WORKSHOP = '--workshop' in sys.argv
OUT = os.path.join(DIST, 'workshop_setup' if WORKSHOP else 'github')
MAGIC = b'GENRYBL1'


def vs_shell(cmd):
    """run a command inside the Visual Studio x64 environment (cmake/ninja/cl)"""
    vswhere = os.path.expandvars(r'%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe')
    vs = subprocess.check_output([vswhere, '-latest', '-products', '*', '-requires',
                                  'Microsoft.VisualStudio.Component.VC.Tools.x86.x64', '-property', 'installationPath'], text=True).strip()
    vcvars = os.path.join(vs, r'VC\Auxiliary\Build\vcvars64.bat')
    env = dict(os.environ)
    env['PATH'] = os.path.join(sys.prefix, 'Scripts') + os.pathsep + env['PATH']      # cmake/ninja from pip
    subprocess.check_call(f'call "{vcvars}" >nul && {cmd}', shell=True, env=env)


def vs_env():
    """the environment vcvars64 makes (VCToolsRedistDir, dumpbin on PATH)"""
    vswhere = os.path.expandvars(r'%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe')
    vs = subprocess.check_output([vswhere, '-latest', '-products', '*', '-requires',
                                  'Microsoft.VisualStudio.Component.VC.Tools.x86.x64', '-property', 'installationPath'], text=True).strip()
    vcvars = os.path.join(vs, r'VC\Auxiliary\Build\vcvars64.bat')
    out = subprocess.check_output(f'call "{vcvars}" >nul && set', shell=True, text=True, errors='replace')
    return dict(l.split('=', 1) for l in out.splitlines() if '=' in l)


# DLLs a clean Windows 10/11 has by itself (everything else must be inside app/)
def windows_has(name):
    n = name.lower()
    if n.startswith('api-ms-win-') or n.startswith('ext-ms-'):
        return True
    # the Visual C++ runtime is NOT part of Windows: it has to ship with the program
    if n.startswith(('msvcp1', 'vcruntime1', 'concrt1', 'vccorlib1', 'mfc1', 'vcomp1')):
        return False
    return os.path.exists(os.path.join(os.environ.get('SystemRoot', r'C:\Windows'), 'System32', name))


def check_dependencies(app, env):
    """every exe/dll of the program: what it imports is inside app/ or a part of Windows"""
    inside = {f.lower() for f in os.listdir(app)}
    dumpbin = shutil.which('dumpbin', path=env.get('PATH') or env.get('Path') or '')
    if not dumpbin:
        sys.exit('dumpbin not found in the Visual Studio environment')
    missing = {}
    pes = []
    for base, _, files in os.walk(app):
        pes += [os.path.join(base, f) for f in files if f.lower().endswith(('.exe', '.dll'))]
    for pe in pes:
        out = subprocess.run([dumpbin, '/nologo', '/dependents', pe], env=env, capture_output=True, text=True, errors='replace').stdout
        deps, on = [], False
        for line in out.splitlines():
            t = line.strip()
            if t.startswith('Image has the following dependencies'):
                on = True
                continue
            if on and t.lower().endswith('.dll'):
                deps.append(t)
            elif on and t.startswith('Summary'):
                break
        for d in deps:
            if d.lower() not in inside and not windows_has(d):
                missing.setdefault(d, []).append(os.path.relpath(pe, app))
    return len(pes), missing


def copy_tree(src, dst):
    for base, _, files in os.walk(src):
        for f in files:
            s = os.path.join(base, f)
            d = os.path.join(dst, os.path.relpath(s, src))
            os.makedirs(os.path.dirname(d), exist_ok=True)
            shutil.copy2(s, d)


def sha(path):
    h = hashlib.sha256()
    with open(path, 'rb') as f:
        for chunk in iter(lambda: f.read(1 << 20), b''):
            h.update(chunk)
    return h.hexdigest()


def main():
    # the 18+ patch that ships inside (data/patch): fresh from the Workshop, before the selftest reads it
    subprocess.check_call([sys.executable, os.path.join(ROOT, 'tools', 'bundle_patch.py')])
    print('== build the public edition')
    vs_shell(f'cmake -S "{ROOT}" -B "{BUILD}" -G Ninja -DCMAKE_BUILD_TYPE=Release -DGB_RELEASE=ON -DCMAKE_PREFIX_PATH="{QT}" >nul')
    vs_shell(f'cmake --build "{BUILD}" --target GenryBL GenryBL_Setup gb_selftest gb_cli')
    env = dict(os.environ)
    env['PATH'] = os.path.join(QT, 'bin') + os.pathsep + env['PATH']
    r = subprocess.run([os.path.join(BUILD, 'gb_selftest.exe')], env=env, capture_output=True, text=True, encoding='utf-8', errors='replace')
    last = [l for l in r.stdout.splitlines() if l.startswith('RESULT') or 'FAIL' in l]
    print('   selftest:', ' | '.join(last))
    if r.returncode != 0:
        sys.exit('selftest failed - no release')
    # «замок»: every story GenryBL must build, installed as mods and checked by the game itself - each screen built,
    # each python name, the game's lint (gb_cli gate, ~4 min). A mod that would crash a player stops the release.
    if '--skip-gate' in sys.argv:
        print('   gate: SKIPPED on request (--skip-gate) - this release was NOT checked by the game')
    else:
        print('== gate: the game checks every story GenryBL must build (a few minutes)')
        g = subprocess.run([os.path.join(BUILD, 'gb_cli.exe'), 'gate'], env=env, capture_output=True, text=True, encoding='utf-8', errors='replace')
        tail = [l for l in g.stdout.splitlines() if l.startswith(('the game checked', '  FAIL', 'GATE'))]
        print('\n'.join('   ' + l for l in tail[:40]))
        if g.returncode != 0:
            sys.exit('gate failed - no release')

    print('== stage dist/stage/GenryBL')
    shutil.rmtree(os.path.join(DIST, 'stage'), ignore_errors=True)
    app = os.path.join(STAGE, 'app')
    os.makedirs(app)
    shutil.copy2(os.path.join(BUILD, 'GenryBL.exe'), app)
    # the symbols ride along: a crash report (work/crash) then names GenryBL's own functions and lines, not bare addresses
    if os.path.isfile(os.path.join(BUILD, 'GenryBL.pdb')):
        shutil.copy2(os.path.join(BUILD, 'GenryBL.pdb'), app)
    subprocess.check_call([os.path.join(QT, 'bin', 'windeployqt.exe'), '--release', '--qmldir', os.path.join(ROOT, 'qml'), '--no-translations',
                           '--no-system-d3d-compiler', '--no-opengl-sw', os.path.join(app, 'GenryBL.exe')],
                          stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, env=env)
    # the Visual C++ runtime next to GenryBL.exe (Microsoft's app-local deployment of the redistributable DLLs)
    venv = vs_env()
    redist = venv.get('VCToolsRedistDir', '')
    crt = []
    for base, dirs, files in os.walk(os.path.join(redist, 'x64')):
        if os.path.basename(base).lower().startswith('microsoft.vc') and base.lower().endswith('.crt'):
            crt += [os.path.join(base, f) for f in files if f.lower().endswith('.dll')]
    if not crt:
        sys.exit('the Visual C++ runtime DLLs were not found in ' + redist)
    for f in crt:
        shutil.copy2(f, app)
    print('   VC++ runtime:', ', '.join(sorted(os.path.basename(f) for f in crt)))
    n, missing = check_dependencies(app, venv)
    if missing:
        for d, who in sorted(missing.items()):
            print(f'   MISSING {d}  <- {", ".join(who[:4])}')
        sys.exit('the program would not start on a clean Windows - no release')
    print(f'   dependencies: {n} exe/dll checked, all inside or a part of Windows')
    data = os.path.join(STAGE, 'data')
    # «Удалённые» / «поправить лицо» of the wardrobe ship with it (collected from every copy on this PC)
    subprocess.check_call([sys.executable, os.path.join(ROOT, 'tools', 'harvest_wardrobe_lists.py')])
    for f in ('es_catalog.json', 'forms.json', 'genrybl.ico', 'starter_story.txt', 'starter_blank.txt', 'wardrobe_hidden.txt', 'wardrobe_faces.txt'):
        os.makedirs(data, exist_ok=True)
        shutil.copy2(os.path.join(ROOT, 'data', f), data)
    copy_tree(os.path.join(ROOT, 'data', 'mod_assets'), os.path.join(data, 'mod_assets'))
    copy_tree(os.path.join(ROOT, 'data', 'es_doc'), os.path.join(data, 'es_doc'))       # es-doc: Russian names + community sounds (GPL-3.0)
    copy_tree(os.path.join(ROOT, 'data', 'menu'), os.path.join(data, 'menu'))           # the living menu: a picture + a wind mask per time of day
    copy_tree(os.path.join(ROOT, 'data', 'i18n'), os.path.join(data, 'i18n'))           # 20 languages (ru = the source, no file)
    copy_tree(os.path.join(ROOT, 'data', 'flags'), os.path.join(data, 'flags'))         # the language chooser's flags
    copy_tree(os.path.join(ROOT, 'data', 'gate'), os.path.join(data, 'gate'))           # «замок»: the game checks a mod's screens and names
    langs = [f for f in os.listdir(os.path.join(data, 'i18n')) if f.endswith('.json') and not f.startswith('_')]
    if len(langs) < 19:
        sys.exit(f'only {len(langs)} translations in data/i18n - no release')
    os.remove(os.path.join(data, 'i18n', '_source.json'))                              # the extractor's list, not for people
    if not WORKSHOP:
        copy_tree(os.path.join(ROOT, 'data', 'patch'), os.path.join(data, 'patch'))     # the 18+ patch inside (bundle_patch.py)
    copy_tree(os.path.join(ROOT, 'third_party', 'ffmpeg'), os.path.join(data, 'tools'))
    shutil.copy2(os.path.join(ROOT, 'release_files', 'ПРОЧТИ.txt'), STAGE)
    if WORKSHOP:
        readme = os.path.join(STAGE, 'ПРОЧТИ.txt')
        text = open(readme, encoding='utf-8').read()
        old = '— он встроен,\n  подписываться не надо.'
        assert old in text, 'ПРОЧТИ.txt: the sentence about the built-in patch moved'
        text = text.replace(old, '— подпишись на него\n  в Мастерской, GenryBL подхватит его сам.')
        text = text.replace('в форматы игры), 18+ патч и всё остальное', 'в форматы игры) и всё остальное')
        open(readme, 'w', encoding='utf-8', newline='\n').write(text)

    print('== pack')
    os.makedirs(OUT, exist_ok=True)
    for f in os.listdir(OUT):
        os.remove(os.path.join(OUT, f))
    payload = os.path.join(DIST, 'payload.zip')
    with zipfile.ZipFile(payload, 'w', zipfile.ZIP_DEFLATED, compresslevel=6) as z:
        for base, _, files in os.walk(STAGE):
            for f in sorted(files):
                p = os.path.join(base, f)
                z.write(p, os.path.join('GenryBL', os.path.relpath(p, STAGE)))
    # the bootstrap itself: static C runtime, only Windows DLLs
    boot = os.path.join(BUILD, 'GenryBL_Setup.exe')
    tmp = os.path.join(DIST, 'bootcheck')
    shutil.rmtree(tmp, ignore_errors=True)
    os.makedirs(tmp)
    shutil.copy2(boot, tmp)
    _, bmissing = check_dependencies(tmp, venv)
    shutil.rmtree(tmp, ignore_errors=True)
    if bmissing:
        sys.exit('GenryBL_Setup.exe needs ' + ', '.join(bmissing) + ' - no release')
    print('   GenryBL_Setup.exe: only Windows DLLs')
    setup = os.path.join(OUT, 'GenryBL_Setup.exe')
    with open(setup, 'wb') as out:
        with open(os.path.join(BUILD, 'GenryBL_Setup.exe'), 'rb') as f:
            out.write(f.read())
        size = os.path.getsize(payload)
        with open(payload, 'rb') as f:
            shutil.copyfileobj(f, out)
        out.write(struct.pack('<Q', size) + MAGIC)
    if WORKSHOP:
        os.remove(payload)
    else:
        shutil.move(payload, os.path.join(OUT, 'GenryBL_V1_portable.zip'))
        shutil.copy2(os.path.join(ROOT, 'release_files', 'README.md'), OUT)
    print('== ' + os.path.relpath(OUT, ROOT).replace(os.sep, '/'))
    for f in sorted(os.listdir(OUT)):
        p = os.path.join(OUT, f)
        print(f'   {f:28} {os.path.getsize(p) / 1048576:8.1f} MB  sha256 {sha(p)[:16]}')


if __name__ == '__main__':
    main()
