"""GenryBL V1 - the Steam Workshop item (the showcase mod + the installer inside it).

    python tools/make_release.py --workshop     (dist/workshop_setup/GenryBL_Setup.exe - no copy of the 18+ patch)
    python tools/build_workshop_item.py
    python tools/workshop_upload.py --item <id> --note "..."

1. compiles projects/genrybl_workshop into the game (gb_cli lint: the game's own Ren'Py makes the .rpyc - the
   Workshop wants one next to every .rpy) and proves the game's lint is clean
2. dist/workshop/GenryBL_Workshop/mods/genrybl_workshop = that mod + GenryBL_Setup.exe + GenryBL_version.txt
   (the «Обновить» button of an installed GenryBL reads the version from there: Steam keeps the item fresh)
3. removes the mod from game/mods again - a local copy next to the subscribed item = labels defined twice
"""
import os
import re
import shutil
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
QT = os.environ.get('GENRYBL_QT', r'E:\Qt\6.8.3\msvc2022_64')
ITEM = os.path.join(ROOT, 'dist', 'workshop', 'GenryBL_Workshop')
MOD = 'genrybl_workshop'


def version():
    src = open(os.path.join(ROOT, 'src', 'app', 'Engine.h'), encoding='utf-8').read()
    return re.search(r'QString version\(\) const \{ return QStringLiteral\("([^"]+)"\); \}', src).group(1)


def es_root():
    for line in open(os.path.join(ROOT, 'work', 'settings.ini'), encoding='utf-8', errors='replace'):
        if line.startswith('esRoot='):
            return line.split('=', 1)[1].strip()
    sys.exit('no esRoot in work/settings.ini')


def main():
    setup = os.path.join(ROOT, 'dist', 'workshop_setup', 'GenryBL_Setup.exe')
    if not os.path.isfile(setup):
        sys.exit('no ' + setup + ' - run tools/make_release.py --workshop first')
    game_mod = os.path.join(es_root(), 'game', 'mods', MOD)
    env = dict(os.environ)
    env['PATH'] = os.path.join(QT, 'bin') + os.pathsep + env['PATH']
    cli = os.path.join(ROOT, 'build', 'gb_cli.exe')
    out = subprocess.run([cli, 'lint', os.path.join(ROOT, 'projects', MOD, 'story.txt')], env=env,
                         capture_output=True, text=True, encoding='utf-8', errors='replace')
    print(out.stdout.strip().splitlines()[-1] if out.stdout.strip() else out.stderr)
    try:
        if out.returncode != 0 or 'LINT CLEAN' not in out.stdout:
            sys.exit('the game lint is not clean - no Workshop item')
        dst = os.path.join(ITEM, 'mods', MOD)
        if os.path.isdir(dst):
            shutil.rmtree(dst)
        shutil.copytree(game_mod, dst)
        shutil.copy2(setup, os.path.join(dst, 'GenryBL_Setup.exe'))
        with open(os.path.join(dst, 'GenryBL_version.txt'), 'w', encoding='utf-8', newline='\n') as f:
            f.write(version() + '\n')
        rpy = [n for n in os.listdir(dst) if n.endswith('.rpy')]
        missing = [n for n in rpy if not os.path.isfile(os.path.join(dst, n + 'c'))]
        if missing:
            sys.exit('no .rpyc for ' + ', '.join(missing))
        if not os.path.isfile(os.path.join(ITEM, 'preview.jpg')):
            sys.exit('no preview.jpg in ' + ITEM)
        size = sum(os.path.getsize(os.path.join(d, n)) for d, _, fs in os.walk(ITEM) for n in fs)
        print('== ' + os.path.relpath(ITEM, ROOT).replace(os.sep, '/') + f'  {version()}  {size / 1048576:.1f} MB')
    finally:
        if os.path.isdir(game_mod):
            shutil.rmtree(game_mod)          # never leave a local copy next to the subscribed item


if __name__ == '__main__':
    main()
