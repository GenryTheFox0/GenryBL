// GenryBL V1 - gb_workshop.exe: puts a mod into the Steam Workshop of Everlasting Summer, one click in GenryBL.
// A child process on purpose: while the Steam API is up, Steam counts it as «playing Everlasting Summer», so it lives
// only for the upload and is gone right after (GenryBL itself never starts the Steam API). It uses the game's own
// steam_api64.dll (the flat API, SDK ~1.50: SteamUGC_v014 / SteamUtils_v010), so nothing extra ships.
//
//   gb_workshop.exe --dll <steam_api64.dll> --content <dir> --title <text> --desc-file <utf8 file>
//                   [--preview <jpg/png>] [--item <id>] [--visibility 0|1|2|3] [--note-file <utf8 file>] [--lang russian]
//                   [--shot <jpg/png>]...   screenshots added to the item's gallery (each run adds them again)
//                   [--tags "Slavya,Romance,Variative"]   the Workshop's own tags (the item gets exactly these)
//   gb_workshop.exe --dll <steam_api64.dll> --check          resolve the functions, start nothing
//   gb_workshop.exe --dll <steam_api64.dll> --subscribe <id> | --unsubscribe <id>   the Center's «Подписаться»
//
// It tells GenryBL what happens, one ASCII line at a time (texts after the code are UTF-8):
//   ITEM <id>                  a new item was made (said at once: an upload that fails later keeps its item)
//   LEGAL                      Steam wants the Workshop agreement accepted before the item is seen
//   PROGRESS <status> <done> <total>   status: 1 config, 2 content, 3 uploading, 4 preview, 5 committing
//   DONE <id>
//   ERROR <code> <text>        code: args | dll | steam | appid | create | update | field | submit | timeout | result
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace {

#pragma pack(push, 8)
struct CreateItemResult { int32_t result; uint64_t fileId; bool needsLegal; };        // callback 3403
struct SubmitItemUpdateResult { int32_t result; bool needsLegal; uint64_t fileId; };  // callback 3404
#pragma pack(pop)
static_assert(sizeof(CreateItemResult) == 24, "CreateItemResult_t is 24 bytes");
static_assert(sizeof(SubmitItemUpdateResult) == 16, "SubmitItemUpdateResult_t is 16 bytes");
struct StringArray { const char** strings; int32_t count; };                            // SteamParamStringArray_t
struct SubscribeResult { int32_t result; uint64_t fileId; };                           // callbacks 1313 / 1315
static_assert(sizeof(SubscribeResult) == 16, "RemoteStorage(Un)SubscribePublishedFileResult_t is 16 bytes");

using FnInit = bool (*)();
using FnVoid = void (*)();
using FnIface = void* (*)();
using FnGetAppId = uint32_t (*)(void*);
using FnCallDone = bool (*)(void*, uint64_t, bool*);
using FnCallResult = bool (*)(void*, uint64_t, void*, int, int, bool*);
using FnCallFailure = int (*)(void*, uint64_t);
using FnCreateItem = uint64_t (*)(void*, uint32_t, int);
using FnStartUpdate = uint64_t (*)(void*, uint32_t, uint64_t);
using FnSetText = bool (*)(void*, uint64_t, const char*);
using FnSetInt = bool (*)(void*, uint64_t, int);
using FnSubmit = uint64_t (*)(void*, uint64_t, const char*);
using FnProgress = int (*)(void*, uint64_t, uint64_t*, uint64_t*);
// SDK 1.50 has (self, handle, tags); later ones add «allow admin tags» - an extra argument the old one never reads
using FnSetTags = bool (*)(void*, uint64_t, const StringArray*, bool);
using FnAddPreviewFile = bool (*)(void*, uint64_t, const char*, int);   // EItemPreviewType
using FnSubscribe = uint64_t (*)(void*, uint64_t);

const uint32_t kApp = 331470;

std::string utf8(const std::wstring& w)
{
    if (w.empty()) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), int(w.size()), nullptr, 0, nullptr, nullptr);
    std::string s(size_t(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), int(w.size()), &s[0], n, nullptr, nullptr);
    return s;
}

