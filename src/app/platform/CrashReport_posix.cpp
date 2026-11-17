// Dafin: on Linux a crash leaves <AppLocalData>/crash/crash-<time>.txt (UTC, e.g.
// ~/.local/share/Dafin/Dafin/crash): version, OS, graphics stack, the signal and where it
// happened, the last fatal Qt message, a likely cause in plain words and the call stack
// (backtrace_symbols_fd, module+offset). A crashed process may be unable to draw anything, so the
// window comes from a fresh process: the signal handler starts `dafin --crash-dialog <report>`
// (runCrashDialog below, a plain Qt Widgets message box, no OpenGL) and waits until OK is pressed;
// without Dafin's own helper it tries zenity, kdialog and xmessage. Nothing runs in normal use
// beyond installing the handlers once at start-up.
//
// The signal path only uses async-signal-safe calls (open/write/mkdir/fork/execv/waitpid and
// backtrace_symbols_fd, whose first use is done at install so libgcc is already loaded).

#include "app/platform/CrashReport.h"

#include <QApplication>
#include <QByteArray>
#include <QFile>
#include <QFileInfo>
#include <QMessageBox>
#include <QString>
#include <QStringList>
#include <QSysInfo>
#include <QtGlobal>

#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <exception>
#include <stdexcept>

#include <execinfo.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

// "build 33 (abc1234)" from CMakeLists.txt, as in CrashReport_win.cpp.
#ifndef DAFIN_BUILD_LABEL
#define DAFIN_BUILD_LABEL ""
#endif

