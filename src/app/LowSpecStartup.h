#pragma once

// Dafin: startup side of low-spec (compatibility) mode, core/base/LowSpec.h. Desktop only.

#include <QString>

class QSplashScreen;

namespace dafin::lowspec {

// First thing in main(), before Dafin puts anything of its own into the environment
// (QT_SCALE_FACTOR, QT_NO_OPENGL_BUGLIST…): the environment a relaunch starts from.
void captureLaunchEnvironment();

// What main.cpp's OpenGL probe found.
struct GlFound
{
    bool haveGl = false;
    int major = 0;
    int minor = 0;
    QString renderer;
    // OpenGL 3.3 or newer, but no 3.3 core profile context: a driver quirk main.cpp reports
    // itself. Not treated as old hardware.
    bool coreProfileFailed = false;
};

// Decides once, after the OpenGL probe and before the playback engine or any window exists: sets
// drift::lowspec (active, software OpenGL, reason) and shows the "below recommended specs" notice
// (hiding `splash`, which may be null, while it is up). Returns true when this process started a
// copy of itself on software OpenGL instead; main() must then quit without drawing anything.
bool decideAtStartup(const GlFound &gl, QSplashScreen *splash);

// Total physical memory in bytes, 0 when unknown.
qint64 totalRamBytes();

} // namespace dafin::lowspec
