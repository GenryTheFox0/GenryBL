"""Upload a folder to the Steam Workshop of Everlasting Summer (app 331470) as the Steam user who is logged in right now.

The same thing the game's own ES_Content_Uploader does: it talks to the running Steam client through the game's
steam_api64.dll (Steamworks ISteamUGC) - no password, no SteamCMD.

    python tools/workshop_upload.py --folder X --preview X/preview.jpg --title "…" --desc-file d.txt
                                    [--item ID] [--visibility public|friends|private|unlisted] [--note "…"]

Without --item a new item is created; its id is printed and written next to the folder (workshop_item_id.txt).
"""
import argparse
import ctypes
import os
import sys
import tempfile
import time

APP_ID = 331470
ES_LIB = r'E:/SteamLibrary/steamapps/common/Everlasting Summer/lib/windows-x86_64/steam_api64.dll'
VISIBILITY = {'public': 0, 'friends': 1, 'private': 2, 'unlisted': 3}
K_CREATE_ITEM = 3403           # CreateItemResult_t
K_SUBMIT_ITEM = 3404           # SubmitItemUpdateResult_t
STATUS = {0: 'invalid', 1: 'preparing config', 2: 'preparing content', 3: 'uploading content', 4: 'uploading preview',
          5: 'committing changes'}


class CreateItemResult(ctypes.Structure):
    _pack_ = 8
    _fields_ = [('result', ctypes.c_int32), ('file_id', ctypes.c_uint64), ('needs_legal', ctypes.c_bool)]