bool readFile(const std::wstring& path, std::string* out)
{
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    char buf[65536];
    DWORD got = 0;
    out->clear();
    while (ReadFile(h, buf, sizeof buf, &got, nullptr) && got) out->append(buf, got);
    CloseHandle(h);
    if (out->size() >= 3 && (unsigned char)(*out)[0] == 0xEF && (unsigned char)(*out)[1] == 0xBB && (unsigned char)(*out)[2] == 0xBF) out->erase(0, 3);
    return true;
}

int64_t fileSize(const std::wstring& path)
{
    WIN32_FILE_ATTRIBUTE_DATA a;
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &a)) return -1;
    return (int64_t(a.nFileSizeHigh) << 32) | a.nFileSizeLow;
}

void say(const char* fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    printf("\n");
    fflush(stdout);
}

struct Api {
    HMODULE dll = nullptr;
    FnInit init = nullptr;
    FnVoid runCallbacks = nullptr, shutdown = nullptr;
    FnIface ugcIface = nullptr, utilsIface = nullptr;
    FnGetAppId appId = nullptr;
    FnCallDone callDone = nullptr;
    FnCallResult callResult = nullptr;
    FnCallFailure callFailure = nullptr;
    FnCreateItem createItem = nullptr;
    FnStartUpdate startUpdate = nullptr;
    FnSetText setTitle = nullptr, setDesc = nullptr, setContent = nullptr, setPreview = nullptr, setLang = nullptr;
    FnSetInt setVisibility = nullptr;
    FnSubmit submit = nullptr;
    FnProgress progress = nullptr;
    FnSetTags setTags = nullptr;
    FnAddPreviewFile addPreviewFile = nullptr;
    FnSubscribe subscribe = nullptr, unsubscribe = nullptr;

    template <typename T> bool get(T& fn, const char* name, bool required = true)
    {
        fn = reinterpret_cast<T>(GetProcAddress(dll, name));
        if (!fn && required) say("ERROR dll the game's Steam library has no %s", name);
        return fn || !required;
    }
    bool load(const std::wstring& path)
    {
        dll = LoadLibraryW(path.c_str());
        if (!dll) { say("ERROR dll cannot load %s (%lu)", utf8(path).c_str(), GetLastError()); return false; }
        bool ok = get(init, "SteamAPI_InitSafe", false);
        if (!init) ok = get(init, "SteamAPI_Init");
        ok = get(runCallbacks, "SteamAPI_RunCallbacks") && ok;
        ok = get(shutdown, "SteamAPI_Shutdown") && ok;
        // the newest interface the library has (SDK ~1.50 in the game today, newer if the game updates its library)
        static const char* const ugcNames[] = {"SteamAPI_SteamUGC_v021", "SteamAPI_SteamUGC_v020", "SteamAPI_SteamUGC_v018", "SteamAPI_SteamUGC_v017",
                                               "SteamAPI_SteamUGC_v016", "SteamAPI_SteamUGC_v015", "SteamAPI_SteamUGC_v014"};
        for (const char* n : ugcNames) if (!ugcIface) ugcIface = reinterpret_cast<FnIface>(GetProcAddress(dll, n));
        if (!ugcIface) { say("ERROR dll the game's Steam library has no SteamUGC interface"); ok = false; }
        static const char* const utilsNames[] = {"SteamAPI_SteamUtils_v010", "SteamAPI_SteamUtils_v009"};
        for (const char* n : utilsNames) if (!utilsIface) utilsIface = reinterpret_cast<FnIface>(GetProcAddress(dll, n));
        if (!utilsIface) { say("ERROR dll the game's Steam library has no SteamUtils interface"); ok = false; }
        get(appId, "SteamAPI_ISteamUtils_GetAppID", false);
        ok = get(callDone, "SteamAPI_ISteamUtils_IsAPICallCompleted") && ok;
        ok = get(callResult, "SteamAPI_ISteamUtils_GetAPICallResult") && ok;
        get(callFailure, "SteamAPI_ISteamUtils_GetAPICallFailureReason", false);
        ok = get(createItem, "SteamAPI_ISteamUGC_CreateItem") && ok;
        ok = get(startUpdate, "SteamAPI_ISteamUGC_StartItemUpdate") && ok;
        ok = get(setTitle, "SteamAPI_ISteamUGC_SetItemTitle") && ok;
        ok = get(setDesc, "SteamAPI_ISteamUGC_SetItemDescription") && ok;
        ok = get(setContent, "SteamAPI_ISteamUGC_SetItemContent") && ok;
        ok = get(setPreview, "SteamAPI_ISteamUGC_SetItemPreview") && ok;
        get(setLang, "SteamAPI_ISteamUGC_SetItemUpdateLanguage", false);
        get(setTags, "SteamAPI_ISteamUGC_SetItemTags", false);
        get(addPreviewFile, "SteamAPI_ISteamUGC_AddItemPreviewFile", false);
        get(subscribe, "SteamAPI_ISteamUGC_SubscribeItem", false);
        get(unsubscribe, "SteamAPI_ISteamUGC_UnsubscribeItem", false);
        ok = get(setVisibility, "SteamAPI_ISteamUGC_SetItemVisibility") && ok;
        ok = get(submit, "SteamAPI_ISteamUGC_SubmitItemUpdate") && ok;
        ok = get(progress, "SteamAPI_ISteamUGC_GetItemUpdateProgress") && ok;
        return ok;
    }
};