namespace {

constexpr size_t kPathCap = 512;
char g_baseDir[kPathCap] = {};   // <AppLocalData>
char g_os[256] = {};
char g_gfx[512] = {};
char g_error[512] = {};
char g_reportPath[kPathCap + 64] = {};
char g_message[1536] = {};
volatile sig_atomic_t g_started = 0;
volatile sig_atomic_t g_busy = 0;
alignas(16) char g_altStack[64 * 1024];

QtMessageHandler g_previousHandler = nullptr;
std::terminate_handler g_previousTerminate = nullptr;

// ---- tiny async-signal-safe text helpers ------------------------------------------------------

void copyTo(char *dst, size_t cap, const char *src)
{
    if (!cap)
        return;
    size_t i = 0;
    for (; src && src[i] && i + 1 < cap; ++i)
        dst[i] = src[i];
    dst[i] = '\0';
}

void appendTo(char *dst, size_t cap, const char *src)
{
    size_t n = std::strlen(dst);
    for (size_t i = 0; src && src[i] && n + 1 < cap; ++i)
        dst[n++] = src[i];
    dst[n] = '\0';
}

void put(int fd, const char *text)
{
    size_t left = text ? std::strlen(text) : 0;
    while (left) {
        const ssize_t n = ::write(fd, text, left);
        if (n <= 0)
            break;
        text += n;
        left -= size_t(n);
    }
}

void putDec(int fd, long long value)
{
    char buf[24];
    int i = sizeof buf;
    buf[--i] = '\0';
    const bool negative = value < 0;
    unsigned long long v = negative ? 0ULL - static_cast<unsigned long long>(value) : static_cast<unsigned long long>(value);
    do {
        buf[--i] = char('0' + v % 10);
        v /= 10;
    } while (v);
    if (negative)
        buf[--i] = '-';
    put(fd, buf + i);
}

void putHex(int fd, unsigned long long value)
{
    char buf[24];
    int i = sizeof buf;
    buf[--i] = '\0';
    do {
        buf[--i] = "0123456789abcdef"[value & 15];
        value >>= 4;
    } while (value);
    buf[--i] = 'x';
    buf[--i] = '0';
    put(fd, buf + i);
}

void two(char *out, int v)
{
    out[0] = char('0' + (v / 10) % 10);
    out[1] = char('0' + v % 10);
}

const char *signalName(int sig)
{
    switch (sig) {
    case SIGSEGV: return "SIGSEGV (invalid memory access)";
    case SIGBUS: return "SIGBUS (bus error)";
    case SIGFPE: return "SIGFPE (arithmetic error)";
    case SIGILL: return "SIGILL (illegal instruction)";
    case SIGABRT: return "SIGABRT (abort, fatal error or uncaught exception)";
    default: return "signal";
    }
}

// mkdir -p for <base>/crash, one component at a time.
void makeCrashDir(char *out, size_t cap)
{
    copyTo(out, cap, g_baseDir);
    for (char *p = out + 1; *p; ++p) {
        if (*p == '/') {
            *p = '\0';
            ::mkdir(out, 0755);
            *p = '/';
        }
    }
    ::mkdir(out, 0755);
    appendTo(out, cap, "/crash");
    ::mkdir(out, 0755);
}

// ---- the window -------------------------------------------------------------------------------

bool runAndWait(const char *program, char *const argv[])
{
    const pid_t pid = ::fork();
    if (pid < 0)
        return false;
    if (pid == 0) {
        ::execv(program, argv);
        ::_exit(127);
    }
    int status = 0;
    while (::waitpid(pid, &status, 0) < 0 && errno == EINTR) {
    }
    return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

void showWindow(bool wroteReport)
{
    // No display (a server, a script): nobody to show anything to; the report is on disk.
    if (!std::getenv("DISPLAY") && !std::getenv("WAYLAND_DISPLAY"))
        return;

    // 1) Dafin's own helper: a fresh process, so none of the crashed one's state is in the way.
    char helperArg[kPathCap + 64];
    copyTo(helperArg, sizeof helperArg, wroteReport ? g_reportPath : "-");
    char *helper[] = {const_cast<char *>("dafin"), const_cast<char *>("--crash-dialog"), helperArg, nullptr};
    if (runAndWait("/proc/self/exe", helper))
        return;

    // 2) Whatever small dialog tool the system has.
    copyTo(g_message, sizeof g_message, g_started ? "Dafin crashed.\n\n" : "Dafin could not start.\n\n");
    appendTo(g_message, sizeof g_message, dafin::crashLikelyCause(g_error));
    appendTo(g_message, sizeof g_message, wroteReport ? "\n\nA report was saved here:\n" : "\n\nA report could not be saved.\n");
    if (wroteReport)
        appendTo(g_message, sizeof g_message, g_reportPath);
    appendTo(g_message, sizeof g_message, "\n\nPlease send it to the Dafin developer.");

    char *zenity[] = {const_cast<char *>("zenity"), const_cast<char *>("--error"), const_cast<char *>("--no-markup"),
                      const_cast<char *>("--title=Dafin"), const_cast<char *>("--text"), g_message, nullptr};
    if (runAndWait("/usr/bin/zenity", zenity) || runAndWait("/usr/local/bin/zenity", zenity))
        return;
    char *kdialog[] = {const_cast<char *>("kdialog"), const_cast<char *>("--error"), g_message, nullptr};
    if (runAndWait("/usr/bin/kdialog", kdialog))
        return;
    char *xmessage[] = {const_cast<char *>("xmessage"), const_cast<char *>("-center"), g_message, nullptr};
    runAndWait("/usr/bin/xmessage", xmessage);
}

// ---- the report -------------------------------------------------------------------------------

void onSignal(int sig, siginfo_t *info, void *)
{
    // A second thread crashing while the first one shows the window just waits.
    if (__sync_lock_test_and_set(&g_busy, 1)) {
        for (;;)
            ::pause();
    }

    char dir[kPathCap + 16];
    makeCrashDir(dir, sizeof dir);

    // crash-YYYYMMDD-HHMMSS.txt, in UTC (gmtime_r takes no locks).
    time_t now = ::time(nullptr);
    struct tm tmv;
    ::gmtime_r(&now, &tmv);
    char stamp[24] = "crash-";
    two(stamp + 6, (tmv.tm_year + 1900) / 100);
    two(stamp + 8, tmv.tm_year + 1900);
    two(stamp + 10, tmv.tm_mon + 1);
    two(stamp + 12, tmv.tm_mday);
    stamp[14] = '-';
    two(stamp + 15, tmv.tm_hour);
    two(stamp + 17, tmv.tm_min);
    two(stamp + 19, tmv.tm_sec);
    stamp[21] = '\0';
    copyTo(g_reportPath, sizeof g_reportPath, dir);
    appendTo(g_reportPath, sizeof g_reportPath, "/");
    appendTo(g_reportPath, sizeof g_reportPath, stamp);
    appendTo(g_reportPath, sizeof g_reportPath, ".txt");

    const int fd = ::open(g_reportPath, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd >= 0) {
        put(fd, "Dafin " DRIFT_VERSION);
        if (DAFIN_BUILD_LABEL[0]) {
            put(fd, ", ");
            put(fd, DAFIN_BUILD_LABEL);
        }
        put(fd, g_started ? " crashed\n" : " could not start\n");
        {
            char when[32];
            two(when, (tmv.tm_year + 1900) / 100);
            two(when + 2, tmv.tm_year + 1900);
            when[4] = '-';
            two(when + 5, tmv.tm_mon + 1);
            when[7] = '-';
            two(when + 8, tmv.tm_mday);
            when[10] = ' ';
            two(when + 11, tmv.tm_hour);
            when[13] = ':';
            two(when + 14, tmv.tm_min);
            when[16] = ':';
            two(when + 17, tmv.tm_sec);
            when[19] = '\0';
            put(fd, "Time (UTC): ");
            put(fd, when);
        }
        put(fd, "\nOS: ");
        put(fd, g_os);
        put(fd, "\nGraphics: ");
        put(fd, g_gfx[0] ? g_gfx : "not known (the OpenGL check had not run)");
        put(fd, "\nError: signal ");
        putDec(fd, sig);
        put(fd, ", ");
        put(fd, signalName(sig));
        if (info && (sig == SIGSEGV || sig == SIGBUS || sig == SIGFPE || sig == SIGILL)) {
            put(fd, ", address ");
            putHex(fd, reinterpret_cast<unsigned long long>(info->si_addr));
        }
        put(fd, ", thread ");
        putDec(fd, ::syscall(SYS_gettid));
        put(fd, "\nLast error message: ");
        put(fd, g_error[0] ? g_error : "none");
        put(fd, "\nLikely cause: ");
        put(fd, dafin::crashLikelyCause(g_error));
        put(fd, "\nStack:\n");
        void *frames[64];
        const int count = ::backtrace(frames, 64);
        ::backtrace_symbols_fd(frames, count, fd);
        ::close(fd);
    }

    showWindow(fd >= 0);
    // SA_RESETHAND put the default action back: returning runs it (a core dump or exit as usual).
}

// ---- Qt and C++ hooks -------------------------------------------------------------------------

void onMessage(QtMsgType type, const QMessageLogContext &context, const QString &message)
{
    if (type == QtFatalMsg)
        copyTo(g_error, sizeof g_error, message.toUtf8().constData());
    if (g_previousHandler) {
        g_previousHandler(type, context, message);
        return;
    }
    // Qt's own handler is not reachable once replaced; this is what it prints.
    const QByteArray out = qFormatLogMessage(type, context, message).toLocal8Bit();
    std::fprintf(stderr, "%s\n", out.constData());
    std::fflush(stderr);
    if (type == QtFatalMsg)
        std::abort();
}

[[noreturn]] void onTerminate()
{
    try {
        if (std::current_exception()) {
            try {
                throw;
            } catch (const std::exception &e) {
                copyTo(g_error, sizeof g_error, "uncaught exception: ");
                appendTo(g_error, sizeof g_error, e.what());
            } catch (...) {
                copyTo(g_error, sizeof g_error, "uncaught exception of unknown type");
            }
        } else {
            copyTo(g_error, sizeof g_error, "std::terminate called");
        }
    } catch (...) {
    }
    if (g_previousTerminate && g_previousTerminate != onTerminate)
        g_previousTerminate();
    std::abort(); // SIGABRT -> onSignal
}

} // namespace

namespace dafin {

void installCrashReport()
{
    // <AppLocalData> as Qt will name it once the application names are set (later in main()):
    // $XDG_DATA_HOME or ~/.local/share, then Dafin/Dafin.
    const char *data = std::getenv("XDG_DATA_HOME");
    const char *home = std::getenv("HOME");
    if (data && *data) {
        copyTo(g_baseDir, sizeof g_baseDir, data);
    } else if (home && *home) {
        copyTo(g_baseDir, sizeof g_baseDir, home);
        appendTo(g_baseDir, sizeof g_baseDir, "/.local/share");
    } else {
        copyTo(g_baseDir, sizeof g_baseDir, "/tmp");
    }
    appendTo(g_baseDir, sizeof g_baseDir, "/Dafin/Dafin");

    copyTo(g_os, sizeof g_os,
           QStringLiteral("%1, kernel %2 %3, cpu %4")
               .arg(QSysInfo::prettyProductName(), QSysInfo::kernelType(), QSysInfo::kernelVersion(),
                    QSysInfo::currentCpuArchitecture())
               .toUtf8()
               .constData());

    void *warm[2];
    ::backtrace(warm, 2); // loads libgcc now, not inside a signal handler

    g_previousHandler = qInstallMessageHandler(onMessage);
    g_previousTerminate = std::set_terminate(onTerminate);

    stack_t alt = {};
    alt.ss_sp = g_altStack;
    alt.ss_size = sizeof g_altStack;
    ::sigaltstack(&alt, nullptr); // a stack overflow still gets a report

    struct sigaction action = {};
    action.sa_sigaction = onSignal;
    action.sa_flags = SA_SIGINFO | SA_ONSTACK | SA_RESETHAND;
    ::sigemptyset(&action.sa_mask);
    for (const int sig : {SIGSEGV, SIGBUS, SIGFPE, SIGILL, SIGABRT})
        ::sigaction(sig, &action, nullptr);
}

void setCrashGraphicsInfo(const char *utf8)
{
    copyTo(g_gfx, sizeof g_gfx, utf8);
}

void markCrashReportStarted()
{
    g_started = 1;
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

// `dafin --crash-dialog <report>`: runs in its own process after the crash, so it starts a normal
// (widgets, no OpenGL) message box. "-" or an unreadable path means no report could be saved.
int runCrashDialog(int argc, char **argv)
{
    QString report;
    for (int i = 1; i + 1 < argc; ++i) {
        if (std::strcmp(argv[i], "--crash-dialog") == 0)
            report = QString::fromLocal8Bit(argv[i + 1]);
    }
    QString headline = QStringLiteral("Dafin crashed.");
    QString cause;
    QFile file(report);
    const bool haveReport = report != QLatin1String("-") && file.open(QIODevice::ReadOnly);
    if (haveReport) {
        const QStringList lines = QString::fromUtf8(file.readAll()).split(QLatin1Char('\n'));
        if (!lines.isEmpty() && lines.first().endsWith(QLatin1String("could not start")))
            headline = QStringLiteral("Dafin could not start.");
        for (const QString &line : lines) {
            if (line.startsWith(QLatin1String("Likely cause: ")))
                cause = line.mid(14).trimmed();
        }
    }

    QString text = headline;
    if (!cause.isEmpty())
        text += QStringLiteral("\n\n") + cause;
    if (haveReport) {
        const QString crashDir = QFileInfo(report).absolutePath();
        text += QStringLiteral("\n\nA report was saved here:\n%1\n\nPlease send it to the Dafin developer. If you can, "
                               "also send the newest file in the logs folder:\n%2")
                    .arg(report, crashDir + QStringLiteral("/../logs"));
    } else {
        text += QStringLiteral("\n\nA report could not be saved.");
    }

    QApplication app(argc, argv);
    QMessageBox box(QMessageBox::Critical, QStringLiteral("Dafin"), text, QMessageBox::Ok);
    box.setWindowFlag(Qt::WindowStaysOnTopHint, true);
    box.exec();
    return 0;
}

} // namespace dafin
