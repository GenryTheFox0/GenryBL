"""data/patch: the 18+ patch of Everlasting Summer that ships inside GenryBL («Deleted hentai scenes», Steam Workshop
1118110148, by Лена / Lena_sova), so its CGs, old cards and the heroines' bodies work without a subscription.

Reads the patch from the Steam Workshop folder and writes data/patch/i8_data.rpa (RPA-3.0, key 0) with everything of
it except Ульяна (her bodies, the «d5_dv_us_wash» frames) and Электроник - those stay the Steam build's own.
make_release.py runs it; the dev app reads the same folder.
"""
import io
import os
import pickle
import sys
import zlib

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PATCH_ID = '1118110148'
def steam_dir():
    """GENRYBL_STEAM, else where Steam says it lives (HKCU\\Software\\Valve\\Steam SteamPath)"""
    if os.environ.get('GENRYBL_STEAM'):
        return os.environ['GENRYBL_STEAM']
    try:
        import winreg
        with winreg.OpenKey(winreg.HKEY_CURRENT_USER, r'Software\Valve\Steam') as k:
            return winreg.QueryValueEx(k, 'SteamPath')[0]
    except OSError:
        return r'C:/Program Files (x86)/Steam'


STEAM = steam_dir()
PAGE = 'https://steamcommunity.com/sharedfiles/filedetails/?id=' + PATCH_ID
AUTHOR = 'https://steamcommunity.com/id/Lena_sova'


def skipped(name):
    n = name.replace('\\', '/').lower()
    return '/us/' in n or '_us_' in n or '/el/' in n


def steam_libraries():
    """every Steam library of this PC (steamapps/libraryfolders.vdf), the main one first"""
    libs = [STEAM]
    vdf = os.path.join(STEAM, 'steamapps', 'libraryfolders.vdf')
    if os.path.isfile(vdf):
        for line in io.open(vdf, encoding='utf-8', errors='replace'):
            parts = line.strip().split('"')
            if len(parts) >= 4 and parts[1] == 'path':
                libs.append(parts[3].replace('\\\\', '/'))
    return libs


def find_patch():
    for lib in steam_libraries():
        p = os.path.join(lib, 'steamapps', 'workshop', 'content', '331470', PATCH_ID, 'i8_data.rpa')
        if os.path.isfile(p):
            return p
    return None


def main():
    src = find_patch()
    out_dir = os.path.join(ROOT, 'data', 'patch')
    out = os.path.join(out_dir, 'i8_data.rpa')
    if not src:
        if os.path.isfile(out):
            print('   18+ patch: not in the Workshop here, keeping', out)
            return 0
        sys.exit('the 18+ patch (Workshop ' + PATCH_ID + ') is not subscribed on this PC and data/patch is empty')
    f = open(src, 'rb')
    head = f.readline().split()
    offset, key = int(head[1], 16), int(head[2], 16) if len(head) > 2 else 0
    f.seek(offset)
    index = pickle.loads(zlib.decompress(f.read()), encoding='bytes')
    os.makedirs(out_dir, exist_ok=True)
    kept, dropped = {}, []
    tmp = out + '.tmp'
    with open(tmp, 'wb') as w:
        w.write(b'RPA-3.0 0000000000000000 00000000\n')
        for raw_name, entries in sorted(index.items()):
            name = raw_name.decode('utf-8') if isinstance(raw_name, bytes) else raw_name
            if skipped(name):
                dropped.append(name)
                continue
            e = entries[0]
            off, length = e[0] ^ key, e[1] ^ key
            prefix = e[2] if len(e) > 2 else b''
            if isinstance(prefix, str):
                prefix = prefix.encode('latin-1')
            f.seek(off)
            data = prefix + f.read(length - len(prefix))
            kept[name] = [(w.tell(), len(data))]
            w.write(data)
        index_at = w.tell()
        w.write(zlib.compress(pickle.dumps(kept, protocol=2)))
        w.seek(0)
        w.write(('RPA-3.0 %016x %08x\n' % (index_at, 0)).encode('ascii'))
    os.replace(tmp, out)
    io.open(os.path.join(out_dir, 'ПАТЧ.txt'), 'w', encoding='utf-8', newline='\r\n').write(
        '18+ патч «Бесконечного лета» — «Deleted hentai scenes»\n'
        'Автор: Лена — ' + AUTHOR + '\n'
        'Страница патча в Мастерской Steam: ' + PAGE + '\n\n'
        'Встроен в GenryBL, чтобы его CG, старые карточки дней и тела героинь работали без подписки.\n'
        'Все права на него — у автора. Нравится — поставь патчу лайк в Мастерской.\n')
    print(f'   18+ patch: {len(kept)} files into data/patch ({os.path.getsize(out) / 1048576:.1f} MB), '
          f'left out {len(dropped)} (Ульяна, Электроник)')
    return 0


if __name__ == '__main__':
    sys.exit(main())