// a Steam call: wait for its answer, saying the upload's progress; give up when nothing moves for `quietMs`
bool wait(Api& a, void* utils, void* ugc, uint64_t call, void* out, int size, int callback, DWORD quietMs, uint64_t update = 0)
{
    bool failed = false;
    DWORD lastMove = GetTickCount();
    int lastStatus = -1;
    uint64_t lastDone = 0;
    for (;;) {
        a.runCallbacks();
        if (a.callDone(utils, call, &failed)) break;
        if (update) {
            uint64_t done = 0, total = 0;
            const int st = a.progress(ugc, update, &done, &total);
            if (st != lastStatus || done != lastDone) {
                say("PROGRESS %d %llu %llu", st, (unsigned long long)done, (unsigned long long)total);
                lastStatus = st;
                lastDone = done;
                lastMove = GetTickCount();
            }
        }
        if (GetTickCount() - lastMove > quietMs) {
            say("ERROR timeout Steam does not answer (the network? a VPN?)");
            return false;
        }
        Sleep(200);
    }
    const bool ok = a.callResult(utils, call, out, size, callback, &failed);
    if (!ok || failed) {
        say("ERROR steam Steam did not answer the call (reason %d)", a.callFailure ? a.callFailure(utils, call) : -2);
        return false;
    }
    return true;
}

// the folder steam_appid.txt lives in while the upload runs, and goes away with it
struct AppIdDir {
    std::wstring dir, back;
    bool make()
    {
        wchar_t tmp[MAX_PATH + 1];
        if (!GetTempPathW(MAX_PATH, tmp)) return false;
        dir = std::wstring(tmp) + L"genrybl_ws_" + std::to_wstring(GetCurrentProcessId());
        CreateDirectoryW(dir.c_str(), nullptr);
        HANDLE h = CreateFileW((dir + L"\\steam_appid.txt").c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
        if (h == INVALID_HANDLE_VALUE) return false;
        DWORD put = 0;
        WriteFile(h, "331470", 6, &put, nullptr);
        CloseHandle(h);
        wchar_t cur[MAX_PATH + 1];
        GetCurrentDirectoryW(MAX_PATH, cur);
        back = cur;
        SetCurrentDirectoryW(dir.c_str());
        SetEnvironmentVariableW(L"SteamAppId", L"331470");
        SetEnvironmentVariableW(L"SteamGameId", L"331470");
        return true;
    }
    ~AppIdDir()
    {
        if (dir.empty()) return;
        if (!back.empty()) SetCurrentDirectoryW(back.c_str());
        DeleteFileW((dir + L"\\steam_appid.txt").c_str());
        RemoveDirectoryW(dir.c_str());
    }
};

} // namespace

