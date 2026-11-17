#include "app/LowSpecStartup.h"

// Desktop only: main.cpp calls it only there, and WebAssembly has no QProcess.
#if !defined(Q_OS_ANDROID) && !defined(Q_OS_WASM)

#include "app/StartupGuard.h"
#include "core/base/LowSpec.h"
#include "engine/gpu/device/GpuStatus.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QLocale>
#include <QMessageBox>
#include <QProcess>
#include <QProcessEnvironment>
#include <QSettings>
#include <QSplashScreen>
#include <QStandardPaths>
#include <QStringList>

#if defined(Q_OS_WIN)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#elif defined(Q_OS_MACOS)
#include <sys/sysctl.h>
#include <sys/types.h>
#elif defined(Q_OS_UNIX)
#include <unistd.h>
#endif

namespace dafin::lowspec {

namespace ls = drift::lowspec;

namespace {

QProcessEnvironment &launchEnvironment()
{
    static QProcessEnvironment env;
    return env;
}
bool g_launchEnvironmentCaptured = false;

QString trLs(const char *text)
{
    return QCoreApplication::translate("LowSpec", text);
}

// Where a software OpenGL can come from. Windows: the Mesa llvmpipe the package puts beside the
// exe as opengl32sw.dll (Qt loads it with QT_OPENGL=software; .github/workflows/package.yml).
// Linux: the system Mesa, which every distribution ships with llvmpipe.
bool softwareGlAvailable()
{
#if defined(Q_OS_WIN)
    return QFileInfo::exists(QCoreApplication::applicationDirPath()
                             + QStringLiteral("/opengl32sw.dll"));
#elif defined(Q_OS_UNIX) && !defined(Q_OS_MACOS)
    return true;
#else
    return false;
#endif
}

// "3.2" → (3, 2); "none" → no GL. False when the variable is missing or unreadable.
bool parseOriginalGl(const QString &value, bool *hadGl, int *major, int *minor)
{
    if (value.isEmpty())
        return false;
    if (value == QLatin1String("none")) {
        *hadGl = false;
        return true;
    }
    const QStringList parts = value.split(QLatin1Char('.'));
    bool okMajor = false;
    bool okMinor = false;
    if (parts.size() != 2)
        return false;
    *major = parts.at(0).toInt(&okMajor);
    *minor = parts.at(1).toInt(&okMinor);
    *hadGl = true;
    return okMajor && okMinor;
}

// Starts this program again on software OpenGL, with the environment main() started with.
bool relaunchOnSoftwareGl(const GlFound &gl)
{
    QProcessEnvironment env = g_launchEnvironmentCaptured ? launchEnvironment()
                                                          : QProcessEnvironment::systemEnvironment();
    // Inside an AppImage the binary lives in a mount that goes away with this process; relaunch
    // the image itself with the runtime's variables cleared (as PreferencesController::restartApp).
    const QString appImage = qEnvironmentVariable("APPIMAGE");
    if (!appImage.isEmpty()) {
        for (const char *name : {"APPIMAGE", "APPDIR", "ARGV0", "OWD", "LD_LIBRARY_PATH",
                                 "QT_PLUGIN_PATH", "QML2_IMPORT_PATH", "QML_IMPORT_PATH",
                                 "QT_QPA_PLATFORM_PLUGIN_PATH"})
            env.remove(QString::fromLatin1(name));
    }
    env.insert(QString::fromLatin1(ls::kSoftwareGlEnv), QStringLiteral("1"));
    env.insert(QString::fromLatin1(ls::kOriginalGlEnv),
               gl.haveGl ? QStringLiteral("%1.%2").arg(gl.major).arg(gl.minor) : QStringLiteral("none"));
    const auto setIfEmpty = [&env](const char *name, const char *value) {
        if (env.value(QString::fromLatin1(name)).isEmpty())
            env.insert(QString::fromLatin1(name), QString::fromLatin1(value));
    };
#if defined(Q_OS_WIN)
    // Qt then loads opengl32sw.dll instead of the driver's opengl32.dll.
    env.insert(QStringLiteral("QT_OPENGL"), QStringLiteral("software"));
#else
    // Mesa's own switch, plus glvnd's: a proprietary driver would otherwise still be picked.
    setIfEmpty("LIBGL_ALWAYS_SOFTWARE", "1");
    setIfEmpty("__GLX_VENDOR_LIBRARY_NAME", "mesa");
#endif
    // llvmpipe (OpenGL 4.5), not softpipe or a driver layered on the same old GPU.
    setIfEmpty("GALLIUM_DRIVER", "llvmpipe");

    QProcess process;
    process.setProgram(appImage.isEmpty() ? QCoreApplication::applicationFilePath() : appImage);
    process.setArguments(QCoreApplication::arguments().mid(1));
    process.setWorkingDirectory(QDir::currentPath());
    process.setProcessEnvironment(env);
    if (!process.startDetached()) {
        qWarning("Low-spec: could not relaunch on software OpenGL (%s)",
                 qUtf8Printable(process.errorString()));
        return false;
    }
    qWarning("Low-spec: OpenGL %s; relaunched on software OpenGL",
             gl.haveGl ? qUtf8Printable(QStringLiteral("%1.%2").arg(gl.major).arg(gl.minor)) : "unavailable");
    return true;
}

QString formatReasons(const ls::Decision &d, const ls::Probe &p, const GlFound &gl)
{
    QStringList parts;
    if (d.reasons & ls::GlTooLow) {
        if (p.forceGl)
            parts << trLs("OpenGL below 3.3 simulated (DAFIN_FORCE_WEAK=gl)");
        else if (p.onSoftwareGl && p.originalGlKnown && p.originalHadGl)
            parts << trLs("OpenGL %1.%2 detected, 3.3 needed").arg(p.originalMajor).arg(p.originalMinor);
        else
            parts << trLs("OpenGL %1.%2 detected, 3.3 needed").arg(gl.major).arg(gl.minor);
    }
    if (d.reasons & ls::NoGl)
        parts << trLs("No usable OpenGL driver detected");
    if (d.reasons & ls::SoftwareRenderer) {
        const QString name = gl.renderer.isEmpty() ? trLs("software OpenGL") : gl.renderer;
        parts << trLs("Drawing with the processor instead of the graphics card (%1)").arg(name);
    }
    if (d.reasons & ls::LowRam) {
        const double gb = double(p.ramBytes) / (1024.0 * 1024.0 * 1024.0);
        parts << trLs("%1 GB of RAM detected, 5 GB recommended").arg(QLocale().toString(gb, 'f', 1));
    }
    if ((d.reasons & ls::Forced) && !p.forceGl)
        parts << trLs("Simulated with DAFIN_FORCE_WEAK (test switch)");
    return parts.join(QStringLiteral("; "));
}

void showNotice(QSplashScreen *splash)
{
    const bool splashWasVisible = splash && splash->isVisible();
    if (splashWasVisible)
        splash->hide();
    QMessageBox box(QMessageBox::Warning, trLs("Dafin – compatibility mode"),
                    trLs("Your computer is below Dafin's recommended specs. Dafin is running in a "
                         "compatibility mode. Many features are disabled or may be slow. Basic "
                         "editing and rendering still work."),
                    QMessageBox::Ok);
    box.setInformativeText(trLs("Detected: %1").arg(QString::fromStdString(ls::reasonText())));
    box.exec();
    if (splashWasVisible)
        splash->show();
}

} // namespace

void captureLaunchEnvironment()
{
    launchEnvironment() = QProcessEnvironment::systemEnvironment();
    g_launchEnvironmentCaptured = true;
}

qint64 totalRamBytes()
{
#if defined(Q_OS_WIN)
    MEMORYSTATUSEX status;
    status.dwLength = sizeof(status);
    if (GlobalMemoryStatusEx(&status))
        return qint64(status.ullTotalPhys);
#elif defined(Q_OS_MACOS)
    int64_t bytes = 0;
    size_t size = sizeof(bytes);
    if (sysctlbyname("hw.memsize", &bytes, &size, nullptr, 0) == 0)
        return bytes;
#elif defined(Q_OS_UNIX)
    const long pages = sysconf(_SC_PHYS_PAGES);
    const long pageSize = sysconf(_SC_PAGE_SIZE);
    if (pages > 0 && pageSize > 0)
        return qint64(pages) * pageSize;
#endif
    return 0;
}

bool decideAtStartup(const GlFound &gl, QSplashScreen *splash)
{
    ls::Probe probe;
    probe.haveGl = gl.haveGl;
    probe.glMajor = gl.major;
    probe.glMinor = gl.minor;
    probe.softwareRenderer = gl.haveGl && drift::gl::isSoftwareRenderer(gl.renderer);
    probe.ramBytes = totalRamBytes();
    const QString force = qEnvironmentVariable(ls::kForceEnv).trimmed().toLower();
    probe.forced = !force.isEmpty() && force != QLatin1String("0");
    probe.forceGl = force == QLatin1String("gl");
    probe.onSoftwareGl = !qEnvironmentVariableIsEmpty(ls::kSoftwareGlEnv);
    probe.originalGlKnown = probe.onSoftwareGl
        && parseOriginalGl(qEnvironmentVariable(ls::kOriginalGlEnv), &probe.originalHadGl,
                           &probe.originalMajor, &probe.originalMinor);

    const ls::Setting setting = ls::parseSetting(
        QSettings().value(QLatin1String(ls::kSettingKey), QStringLiteral("auto")).toString().toStdString());
    // --check-qml and the QML previews run unattended (CI, under llvmpipe): no relaunch, no dialog.
    // Nor for a driver that has 3.3 but no core profile: main.cpp's own message covers that.
    const bool unattended = QStandardPaths::isTestModeEnabled() || !splash;

    ls::Decision decision = ls::decide(probe, setting, softwareGlAvailable());
    if (unattended || gl.coreProfileFailed) {
        decision.relaunchOnSoftwareGl = false;
        decision.showNotice = false;
    }

    if (decision.relaunchOnSoftwareGl && relaunchOnSoftwareGl(gl)) {
        drift::startup::cancelThisLaunch();
        return true;
    }

    ls::setActive(decision.active);
    ls::setOnSoftwareGl(probe.onSoftwareGl);
    if (decision.weakHardware)
        ls::reasonText() = formatReasons(decision, probe, gl).toStdString();
    if (decision.active) {
        qWarning("Low-spec mode is on (%s)",
                 decision.weakHardware ? ls::reasonText().c_str() : "turned on in Settings");
    }
    if (decision.showNotice)
        showNotice(splash);
    return false;
}

} // namespace dafin::lowspec

#endif // !Q_OS_ANDROID && !Q_OS_WASM
