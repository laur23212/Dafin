#pragma once

#include <cstring>

namespace dafin {

// Dafin must never close silently. When it crashes (or cannot start) it leaves a report and says
// so in a window the user closes with OK:
//  - Windows (CrashReport_win.cpp): %LOCALAPPDATA%\Dafin\crash\crash-<time>.txt and dafin-log.txt,
//    and a native MessageBox from the crashing process.
//  - Linux (CrashReport_posix.cpp): <AppLocalData>/crash/crash-<time>.txt, and a second, fresh
//    process (`dafin --crash-dialog <report>`) shows the window, since the crashed one may be
//    unable to draw anything itself.
// Elsewhere these are no-ops.
#if defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__))
void installCrashReport();
// One line about the graphics stack ("NVIDIA ..., OpenGL 3.1 ..."); goes into the report and the
// "likely cause" of a start-up crash. Safe to call from any thread, any number of times.
void setCrashGraphicsInfo(const char *utf8);
// The first frame was drawn: from now on a crash is "Dafin crashed", before it "could not start".
void markCrashReportStarted();
// DAFIN_FORCE_CRASH=1 (null write) | abort | terminate | start (qFatal like a failed OpenGL
// context) makes a fake crash right here, to see the window. One getenv; nothing otherwise.
void forceCrashIfRequested();
#else
inline void installCrashReport() {}
inline void setCrashGraphicsInfo(const char *) {}
inline void markCrashReportStarted() {}
inline void forceCrashIfRequested() {}
#endif

// `dafin --crash-dialog <report>`: the Linux helper process. Returns the exit code.
#if defined(__linux__) && !defined(__ANDROID__)
int runCrashDialog(int argc, char **argv);
#else
inline int runCrashDialog(int, char **) { return 0; }
#endif

// Plain words for what an error text probably means; "" when nothing is known.
inline const char *crashLikelyCause(const char *error)
{
    if (!error || !*error)
        return "";
    const auto has = [error](const char *word) { return std::strstr(error, word) != nullptr; };
    if (has("OpenGL") || has("opengl") || has("RHI") || has("WGL") || has("GLX") || has("EGL")
        || has("graphics") || has("GPU"))
        return "Your graphics driver probably does not support OpenGL 3.3, or could not create a "
               "graphics context. Please update your graphics driver.";
    if (has("platform plugin"))
        return "Qt could not load its platform plugin. Dafin's files may be incomplete or damaged; "
               "please reinstall Dafin.";
    if (has("bad_alloc") || has("out of memory") || has("Out of memory"))
        return "Dafin ran out of memory.";
    return "";
}

} // namespace dafin