int wmain(int argc, wchar_t** argv)
{
    std::wstring dllPath, content, title, descFile, preview, noteFile, lang = L"russian", tagList;
    std::vector<std::wstring> shots;
    bool withTags = false;
    uint64_t item = 0;
    int visibility = -1;
    bool check = false;
    uint64_t subId = 0;
    bool subOn = true;
    for (int i = 1; i < argc; ++i) {
        const std::wstring k = argv[i];
        auto next = [&]() -> std::wstring { return i + 1 < argc ? argv[++i] : std::wstring(); };
        if (k == L"--dll") dllPath = next();
        else if (k == L"--content") content = next();
        else if (k == L"--title") title = next();
        else if (k == L"--desc-file") descFile = next();
        else if (k == L"--preview") preview = next();
        else if (k == L"--shot") shots.push_back(next());
        else if (k == L"--note-file") noteFile = next();
        else if (k == L"--lang") lang = next();
        else if (k == L"--tags") { tagList = next(); withTags = true; }
        else if (k == L"--item") item = _wcstoui64(next().c_str(), nullptr, 10);
        else if (k == L"--visibility") visibility = _wtoi(next().c_str());
        else if (k == L"--check") check = true;
        else if (k == L"--subscribe") subId = _wcstoui64(next().c_str(), nullptr, 10);
        else if (k == L"--unsubscribe") { subId = _wcstoui64(next().c_str(), nullptr, 10); subOn = false; }
    }
    if (dllPath.empty()) { say("ERROR args --dll is needed"); return 2; }
    Api a;
    if (!a.load(dllPath)) return 1;
    if (check) { say("CHECK OK"); return 0; }

    if (subId) {                                     // the Center: subscribe / unsubscribe, Steam downloads it itself
        if (!(subOn ? a.subscribe : a.unsubscribe)) { say("ERROR dll the game's Steam library cannot subscribe"); return 1; }
        AppIdDir appId;
        if (!appId.make()) { say("ERROR steam cannot write steam_appid.txt"); return 1; }
        if (!a.init()) { say("ERROR steam Steam is not running, or not logged in, or this account has no Everlasting Summer"); return 1; }
        int code = 1;
        void* ugc = a.ugcIface();
        void* utils = a.utilsIface();
        SubscribeResult r{};
        if (ugc && utils && wait(a, utils, ugc, (subOn ? a.subscribe : a.unsubscribe)(ugc, subId), &r, sizeof r, subOn ? 1313 : 1315, 30000)) {
            if (r.result == 1) { say("DONE %llu", (unsigned long long)subId); code = 0; }
            else say("ERROR result EResult %d", r.result);
        }
        a.shutdown();
        return code;
    }

    // everything is checked BEFORE Steam starts: a refusal from Steam after an hour of upload is the worst kind
    std::string desc, note;
    if (content.empty() || title.empty() || descFile.empty()) { say("ERROR args --content, --title and --desc-file are needed"); return 2; }
    const DWORD attr = GetFileAttributesW(content.c_str());
    if (attr == INVALID_FILE_ATTRIBUTES || !(attr & FILE_ATTRIBUTE_DIRECTORY)) { say("ERROR args no content folder %s", utf8(content).c_str()); return 2; }
    if (!readFile(descFile, &desc)) { say("ERROR args cannot read the description"); return 2; }
    if (!noteFile.empty()) readFile(noteFile, &note);
    const std::string title8 = utf8(title);
    if (title8.size() > 128 * 4 || title.size() > 128) { say("ERROR args the title is longer than 128 characters"); return 2; }
    if (desc.size() > 8000) { say("ERROR args the description is longer than 8000 bytes"); return 2; }
    if (note.size() > 8000) note.resize(8000);
    if (!preview.empty()) {
        const int64_t sz = fileSize(preview);
        if (sz < 0) { say("ERROR args no preview picture %s", utf8(preview).c_str()); return 2; }
        if (sz > 1024 * 1024) { say("ERROR args the preview picture is over 1 MB"); return 2; }
    }
    for (const std::wstring& s : shots) {
        const int64_t sz = fileSize(s);
        if (sz < 0) { say("ERROR args no screenshot %s", utf8(s).c_str()); return 2; }
        if (sz > 1024 * 1024) { say("ERROR args the screenshot %s is over 1 MB", utf8(s).c_str()); return 2; }
    }

    AppIdDir appId;
    if (!appId.make()) { say("ERROR steam cannot write steam_appid.txt"); return 1; }
    if (!a.init()) { say("ERROR steam Steam is not running, or not logged in, or this account has no Everlasting Summer"); return 1; }
    int code = 1;
    do {
        void* ugc = a.ugcIface();
        void* utils = a.utilsIface();
        if (!ugc || !utils) { say("ERROR steam Steam gave no Workshop interface"); break; }
        if (a.appId && a.appId(utils) != kApp) { say("ERROR appid Steam started for app %u, not Everlasting Summer", a.appId(utils)); break; }
        bool legal = false;
        if (!item) {
            CreateItemResult r{};
            if (!wait(a, utils, ugc, a.createItem(ugc, kApp, 0), &r, sizeof r, 3403, 60000)) break;
            if (r.result != 1) { say("ERROR create EResult %d", r.result); break; }
            item = r.fileId;
            say("ITEM %llu", (unsigned long long)item);
            if (r.needsLegal) { legal = true; say("LEGAL"); }
        }
        const uint64_t h = a.startUpdate(ugc, kApp, item);
        if (h == ~0ull) { say("ERROR update Steam refused to update item %llu (is it yours?)", (unsigned long long)item); break; }
        if (a.setLang && !lang.empty()) a.setLang(ugc, h, utf8(lang).c_str());
        if (!a.setTitle(ugc, h, title8.c_str())) { say("ERROR field title"); break; }
        if (!a.setDesc(ugc, h, desc.c_str())) { say("ERROR field description"); break; }
        if (!preview.empty() && !a.setPreview(ugc, h, utf8(preview).c_str())) { say("ERROR field preview"); break; }
        if (visibility >= 0 && !a.setVisibility(ugc, h, visibility)) { say("ERROR field visibility"); break; }
        if (!shots.empty() && !a.addPreviewFile) { say("ERROR dll the game's Steam library cannot add screenshots"); break; }
        bool shotsOk = true;
        for (const std::wstring& s : shots)                 // k_EItemPreviewType_Image = 0
            if (!a.addPreviewFile(ugc, h, utf8(s).c_str(), 0)) { say("ERROR field screenshot %s", utf8(s).c_str()); shotsOk = false; break; }
        if (!shotsOk) break;
        if (withTags && a.setTags) {
            std::vector<std::string> tags;
            std::string all = utf8(tagList), cur;
            for (char c : all + ",") {
                if (c == ',') { if (!cur.empty()) tags.push_back(cur); cur.clear(); }
                else cur += c;
            }
            std::vector<const char*> ptrs;
            for (const std::string& t : tags) ptrs.push_back(t.c_str());
            const StringArray arr{ptrs.empty() ? nullptr : ptrs.data(), int32_t(ptrs.size())};
            if (!a.setTags(ugc, h, &arr, false)) { say("ERROR field tags"); break; }
        }
        if (!a.setContent(ugc, h, utf8(content).c_str())) { say("ERROR field content"); break; }
        SubmitItemUpdateResult s{};
        if (!wait(a, utils, ugc, a.submit(ugc, h, note.c_str()), &s, sizeof s, 3404, 180000, h)) break;
        if (s.result != 1) { say("ERROR result EResult %d", s.result); break; }
        if (s.needsLegal && !legal) say("LEGAL");
        say("DONE %llu", (unsigned long long)item);
        code = 0;
    } while (false);
    a.shutdown();
    return code;
}
