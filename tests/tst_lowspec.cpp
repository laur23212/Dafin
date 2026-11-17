// Plain C++ (no Qt): when Dafin treats a computer as weak, relaunches on software OpenGL and turns
// on low-spec mode (core/base/LowSpec.h).
#include "core/base/LowSpec.h"

#include <cstdio>

using namespace drift::lowspec;

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)

static Probe goodMachine()
{
    Probe p;
    p.haveGl = true;
    p.glMajor = 4;
    p.glMinor = 6;
    p.ramBytes = 16LL * 1024 * 1024 * 1024;
    return p;
}

int main()
{
    // A normal computer: nothing changes.
    {
        const Decision d = decide(goodMachine(), Setting::Auto, true);
        CHECK(!d.weakHardware);
        CHECK(!d.active);
        CHECK(!d.relaunchOnSoftwareGl);
        CHECK(!d.showNotice);
        CHECK(d.reasons == NoReason);
    }
    // Exactly 3.3 is enough; unknown RAM never counts as weak.
    {
        Probe p = goodMachine();
        p.glMajor = 3;
        p.glMinor = 3;
        p.ramBytes = 0;
        CHECK(!decide(p, Setting::Auto, true).weakHardware);
    }
    // OpenGL 3.2: relaunch on software GL, no notice yet (the relaunched process shows it).
    {
        Probe p = goodMachine();
        p.glMajor = 3;
        p.glMinor = 2;
        const Decision d = decide(p, Setting::Auto, true);
        CHECK(d.reasons & GlTooLow);
        CHECK(d.relaunchOnSoftwareGl);
        CHECK(!d.showNotice);
        CHECK(d.active);
    }
    // ...the relaunched process: llvmpipe 4.5, reports the original 3.2, never relaunches again.
    {
        Probe p = goodMachine();
        p.glMajor = 4;
        p.glMinor = 5;
        p.softwareRenderer = true;
        p.onSoftwareGl = true;
        p.originalGlKnown = true;
        p.originalHadGl = true;
        p.originalMajor = 3;
        p.originalMinor = 2;
        const Decision d = decide(p, Setting::Auto, true);
        CHECK(d.reasons & GlTooLow);
        CHECK(d.reasons & SoftwareRenderer);
        CHECK(!d.relaunchOnSoftwareGl);
        CHECK(d.active);
        CHECK(d.showNotice);
    }
    // Software GL that still fails in the relaunch: no loop, no notice (the error box explains).
    {
        Probe p;
        p.onSoftwareGl = true;
        const Decision d = decide(p, Setting::Auto, true);
        CHECK(!d.relaunchOnSoftwareGl);
        CHECK(!d.showNotice);
        CHECK(d.reasons & NoGl);
    }
    // 3.2 with no software GL to switch to (Windows arm64): no relaunch, the old error instead.
    {
        Probe p = goodMachine();
        p.glMajor = 3;
        p.glMinor = 2;
        const Decision d = decide(p, Setting::Auto, false);
        CHECK(!d.relaunchOnSoftwareGl);
        CHECK(!d.showNotice);
    }
    // No OpenGL at all: also tries software GL.
    {
        Probe p;
        p.ramBytes = 8LL * 1024 * 1024 * 1024;
        CHECK(decide(p, Setting::Auto, true).relaunchOnSoftwareGl);
    }
    // 4 GB of RAM (reported as ~3.8 GiB) is weak; 6 GB and 8 GB are not.
    {
        Probe p = goodMachine();
        p.ramBytes = 3900LL * 1024 * 1024;
        const Decision d = decide(p, Setting::Auto, true);
        CHECK(d.reasons == LowRam);
        CHECK(d.active && d.showNotice && !d.relaunchOnSoftwareGl);
        p.ramBytes = 5800LL * 1024 * 1024;
        CHECK(!decide(p, Setting::Auto, true).weakHardware);
        p.ramBytes = 7700LL * 1024 * 1024;
        CHECK(!decide(p, Setting::Auto, true).weakHardware);
    }
    // A software rasterizer already drawing (a VM, or Qt fell back by itself) is weak, no relaunch.
    {
        Probe p = goodMachine();
        p.softwareRenderer = true;
        const Decision d = decide(p, Setting::Auto, true);
        CHECK(d.reasons == SoftwareRenderer);
        CHECK(!d.relaunchOnSoftwareGl && d.showNotice);
    }
    // The setting: "off" keeps every feature on weak hardware (the GL switch still happens),
    // "on" turns the limits on for a strong machine without the notice.
    {
        Probe weak = goodMachine();
        weak.ramBytes = 3LL * 1024 * 1024 * 1024;
        CHECK(!decide(weak, Setting::Off, true).active);
        CHECK(!decide(weak, Setting::Off, true).showNotice);
        Probe old = goodMachine();
        old.glMinor = 2;
        old.glMajor = 3;
        CHECK(decide(old, Setting::Off, true).relaunchOnSoftwareGl);
        const Decision on = decide(goodMachine(), Setting::On, true);
        CHECK(on.active && !on.showNotice && !on.weakHardware);
    }
    // DAFIN_FORCE_WEAK=1 on a strong machine: low-spec mode and the notice, no GL switch, even
    // with the setting off.
    {
        Probe p = goodMachine();
        p.forced = true;
        const Decision d = decide(p, Setting::Off, true);
        CHECK(d.reasons == Forced);
        CHECK(d.active && d.showNotice && !d.relaunchOnSoftwareGl);
    }
    // DAFIN_FORCE_WEAK=gl: relaunches on software GL once, then shows the notice there.
    {
        Probe p = goodMachine();
        p.forced = true;
        p.forceGl = true;
        Decision d = decide(p, Setting::Auto, true);
        CHECK(d.relaunchOnSoftwareGl && !d.showNotice);
        p.onSoftwareGl = true;
        p.softwareRenderer = true;
        p.glMajor = 4;
        p.glMinor = 5;
        d = decide(p, Setting::Auto, true);
        CHECK(!d.relaunchOnSoftwareGl && d.showNotice && d.active);
    }
    CHECK(parseSetting("on") == Setting::On);
    CHECK(parseSetting("off") == Setting::Off);
    CHECK(parseSetting("auto") == Setting::Auto);
    CHECK(parseSetting("garbage") == Setting::Auto);
    CHECK(!active());
    setActive(true);
    CHECK(active());
    setActive(false);

    if (failures == 0)
        std::printf("tst_lowspec: all passed\n");
    return failures == 0 ? 0 : 1;
}
