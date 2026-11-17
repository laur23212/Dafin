// Dafin: on Windows, a crash leaves %LOCALAPPDATA%\Dafin\crash\crash-<time>.txt behind: the
// exception, where it happened and the call stack, each frame as module + offset. With the
// dafin.pdb of the same build (the windows-symbols-<arch> artifact) the offsets read back as
// function, file and line: run the Build workflow with platforms=crash-report and the report's
// text, or llvm-symbolizer --obj=dafin.exe --relative-address <offset> beside the .pdb.
// The last Qt messages before the crash go to dafin-log.txt beside it, rewritten every start.
// Dafin never closes silently: besides unhandled exceptions, abort() (qFatal, a failed OpenGL
// context in Qt Quick, std::terminate), pure virtual calls and invalid CRT parameters write the
// same report and show the same message box. Not catchable by anyone: a __fastfail, a kill from
// outside and a DLL missing at load time (Windows shows its own box for that one).

#include "app/platform/CrashReport.h"

#include <QByteArray>
#include <QString>
#include <QSysInfo>
#include <QtGlobal>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <intrin.h>

#include <csignal>
#include <cstdarg>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <exception>
#include <mutex>
#include <stdexcept>

// "build 33 (abc1234)" from CMakeLists.txt, so a report names the dafin.pdb that reads it back
// (Build workflow, platforms=crash-report).
#ifndef DAFIN_BUILD_LABEL
#define DAFIN_BUILD_LABEL ""
#endif

namespace {

wchar_t g_dir[MAX_PATH] = {};
HANDLE g_log = INVALID_HANDLE_VALUE;
std::mutex g_logMutex;
QtMessageHandler g_previousHandler = nullptr;
std::terminate_handler g_previousTerminate = nullptr;
char g_os[256] = {};
char g_gfx[512] = {};
char g_error[512] = {};
volatile LONG g_started = 0;
volatile LONG g_inCrash = 0;

// Exception codes for the runtime failures that never raise an exception of their own.
constexpr DWORD kAbortCode = 0xE0DAF001;
constexpr DWORD kPurecallCode = 0xE0DAF002;
constexpr DWORD kInvalidParameterCode = 0xE0DAF003;

void setText(char *dst, size_t cap, const char *src)
{
    if (!cap)
        return;
    size_t i = 0;
    for (; src && src[i] && i + 1 < cap; ++i)
        dst[i] = src[i];
    dst[i] = '\0';
}

void writeAll(HANDLE file, const char *text, int length)
{
    DWORD written = 0;
    if (length > 0)
        WriteFile(file, text, DWORD(length), &written, nullptr);
}

void writeLine(HANDLE file, const char *format, ...)
{
    char buffer[1024];
    va_list args;
    va_start(args, format);
    const int n = _vsnprintf_s(buffer, sizeof buffer, _TRUNCATE, format, args);
    va_end(args);
    writeAll(file, buffer, n < 0 ? int(strlen(buffer)) : n);
    writeAll(file, "\r\n", 2);
}

// "Qt6Quick.dll+0x1a2b3" for an address, from the module that holds it.
void describe(HANDLE file, int index, DWORD64 address)
{
    HMODULE module = nullptr;
    wchar_t path[MAX_PATH] = L"?";
    if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCWSTR>(address), &module)
        && module) {
        GetModuleFileNameW(module, path, MAX_PATH);
    }
    const wchar_t *name = wcsrchr(path, L'\\');
    name = name ? name + 1 : path;
    char utf8[MAX_PATH * 3] = {};
    WideCharToMultiByte(CP_UTF8, 0, name, -1, utf8, int(sizeof utf8), nullptr, nullptr);
    const DWORD64 offset = module ? address - reinterpret_cast<DWORD64>(module) : address;
    writeLine(file, "  #%02d  %s+0x%llx", index, utf8, static_cast<unsigned long long>(offset));
}