class SubmitItemResult(ctypes.Structure):
    _pack_ = 8
    _fields_ = [('result', ctypes.c_int32), ('needs_legal', ctypes.c_bool), ('file_id', ctypes.c_uint64)]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--folder', required=True)
    ap.add_argument('--preview', required=True)
    ap.add_argument('--title', required=True)
    ap.add_argument('--desc-file', required=True)
    ap.add_argument('--item', type=int, default=0)
    ap.add_argument('--visibility', default='public', choices=list(VISIBILITY))
    ap.add_argument('--note', default='Первая версия мода')
    ap.add_argument('--dll', default=ES_LIB)
    a = ap.parse_args()
    folder = os.path.abspath(a.folder)
    preview = os.path.abspath(a.preview)
    desc = open(a.desc_file, encoding='utf-8').read()
    if len(desc.encode('utf-8')) > 8000:
        sys.exit('description is longer than Steam takes (8000 bytes)')
    if os.path.getsize(preview) > 1024 * 1024:
        sys.exit('the preview must be under 1 MB')

    # Steamworks reads the app id from steam_appid.txt in the working directory
    work = tempfile.mkdtemp(prefix='genrybl_ws_')
    open(os.path.join(work, 'steam_appid.txt'), 'w').write(str(APP_ID))
    os.chdir(work)
    os.add_dll_directory(os.path.dirname(a.dll))
    api = ctypes.CDLL(a.dll)

    api.SteamAPI_InitSafe.restype = ctypes.c_bool
    api.SteamAPI_SteamUGC_v014.restype = ctypes.c_void_p
    api.SteamAPI_SteamUtils_v010.restype = ctypes.c_void_p
    api.SteamAPI_ISteamUGC_CreateItem.argtypes = [ctypes.c_void_p, ctypes.c_uint32, ctypes.c_int]
    api.SteamAPI_ISteamUGC_CreateItem.restype = ctypes.c_uint64
    api.SteamAPI_ISteamUGC_StartItemUpdate.argtypes = [ctypes.c_void_p, ctypes.c_uint32, ctypes.c_uint64]
    api.SteamAPI_ISteamUGC_StartItemUpdate.restype = ctypes.c_uint64
    for f in ('SetItemTitle', 'SetItemDescription', 'SetItemContent', 'SetItemPreview'):
        fn = getattr(api, 'SteamAPI_ISteamUGC_' + f)
        fn.argtypes = [ctypes.c_void_p, ctypes.c_uint64, ctypes.c_char_p]
        fn.restype = ctypes.c_bool
    api.SteamAPI_ISteamUGC_SetItemVisibility.argtypes = [ctypes.c_void_p, ctypes.c_uint64, ctypes.c_int]
    api.SteamAPI_ISteamUGC_SetItemVisibility.restype = ctypes.c_bool
    api.SteamAPI_ISteamUGC_SubmitItemUpdate.argtypes = [ctypes.c_void_p, ctypes.c_uint64, ctypes.c_char_p]
    api.SteamAPI_ISteamUGC_SubmitItemUpdate.restype = ctypes.c_uint64
    api.SteamAPI_ISteamUGC_GetItemUpdateProgress.argtypes = [ctypes.c_void_p, ctypes.c_uint64, ctypes.POINTER(ctypes.c_uint64),
                                                            ctypes.POINTER(ctypes.c_uint64)]
    api.SteamAPI_ISteamUGC_GetItemUpdateProgress.restype = ctypes.c_int
    api.SteamAPI_ISteamUtils_IsAPICallCompleted.argtypes = [ctypes.c_void_p, ctypes.c_uint64, ctypes.POINTER(ctypes.c_bool)]
    api.SteamAPI_ISteamUtils_IsAPICallCompleted.restype = ctypes.c_bool
    api.SteamAPI_ISteamUtils_GetAPICallResult.argtypes = [ctypes.c_void_p, ctypes.c_uint64, ctypes.c_void_p, ctypes.c_int, ctypes.c_int,
                                                         ctypes.POINTER(ctypes.c_bool)]
    api.SteamAPI_ISteamUtils_GetAPICallResult.restype = ctypes.c_bool

    if not api.SteamAPI_InitSafe():
        sys.exit('Steam is not running (or not logged in) - start Steam and try again')
    ugc = api.SteamAPI_SteamUGC_v014()
    utils = api.SteamAPI_SteamUtils_v010()

    def wait(call, struct, kind, progress=None):
        failed = ctypes.c_bool(False)
        last = ''
        while not api.SteamAPI_ISteamUtils_IsAPICallCompleted(utils, call, ctypes.byref(failed)):
            api.SteamAPI_RunCallbacks()
            if progress:
                line = progress()
                if line != last:
                    print('  ' + line, flush=True)
                    last = line
            time.sleep(0.5)
        out = struct()
        ok = api.SteamAPI_ISteamUtils_GetAPICallResult(utils, call, ctypes.byref(out), ctypes.sizeof(out), kind, ctypes.byref(failed))
        if not ok or failed.value:
            sys.exit('Steam did not answer the call (network? VPN?)')
        return out

    item = a.item
    if not item:
        print('creating a new Workshop item…', flush=True)
        r = wait(api.SteamAPI_ISteamUGC_CreateItem(ugc, APP_ID, 0), CreateItemResult, K_CREATE_ITEM)
        if r.result != 1:
            sys.exit(f'CreateItem failed: EResult {r.result}')
        item = r.file_id
        with open(os.path.join(os.path.dirname(folder), 'workshop_item_id.txt'), 'w') as f:
            f.write(str(item))
        print(f'item {item} created' + (' - Steam wants the Workshop legal agreement accepted' if r.needs_legal else ''), flush=True)

    h = api.SteamAPI_ISteamUGC_StartItemUpdate(ugc, APP_ID, item)
    checks = [api.SteamAPI_ISteamUGC_SetItemTitle(ugc, h, a.title.encode('utf-8')),
              api.SteamAPI_ISteamUGC_SetItemDescription(ugc, h, desc.encode('utf-8')),
              api.SteamAPI_ISteamUGC_SetItemContent(ugc, h, folder.encode('utf-8')),
              api.SteamAPI_ISteamUGC_SetItemPreview(ugc, h, preview.encode('utf-8')),
              api.SteamAPI_ISteamUGC_SetItemVisibility(ugc, h, VISIBILITY[a.visibility])]
    if not all(checks):
        sys.exit(f'Steam refused a field of the item: {checks}')

    def progress():
        done, total = ctypes.c_uint64(0), ctypes.c_uint64(0)
        st = api.SteamAPI_ISteamUGC_GetItemUpdateProgress(ugc, h, ctypes.byref(done), ctypes.byref(total))
        pct = f' {done.value * 100 // total.value}%' if total.value else ''
        return STATUS.get(st, str(st)) + pct

    print('uploading…', flush=True)
    r = wait(api.SteamAPI_ISteamUGC_SubmitItemUpdate(ugc, h, a.note.encode('utf-8')), SubmitItemResult, K_SUBMIT_ITEM, progress)
    if r.result != 1:
        sys.exit(f'SubmitItemUpdate failed: EResult {r.result}')
    print(f'DONE https://steamcommunity.com/sharedfiles/filedetails/?id={item}')
    if r.needs_legal:
        print('LEGAL: accept https://steamcommunity.com/sharedfiles/workshoplegalagreement to make it visible')
    api.SteamAPI_Shutdown()


if __name__ == '__main__':
    main()
