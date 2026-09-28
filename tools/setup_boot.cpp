// GenryBL_Setup.exe - the one file people download. A plain Win32 bootstrap (no Qt): the program
// itself is zipped into the end of this exe (make_release.py appends it); the bootstrap unpacks it
// to a temp folder with Windows' own tar (Windows 10 1803+), then starts the animated installer -
// GenryBL.exe --install from there - and cleans the temp folder when it closes.
// Footer (last 16 bytes): payload size (uint64 LE) + "GENRYBL1".
#ifndef UNICODE
#define UNICODE
#endif
#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>

#include <string>

namespace {

const char kMagic[8] = {'G', 'E', 'N', 'R', 'Y', 'B', 'L', '1'};
HWND g_wnd = nullptr;

std::wstring tempDir()
{
    wchar_t buf[MAX_PATH];
    GetTempPathW(MAX_PATH, buf);
    return std::wstring(buf) + L"GenryBL_Setup_" + std::to_wstring(GetTickCount64());
}

bool copyPayload(const std::wstring& zip)
{
    wchar_t self[MAX_PATH];
    GetModuleFileNameW(nullptr, self, MAX_PATH);
    HANDLE in = CreateFileW(self, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (in == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER size;
    GetFileSizeEx(in, &size);
    bool ok = false;
    char footer[16];
    LARGE_INTEGER pos;
    pos.QuadPart = size.QuadPart - 16;
    DWORD got = 0;
    if (size.QuadPart > 16 && SetFilePointerEx(in, pos, nullptr, FILE_BEGIN) && ReadFile(in, footer, 16, &got, nullptr) && got == 16 &&
        memcmp(footer + 8, kMagic, 8) == 0) {
        unsigned long long payload = 0;
        memcpy(&payload, footer, 8);
        pos.QuadPart = size.QuadPart - 16 - static_cast<long long>(payload);
        HANDLE out = CreateFileW(zip.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
        if (pos.QuadPart > 0 && out != INVALID_HANDLE_VALUE && SetFilePointerEx(in, pos, nullptr, FILE_BEGIN)) {
            static char buf[1 << 20];
            unsigned long long left = payload;
            ok = true;
            while (left && ok) {
                const DWORD want = static_cast<DWORD>(left < sizeof buf ? left : sizeof buf);
                DWORD r = 0, w = 0;
                ok = ReadFile(in, buf, want, &r, nullptr) && r == want && WriteFile(out, buf, r, &w, nullptr) && w == r;
                left -= r;
                MSG m;
                while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&m); DispatchMessageW(&m); }
            }
        }
        if (out != INVALID_HANDLE_VALUE) CloseHandle(out);
    }
    CloseHandle(in);
    return ok;
}

// run a program and keep the little window alive while it works
DWORD runAndWait(std::wstring cmd, const std::wstring& dir, bool hidden)
{
    STARTUPINFOW si{};
    si.cb = sizeof si;
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, hidden ? CREATE_NO_WINDOW : 0, nullptr,
                        dir.empty() ? nullptr : dir.c_str(), &si, &pi))
        return 0xFFFFFFFF;
    for (;;) {
        const DWORD r = MsgWaitForMultipleObjects(1, &pi.hProcess, FALSE, INFINITE, QS_ALLINPUT);
        if (r == WAIT_OBJECT_0) break;
        MSG m;
        while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&m); DispatchMessageW(&m); }
    }
    DWORD code = 0;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return code;
}

void removeTree(const std::wstring& dir)
{
    std::wstring from = dir + L'\0';
    SHFILEOPSTRUCTW op{};
    op.wFunc = FO_DELETE;
    op.pFrom = from.c_str();
    op.fFlags = FOF_NO_UI;
    SHFileOperationW(&op);
}

LRESULT CALLBACK proc(HWND h, UINT msg, WPARAM w, LPARAM l)
{
    if (msg == WM_CTLCOLORSTATIC) {
        SetTextColor(reinterpret_cast<HDC>(w), RGB(255, 221, 125));
        SetBkColor(reinterpret_cast<HDC>(w), RGB(23, 33, 28));
        static HBRUSH b = CreateSolidBrush(RGB(23, 33, 28));
        return reinterpret_cast<LRESULT>(b);
    }
    return DefWindowProcW(h, msg, w, l);
}