LONG WINAPI onCrash(EXCEPTION_POINTERS *info)
{
    // A second thread crashing while the first one shows the message box just waits for it.
    if (InterlockedExchange(&g_inCrash, 1))
        Sleep(INFINITE);
    SYSTEMTIME now;
    GetLocalTime(&now);
    wchar_t path[MAX_PATH + 64];
    swprintf_s(path, L"%ls\\crash-%04d%02d%02d-%02d%02d%02d.txt", g_dir, now.wYear, now.wMonth, now.wDay, now.wHour,
               now.wMinute, now.wSecond);
    HANDLE file = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        // No report possible (no LOCALAPPDATA, disk full): still say so rather than vanish.
        MessageBoxW(nullptr,
                    g_started ? L"Dafin crashed.\n\nA report could not be saved."
                              : L"Dafin could not start.\n\nA report could not be saved.",
                    L"Dafin", MB_OK | MB_ICONERROR | MB_SETFOREGROUND | MB_TOPMOST);
        return EXCEPTION_CONTINUE_SEARCH;
    }

    const EXCEPTION_RECORD *record = info->ExceptionRecord;
    const char *outcome = g_started ? "crashed" : "could not start";
    if (DAFIN_BUILD_LABEL[0])
        writeLine(file, "Dafin %s, %s, %s", DRIFT_VERSION, DAFIN_BUILD_LABEL, outcome);
    else
        writeLine(file, "Dafin %s %s", DRIFT_VERSION, outcome);
    writeLine(file, "Time: %04d-%02d-%02d %02d:%02d:%02d", now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute,
              now.wSecond);
    writeLine(file, "OS: %s", g_os);
    writeLine(file, "Graphics: %s", g_gfx[0] ? g_gfx : "not known (the OpenGL check had not run)");
    writeLine(file, "Last error message: %s", g_error[0] ? g_error : "none");
    writeLine(file, "Likely cause: %s", dafin::crashLikelyCause(g_error));
    writeLine(file, "Exception 0x%08lx at 0x%p, thread %lu", record->ExceptionCode, record->ExceptionAddress,
              GetCurrentThreadId());
    if (record->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && record->NumberParameters >= 2) {
        writeLine(file, "%s address 0x%llx", record->ExceptionInformation[0] == 8 ? "Executing"
                                             : record->ExceptionInformation[0] == 1 ? "Writing" : "Reading",
                  static_cast<unsigned long long>(record->ExceptionInformation[1]));
    }
    writeLine(file, "Stack:");

#if defined(_M_X64)
    CONTEXT context = *info->ContextRecord;
    // A call through a null pointer lands at 0 with the caller's return address on top of the
    // stack: start from there.
    if (context.Rip < 0x10000) {
        describe(file, 0, context.Rip);
        context.Rip = *reinterpret_cast<DWORD64 *>(context.Rsp);
        context.Rsp += 8;
    }
    for (int i = 1; i < 64 && context.Rip; ++i) {
        describe(file, i, context.Rip);
        DWORD64 imageBase = 0;
        PRUNTIME_FUNCTION function = RtlLookupFunctionEntry(context.Rip, &imageBase, nullptr);
        if (!function) {
            // A leaf function: the return address is on top of the stack.
            context.Rip = *reinterpret_cast<DWORD64 *>(context.Rsp);
            context.Rsp += 8;
            continue;
        }
        PVOID handlerData = nullptr;
        DWORD64 establisherFrame = 0;
        RtlVirtualUnwind(UNW_FLAG_NHANDLER, imageBase, context.Rip, function, &context, &handlerData,
                         &establisherFrame, nullptr);
    }
#else
    describe(file, 0, reinterpret_cast<DWORD64>(record->ExceptionAddress));
#endif
    CloseHandle(file);

    {
        std::lock_guard<std::mutex> lock(g_logMutex);
        if (g_log != INVALID_HANDLE_VALUE)
            FlushFileBuffers(g_log);
    }
    wchar_t cause[512] = {};
    MultiByteToWideChar(CP_UTF8, 0, dafin::crashLikelyCause(g_error), -1, cause, 512);
    wchar_t message[MAX_PATH + 1024];
    swprintf_s(message, L"%ls%ls%ls\n\nA report was saved here:\n%ls\n\n"
                        L"Please send it to the Dafin developer, together with dafin-log.txt from the same folder.",
               g_started ? L"Dafin crashed." : L"Dafin could not start.", cause[0] ? L"\n\n" : L"", cause, path);
    MessageBoxW(nullptr, message, L"Dafin", MB_OK | MB_ICONERROR | MB_SETFOREGROUND | MB_TOPMOST);
    // Windows still records it (Event Viewer) and ends the process.
    return EXCEPTION_CONTINUE_SEARCH;
}

void onMessage(QtMsgType type, const QMessageLogContext &context, const QString &message)
{
    if (type == QtFatalMsg)
        setText(g_error, sizeof g_error, message.toUtf8().constData());
    {
        static const char *const kinds[] = {"debug", "warning", "critical", "fatal", "info"};
        const QByteArray line = QByteArray(kinds[qBound(0, int(type), 4)]) + ": " + message.toUtf8() + "\r\n";
        std::lock_guard<std::mutex> lock(g_logMutex);
        if (g_log != INVALID_HANDLE_VALUE)
            writeAll(g_log, line.constData(), int(line.size()));
    }
    if (g_previousHandler)
        g_previousHandler(type, context, message);
}

