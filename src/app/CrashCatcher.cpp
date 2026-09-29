#include "CrashCatcher.h"

#include <QDir>
#include <QFileInfo>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <dbghelp.h>
#include <tlhelp32.h>

#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>

namespace {

// everything the handler needs is ready before anything falls: no allocations of Qt strings in a dying process
wchar_t g_dir[1024];
wchar_t g_log[1024];
char g_version[64];
volatile LONG g_busy = 0;

void walk(FILE* f, HANDLE process, HANDLE thread, CONTEXT ctx)
{
    STACKFRAME64 sf{};
    sf.AddrPC.Offset = ctx.Rip;
    sf.AddrPC.Mode = AddrModeFlat;
    sf.AddrFrame.Offset = ctx.Rbp;
    sf.AddrFrame.Mode = AddrModeFlat;
    sf.AddrStack.Offset = ctx.Rsp;
    sf.AddrStack.Mode = AddrModeFlat;
    for (int i = 0; i < 48; ++i) {
        if (!StackWalk64(IMAGE_FILE_MACHINE_AMD64, process, thread, &sf, &ctx, nullptr, SymFunctionTableAccess64, SymGetModuleBase64, nullptr))
            break;
        const DWORD64 pc = sf.AddrPC.Offset;
        if (!pc) break;
        char module[MAX_PATH] = "?";
        const DWORD64 base = SymGetModuleBase64(process, pc);
        if (base) GetModuleFileNameA(reinterpret_cast<HMODULE>(base), module, MAX_PATH);
        const char* name = strrchr(module, '\\');
        name = name ? name + 1 : module;
        alignas(SYMBOL_INFO) char buf[sizeof(SYMBOL_INFO) + 512];
        auto* sym = reinterpret_cast<SYMBOL_INFO*>(buf);
        memset(buf, 0, sizeof(buf));
        sym->SizeOfStruct = sizeof(SYMBOL_INFO);
        sym->MaxNameLen = 511;
        DWORD64 disp = 0;
        IMAGEHLP_LINE64 line{};
        line.SizeOfStruct = sizeof(line);
        DWORD lineDisp = 0;
        fprintf(f, "  #%02d %s+0x%llx", i, name, static_cast<unsigned long long>(pc - base));
        if (SymFromAddr(process, pc, &disp, sym)) fprintf(f, "  %s+0x%llx", sym->Name, static_cast<unsigned long long>(disp));
        if (SymGetLineFromAddr64(process, pc, &lineDisp, &line)) fprintf(f, "  (%s:%lu)", line.FileName, line.LineNumber);
        fputc('\n', f);
    }
}

void report(EXCEPTION_POINTERS* ep, const char* what)
{
    if (InterlockedExchange(&g_busy, 1)) return;           // one report, even if the handler itself trips
    SYSTEMTIME t;
    GetLocalTime(&t);
    wchar_t base[1400];
    swprintf(base, 1400, L"%s\\GenryBL_%04u%02u%02u_%02u%02u%02u", g_dir, t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
    CreateDirectoryW(g_dir, nullptr);
    const HANDLE process = GetCurrentProcess();

    // the minidump first: it needs the threads as they are
    wchar_t dmp[1500];
    swprintf(dmp, 1500, L"%s.dmp", base);
    const HANDLE df = CreateFileW(dmp, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (df != INVALID_HANDLE_VALUE) {
        MINIDUMP_EXCEPTION_INFORMATION mei{GetCurrentThreadId(), ep, FALSE};
        MiniDumpWriteDump(process, GetCurrentProcessId(), df,
                          MINIDUMP_TYPE(MiniDumpWithThreadInfo | MiniDumpWithIndirectlyReferencedMemory | MiniDumpScanMemory),
                          ep ? &mei : nullptr, nullptr, nullptr);
        CloseHandle(df);
    }

    wchar_t txt[1500];
    swprintf(txt, 1500, L"%s.txt", base);
    FILE* f = nullptr;
    if (_wfopen_s(&f, txt, L"w") != 0 || !f) return;
    fprintf(f, "GenryBL %s crashed: %s", g_version, what);
    if (ep && ep->ExceptionRecord) {
        fprintf(f, " (code 0x%08lX at 0x%p", ep->ExceptionRecord->ExceptionCode, ep->ExceptionRecord->ExceptionAddress);
        if (ep->ExceptionRecord->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && ep->ExceptionRecord->NumberParameters >= 2)
            fprintf(f, ", %s 0x%llx", ep->ExceptionRecord->ExceptionInformation[0] ? "writing" : "reading",
                    static_cast<unsigned long long>(ep->ExceptionRecord->ExceptionInformation[1]));
        fputc(')', f);
    }
    fprintf(f, "\n%04u-%02u-%02u %02u:%02u:%02u\n\n", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);

    SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS | SYMOPT_LOAD_LINES);
    SymInitialize(process, nullptr, TRUE);                  // GenryBL.pdb next to the exe; Qt: its exports
    const DWORD me = GetCurrentThreadId();
    fprintf(f, "== the thread that fell (%lu)\n", me);
    CONTEXT here{};
    if (ep && ep->ContextRecord) here = *ep->ContextRecord;
    else RtlCaptureContext(&here);
    walk(f, process, GetCurrentThread(), here);

    // every other thread, frozen where it stood: the other half of a race
    const HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snap != INVALID_HANDLE_VALUE) {
        THREADENTRY32 te{};
        te.dwSize = sizeof(te);
        for (BOOL ok = Thread32First(snap, &te); ok; ok = Thread32Next(snap, &te)) {
            if (te.th32OwnerProcessID != GetCurrentProcessId() || te.th32ThreadID == me) continue;
            const HANDLE th = OpenThread(THREAD_GET_CONTEXT | THREAD_SUSPEND_RESUME | THREAD_QUERY_INFORMATION, FALSE, te.th32ThreadID);
            if (!th) continue;
            SuspendThread(th);
            CONTEXT c{};
            c.ContextFlags = CONTEXT_FULL;
            if (GetThreadContext(th, &c)) {
                fprintf(f, "\n== thread %lu\n", te.th32ThreadID);
                walk(f, process, th, c);
            }
            CloseHandle(th);                                // left suspended: the process is going down anyway
        }
        CloseHandle(snap);
    }

    // what the program was doing: the end of its own log
    const HANDLE lf = CreateFileW(g_log, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (lf != INVALID_HANDLE_VALUE) {
        LARGE_INTEGER size{};
        GetFileSizeEx(lf, &size);
        const LONGLONG want = size.QuadPart > 12000 ? 12000 : size.QuadPart;
        LARGE_INTEGER pos;
        pos.QuadPart = size.QuadPart - want;
        SetFilePointerEx(lf, pos, nullptr, FILE_BEGIN);
        static char tail[12001];
        DWORD got = 0;
        ReadFile(lf, tail, DWORD(want), &got, nullptr);
        tail[got] = 0;
        fprintf(f, "\n== the end of genrybl.log\n%s\n", tail);
        CloseHandle(lf);
    }
    fclose(f);
}

LONG WINAPI onException(EXCEPTION_POINTERS* ep)
{
    report(ep, "an exception");
    return EXCEPTION_CONTINUE_SEARCH;                      // Windows closes it as before
}

void onAbort(int)
{
    report(nullptr, "abort() (qFatal / std::terminate / a failed check)");
}

void onInvalidParameter(const wchar_t*, const wchar_t*, const wchar_t*, unsigned int, uintptr_t)
{
    report(nullptr, "the C runtime's invalid-parameter kill");
    TerminateProcess(GetCurrentProcess(), 0xC0000409);
}

}   // namespace

namespace crash {

void install(const QString& crashDir, const QString& logPath, const QString& version)
{
    const QString d = QDir::toNativeSeparators(crashDir), l = QDir::toNativeSeparators(logPath);
    wcsncpy_s(g_dir, reinterpret_cast<const wchar_t*>(d.utf16()), _TRUNCATE);
    wcsncpy_s(g_log, reinterpret_cast<const wchar_t*>(l.utf16()), _TRUNCATE);
    strncpy_s(g_version, version.toLatin1().constData(), _TRUNCATE);
    // no «GenryBL has stopped working» box from Windows: the report is written and GenryBL says so itself next time
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    SetUnhandledExceptionFilter(onException);
    signal(SIGABRT, onAbort);
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
    _set_invalid_parameter_handler(onInvalidParameter);
    std::set_terminate([] { report(nullptr, "std::terminate (an uncaught C++ exception)"); std::abort(); });
}

QString newestReport(const QString& crashDir)
{
    const QFileInfoList l = QDir(crashDir).entryInfoList({QStringLiteral("GenryBL_*.txt")}, QDir::Files, QDir::Time);
    return l.isEmpty() ? QString() : l.first().absoluteFilePath();
}

}   // namespace crash

#else

namespace crash {
void install(const QString&, const QString&, const QString&) {}
QString newestReport(const QString&) { return {}; }
}

#endif
