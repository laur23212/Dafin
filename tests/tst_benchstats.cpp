// Dafin: the numbers and go/stop rules behind `dafin --bench` (C1), and its phase timers.
// No GPU, no Qt GUI. The benchmark runs themselves need an NVIDIA card and are NOT_RUN here.
#include "engine/diagnostics/BenchPhases.h"
#include "engine/diagnostics/BenchStats.h"

#include <QtTest>

using namespace drift::bench;

namespace {

ExportRun nvencRun(double wallS, double hostCopyS, double sendS)
{
    ExportRun r;
    r.wallS = wallS;
    r.nvenc = true;
    r.frames = 600;
    r.hostNv12 = 600;
    r.hostCopyS = hostCopyS;
    r.sendS = sendS;
    return r;
}

PreviewRun previewRun(double frameMs, double syncMs, double seekMs, double seekDecodeMs)
{
    PreviewRun p;
    p.budgetMs = 1000.0 / 30.0;
    p.cudaImports = 300;
    p.nvdecFrames = 300;
    for (int i = 0; i < 300; ++i) {
        p.frameMs.push_back(frameMs);
        p.frameSyncMs.push_back(syncMs);
    }
    for (int i = 0; i < 60; ++i) {
        p.seekMs.push_back(seekMs);
        p.seekDecodeMs.push_back(seekDecodeMs);
    }
    return p;
}

} // namespace

class TestBenchStats : public QObject
{
    Q_OBJECT

private slots:
    void medianAndPercentile()
    {
        QCOMPARE(median({3, 1, 2}), 2.0);
        QCOMPARE(median({1, 2, 3, 4}), 2.5);
        QCOMPARE(median({}), 0.0);
        // Nearest rank: with five runs p95 is the slowest one, which the report says.
        QCOMPARE(percentile({5, 1, 4, 2, 3}, 95), 5.0);
        std::vector<double> hundred;
        for (int i = 1; i <= 100; ++i)
            hundred.push_back(i);
        QCOMPARE(percentile(hundred, 95), 95.0);
        QCOMPARE(percentile(hundred, 50), 50.0);
        QCOMPARE(percentile({}, 95), 0.0);
    }

    void exportCeilingDecides()
    {
        // 2 s copy + 5 s send out of 100 s: at most 7 % to gain, under the 10 % bar.
        Decision d = decideExport(std::vector<ExportRun>(5, nvencRun(100, 2, 5)));
        QCOMPARE(d.verdict, Verdict::Stop);
        QVERIFY(qAbs(d.value - 0.07) < 1e-9);

        d = decideExport(std::vector<ExportRun>(5, nvencRun(100, 2, 20)));
        QCOMPARE(d.verdict, Verdict::Go);

        // Median over runs, so one odd run does not flip it.
        std::vector<ExportRun> mixed(4, nvencRun(100, 2, 5));
        mixed.push_back(nvencRun(100, 2, 40));
        QCOMPARE(decideExport(mixed).verdict, Verdict::Stop);
    }

    void exportNeedsNvencAndARoute()
    {
        ExportRun cpu = nvencRun(100, 2, 50);
        cpu.nvenc = false;
        QCOMPARE(decideExport({cpu}).verdict, Verdict::Unsupported);

        ExportRun unproven = nvencRun(100, 2, 50);
        unproven.hostNv12 = 0;
        QCOMPARE(decideExport({unproven}).verdict, Verdict::Inconclusive);
        QCOMPARE(decideExport({}).verdict, Verdict::Inconclusive);
    }

    void previewSyncDecides()
    {
        // Within budget: nothing to notice, whatever the sync costs.
        QCOMPARE(decidePreviewSync({previewRun(20, 8, 60, 50)}).verdict, Verdict::Stop);
        // Over budget and a quarter of it is the sync.
        QCOMPARE(decidePreviewSync({previewRun(40, 10, 60, 50)}).verdict, Verdict::Go);
        // Over budget but the sync is not why: 39 ms without it.
        QCOMPARE(decidePreviewSync({previewRun(40, 1, 60, 50)}).verdict, Verdict::Stop);

        PreviewRun noInterop = previewRun(40, 10, 60, 50);
        noInterop.cudaImports = 0;
        QCOMPARE(decidePreviewSync({noInterop}).verdict, Verdict::Unsupported);
    }

    void scrubDecides()
    {
        QCOMPARE(decideScrub({previewRun(20, 1, 60, 50)}).verdict, Verdict::Stop);
        QCOMPARE(decideScrub({previewRun(20, 1, 200, 150)}).verdict, Verdict::Go);
        QCOMPARE(decideScrub({previewRun(20, 1, 200, 40)}).verdict, Verdict::Stop);

        PreviewRun software = previewRun(20, 1, 200, 150);
        software.nvdecFrames = 0;
        QCOMPARE(decideScrub({software}).verdict, Verdict::Unsupported);
    }

    void phaseTimersAreOptIn()
    {
        reset();
        setEnabled(false);
        {
            const Scope s(Phase::ExportSend);
        }
        QCOMPARE(snapshot().phases[int(Phase::ExportSend)].count, uint64_t(0));

        setEnabled(true);
        {
            const Scope s(Phase::ExportSend);
        }
        Stopwatch w(Phase::ExportCompose);
        w.stop();
        w.stop(); // once only
        const Snapshot snap = snapshot();
        QCOMPARE(snap.phases[int(Phase::ExportSend)].count, uint64_t(1));
        QCOMPARE(snap.phases[int(Phase::ExportCompose)].count, uint64_t(1));
        QVERIFY(snap.phases[int(Phase::ExportSend)].maxNs <= snap.phases[int(Phase::ExportSend)].ns);
        setEnabled(false);
        reset();
        QCOMPARE(snapshot().phases[int(Phase::ExportSend)].count, uint64_t(0));
        QCOMPARE(QString::fromLatin1(id(Phase::PreviewCudaSync)), QStringLiteral("preview_cuda_sync"));
    }
};

QTEST_APPLESS_MAIN(TestBenchStats)
#include "tst_benchstats.moc"