// abort() (qFatal, std::terminate), a pure virtual call and an invalid CRT parameter do not go
// through the unhandled-exception filter, so each makes the same report from here.
void reportRuntimeFailure(DWORD code)
{
    CONTEXT context = {};
    RtlCaptureContext(&context);
    EXCEPTION_RECORD record = {};
    record.ExceptionCode = code;
    record.ExceptionAddress = _ReturnAddress();
    EXCEPTION_POINTERS pointers = {&record, &context};
    onCrash(&pointers);
}

void onAbort(int)
{
    reportRuntimeFailure(kAbortCode);
    TerminateProcess(GetCurrentProcess(), 3);
}

void onPurecall()
{
    setText(g_error, sizeof g_error, "pure virtual function call");
    reportRuntimeFailure(kPurecallCode);
    TerminateProcess(GetCurrentProcess(), 3);
}

void onInvalidParameter(const wchar_t *, const wchar_t *, const wchar_t *, unsigned int, uintptr_t)
{
    setText(g_error, sizeof g_error, "invalid parameter passed to the C runtime");
    reportRuntimeFailure(kInvalidParameterCode);
    TerminateProcess(GetCurrentProcess(), 3);
}

[[noreturn]] void onTerminate()
{
    try {
        if (std::current_exception()) {
            try {
                throw;
            } catch (const std::exception &e) {
                setText(g_error, sizeof g_error, "uncaught exception: ");
                const size_t used = strlen(g_error);
                setText(g_error + used, sizeof g_error - used, e.what());
            } catch (...) {
                setText(g_error, sizeof g_error, "uncaught exception of unknown type");
            }
        } else {
            setText(g_error, sizeof g_error, "std::terminate called");
        }
    } catch (...) {
    }
    if (g_previousTerminate && g_previousTerminate != onTerminate)
        g_previousTerminate();
    std::abort(); // -> onAbort
}

} // namespace

namespace dafin {

void installCrashReport()
{
    wchar_t base[MAX_PATH] = {};
    const DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", base, MAX_PATH);
    if (n == 0 || n >= MAX_PATH)
        return;
    swprintf_s(g_dir, L"%ls\\Dafin", base);
    CreateDirectoryW(g_dir, nullptr);
    wcscat_s(g_dir, L"\\crash");
    CreateDirectoryW(g_dir, nullptr);

    wchar_t logPath[MAX_PATH + 32];
    swprintf_s(logPath, L"%ls\\dafin-log.txt", g_dir);
    // Write-through, so the lines just before a crash are on disk.
    g_log = CreateFileW(logPath, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS,
                        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH, nullptr);
    g_previousHandler = qInstallMessageHandler(onMessage);
    SetUnhandledExceptionFilter(onCrash);

    setText(g_os, sizeof g_os,
            QStringLiteral("%1, kernel %2 %3, cpu %4")
                .arg(QSysInfo::prettyProductName(), QSysInfo::kernelType(), QSysInfo::kernelVersion(),
                     QSysInfo::currentCpuArchitecture())
                .toUtf8()
                .constData());
    signal(SIGABRT, onAbort);
    _set_abort_behavior(0, _CALL_REPORTFAULT); // no Windows Error Reporting box after ours
    _set_purecall_handler(onPurecall);
    _set_invalid_parameter_handler(onInvalidParameter);
    g_previousTerminate = std::set_terminate(onTerminate);
}

void setCrashGraphicsInfo(const char *utf8)
{
    setText(g_gfx, sizeof g_gfx, utf8);
}

void markCrashReportStarted()
{
    InterlockedExchange(&g_started, 1);
}

void forceCrashIfRequested()
{
    const QByteArray mode = qgetenv("DAFIN_FORCE_CRASH");
    if (mode.isEmpty() || mode == "0")
        return;
    if (mode == "abort")
        std::abort();
    if (mode == "terminate") {
        try {
            throw std::runtime_error("DAFIN_FORCE_CRASH test exception");
        } catch (...) {
            std::terminate();
        }
    }
    if (mode == "start")
        qFatal("Failed to create OpenGL context for format QSurfaceFormat(version 3.3) "
               "(fake start-up failure, DAFIN_FORCE_CRASH=start)");
    if (mode == "late")
        markCrashReportStarted(); // same crash below, worded as "crashed" instead of "could not start"
    volatile int *nothing = nullptr;
    *nothing = 1; // "1": a real invalid memory write
}

} // namespace dafin