void showWindow(HINSTANCE inst)
{
    INITCOMMONCONTROLSEX ic{sizeof ic, ICC_PROGRESS_CLASS};
    InitCommonControlsEx(&ic);
    WNDCLASSW wc{};
    wc.lpfnWndProc = proc;
    wc.hInstance = inst;
    wc.hbrBackground = CreateSolidBrush(RGB(23, 33, 28));
    wc.lpszClassName = L"GenryBLSetupBoot";
    wc.hIcon = LoadIconW(inst, L"IDI_ICON1");
    wc.hCursor = LoadCursorW(nullptr, IDC_APPSTARTING);
    RegisterClassW(&wc);
    const int W = 460, H = 150;
    g_wnd = CreateWindowExW(WS_EX_TOPMOST, wc.lpszClassName, L"GenryBL", WS_POPUP | WS_BORDER, (GetSystemMetrics(SM_CXSCREEN) - W) / 2,
                            (GetSystemMetrics(SM_CYSCREEN) - H) / 2, W, H, nullptr, nullptr, inst, nullptr);
    HFONT big = CreateFontW(30, 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
    HFONT smallFont = CreateFontW(18, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
    HWND t1 = CreateWindowW(L"STATIC", L"GenryBL", WS_CHILD | WS_VISIBLE, 24, 18, 400, 36, g_wnd, nullptr, inst, nullptr);
    HWND t2 = CreateWindowW(L"STATIC", L"Распаковываю установщик…", WS_CHILD | WS_VISIBLE, 24, 58, 400, 24, g_wnd, nullptr, inst, nullptr);
    SendMessageW(t1, WM_SETFONT, reinterpret_cast<WPARAM>(big), TRUE);
    SendMessageW(t2, WM_SETFONT, reinterpret_cast<WPARAM>(smallFont), TRUE);
    HWND bar = CreateWindowW(PROGRESS_CLASSW, nullptr, WS_CHILD | WS_VISIBLE | PBS_MARQUEE, 24, 98, W - 48, 18, g_wnd, nullptr, inst, nullptr);
    SendMessageW(bar, PBM_SETMARQUEE, TRUE, 30);
    ShowWindow(g_wnd, SW_SHOW);
    UpdateWindow(g_wnd);
}

}   // namespace

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR cmdLine, int)
{
    // anything given to the setup goes on to the installer (GenryBL_Setup.exe --silent-to "<dir>" --es "<game>")
    const std::wstring extra = cmdLine && *cmdLine ? std::wstring(L" ") + cmdLine : std::wstring();
    showWindow(inst);
    const std::wstring dir = tempDir();
    CreateDirectoryW(dir.c_str(), nullptr);
    const std::wstring zip = dir + L"\\payload.zip";
    if (!copyPayload(zip)) {
        MessageBoxW(g_wnd, L"Установщик повреждён: внутри нет программы. Скачай GenryBL_Setup.exe заново.", L"GenryBL", MB_ICONERROR);
        removeTree(dir);
        return 1;
    }
    wchar_t sys[MAX_PATH];
    GetSystemDirectoryW(sys, MAX_PATH);
    const std::wstring tar = std::wstring(sys) + L"\\tar.exe";
    if (GetFileAttributesW(tar.c_str()) == INVALID_FILE_ATTRIBUTES ||
        runAndWait(L"\"" + tar + L"\" -xf \"" + zip + L"\" -C \"" + dir + L"\"", dir, true) != 0) {
        MessageBoxW(g_wnd, L"Не получилось распаковать программу (нужна Windows 10 или новее).", L"GenryBL", MB_ICONERROR);
        removeTree(dir);
        return 1;
    }
    DeleteFileW(zip.c_str());
    const std::wstring app = dir + L"\\GenryBL\\app\\GenryBL.exe";
    ShowWindow(g_wnd, SW_HIDE);
    const DWORD code = runAndWait(L"\"" + app + L"\" --install" + extra, dir + L"\\GenryBL\\app", false);
    removeTree(dir);
    return code == 0xFFFFFFFF ? 1 : int(code);
}
