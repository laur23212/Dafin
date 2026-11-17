#pragma once

#include <atomic>
#include <string>

// Dafin: low-spec (compatibility) mode for weak computers. No Qt, so the rule is tested on its own
// (tests/tst_lowspec.cpp) and any layer can ask. Decided once at startup (app/LowSpecStartup.cpp)
// and never changed after; nothing here writes a user setting.
//
// Weak hardware is any of: OpenGL below 3.3 (or none), a software rasterizer already drawing, or
// under kWeakRamBytes of RAM. Below 3.3 the GPU compositor cannot run at all, so Dafin relaunches
// itself once on a software OpenGL (Mesa llvmpipe, OpenGL 4.5 on the CPU) and runs slowly instead
// of not at all. On a machine that is not weak, nothing changes: the probe is the one main.cpp
// already did, and every check made later is a relaxed atomic load outside any per-frame loop.
namespace drift::lowspec {

// Settings key (QSettings): "auto" (default), "on" or "off". Read at startup only.
inline constexpr const char *kSettingKey = "performance/lowSpecMode";

// Environment. DAFIN_FORCE_WEAK=1 simulates a weak computer (low-spec mode and the notice);
// DAFIN_FORCE_WEAK=gl also simulates OpenGL below 3.3, so the software-OpenGL relaunch runs too.
inline constexpr const char *kForceEnv = "DAFIN_FORCE_WEAK";
// Set by the relaunch: this process is already on software OpenGL (and must never relaunch again).
inline constexpr const char *kSoftwareGlEnv = "DAFIN_SOFTWARE_GL";
// Set by the relaunch: the OpenGL the first process found, "3.2", or "none" when it had none.
inline constexpr const char *kOriginalGlEnv = "DAFIN_ORIGINAL_GL";

// Under 5 GiB counts as weak. A "4 GB" machine reports 3.4–3.9 GiB (the iGPU and firmware keep
// some), an "8 GB" one 7.5 or more; 6 GB machines are left alone.
inline constexpr long long kWeakRamBytes = 5LL * 1024 * 1024 * 1024;

enum class Setting { Auto, On, Off };

inline Setting parseSetting(const std::string &value)
{
    if (value == "on")
        return Setting::On;
    if (value == "off")
        return Setting::Off;
    return Setting::Auto;
}

inline bool glAtLeast33(int major, int minor)
{
    return major > 3 || (major == 3 && minor >= 3);
}

// What the startup probe found. A zero ramBytes means unknown and never counts as weak.
struct Probe
{
    bool haveGl = false;
    int glMajor = 0;
    int glMinor = 0;
    bool softwareRenderer = false; // GL_RENDERER is llvmpipe, softpipe, GDI Generic…
    long long ramBytes = 0;
    bool forced = false;     // DAFIN_FORCE_WEAK set
    bool forceGl = false;    // DAFIN_FORCE_WEAK=gl
    bool onSoftwareGl = false; // DAFIN_SOFTWARE_GL set: this is the relaunched process
    bool originalGlKnown = false; // DAFIN_ORIGINAL_GL carried a version
    bool originalHadGl = false;
    int originalMajor = 0;
    int originalMinor = 0;
};

// Why the computer counts as weak. A bit set, so the notice can list every reason.
enum Reason : unsigned {
    NoReason = 0,
    GlTooLow = 1u << 0,      // OpenGL below 3.3 (here, or in the process that relaunched us)
    NoGl = 1u << 1,          // no OpenGL context at all
    SoftwareRenderer = 1u << 2,
    LowRam = 1u << 3,
    Forced = 1u << 4,        // DAFIN_FORCE_WEAK
};

struct Decision
{
    unsigned reasons = NoReason;
    bool weakHardware = false;   // any reason
    bool active = false;         // low-spec mode on for this run
    bool relaunchOnSoftwareGl = false;
    bool showNotice = false;     // the "below recommended specs" dialog
};

// `softwareGlAvailable`: whether a software OpenGL can be switched to (the bundled Mesa on
// Windows, the system Mesa on Linux).
inline Decision decide(const Probe &p, Setting setting, bool softwareGlAvailable)
{
    Decision d;
    const bool glOk = p.haveGl && glAtLeast33(p.glMajor, p.glMinor);
    if (!p.haveGl)
        d.reasons |= NoGl;
    else if (!glOk)
        d.reasons |= GlTooLow;
    // The relaunched process sees llvmpipe's 4.5; what made it relaunch is what to report.
    if (p.onSoftwareGl && p.originalGlKnown)
        d.reasons |= p.originalHadGl ? GlTooLow : NoGl;
    if (p.haveGl && p.softwareRenderer)
        d.reasons |= SoftwareRenderer;
    if (p.ramBytes > 0 && p.ramBytes < kWeakRamBytes)
        d.reasons |= LowRam;
    if (p.forced)
        d.reasons |= Forced;
    if (p.forceGl && !p.onSoftwareGl)
        d.reasons |= GlTooLow;

    d.weakHardware = d.reasons != NoReason;

    // Once only: a relaunched process never relaunches again, whatever it finds.
    const bool glUnusable = !glOk || (p.forceGl && !p.onSoftwareGl);
    d.relaunchOnSoftwareGl = glUnusable && !p.onSoftwareGl && softwareGlAvailable;

    // The compatibility path itself is never optional (without it there is no picture), but the
    // feature limits are: "off" keeps every feature even on weak hardware, "on" forces them. The
    // test switch wins over both, so it always shows what a weak computer gets.
    d.active = p.forced || setting == Setting::On || (setting == Setting::Auto && d.weakHardware);
    // Not when OpenGL is still unusable here: main.cpp's "driver is too old" error says more.
    d.showNotice = d.weakHardware && d.active && !d.relaunchOnSoftwareGl && glOk;
    return d;
}

inline std::atomic<bool> &flag()
{
    static std::atomic<bool> value{false};
    return value;
}

// Low-spec mode is on for this run.
inline bool active() { return flag().load(std::memory_order_relaxed); }
inline void setActive(bool on) { flag().store(on, std::memory_order_relaxed); }

// This run draws through a software OpenGL that Dafin switched to.
inline std::atomic<bool> &softwareGlFlag()
{
    static std::atomic<bool> value{false};
    return value;
}
inline bool onSoftwareGl() { return softwareGlFlag().load(std::memory_order_relaxed); }
inline void setOnSoftwareGl(bool on) { softwareGlFlag().store(on, std::memory_order_relaxed); }

// The detected reason, one line, UTF-8, already translated (set by app/LowSpecStartup.cpp).
inline std::string &reasonText()
{
    static std::string text;
    return text;
}

} // namespace drift::lowspec
