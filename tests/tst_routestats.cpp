// Dafin: route counters and GPU identity classification. No GPU, no Qt GUI.
//
// This is the "synthetic pure classification" half of C0. The other half (an NVIDIA and a
// non-NVIDIA machine each showing non-zero route counts and the right unsupported reason) needs
// real hardware and is NOT_RUN here.
#include "engine/diagnostics/RouteStats.h"

#include <QtTest>

#include <thread>
#include <vector>

using namespace drift::routes;

namespace {
DeviceId gpu(uint16_t vendor, const char *key = "", bool available = true)
{
    DeviceId d;
    d.available = available;
    d.vendorId = vendor;
    d.key = key;
    return d;
}
constexpr uint16_t kNvidia = 0x10de;
constexpr uint16_t kAmd = 0x1002;
constexpr uint16_t kIntel = 0x8086;
} // namespace

class TestRouteStats : public QObject
{
    Q_OBJECT

private slots:
    void init() { reset(); }

    void aStageThatDoesNotExistIsUnavailable()
    {
        QCOMPARE(classifyDeviceMatch(gpu(kNvidia, "a"), gpu(0, "", false)), Match::Unavailable);
        QCOMPARE(classifyDeviceMatch(gpu(0, "", false), gpu(kNvidia, "a")), Match::Unavailable);
    }

    void differentVendorsAreMismatched()
    {
        // The hybrid laptop: Intel draws, NVDEC decodes.
        QCOMPARE(classifyDeviceMatch(gpu(kIntel, "i"), gpu(kNvidia, "n")), Match::Mismatched);
        QCOMPARE(classifyDeviceMatch(gpu(kAmd), gpu(kNvidia)), Match::Mismatched);
    }

    void aVendorThatCouldNotBeNamedIsUnknown()
    {
        QCOMPARE(classifyDeviceMatch(gpu(0, "x"), gpu(kNvidia, "n")), Match::Unknown);
        QCOMPARE(classifyDeviceMatch(gpu(kNvidia, "n"), gpu(0, "x")), Match::Unknown);
    }

    void sameVendorWithoutAKeyIsNotAMatch()
    {
        // "NVIDIA is NVIDIA" would pass on a machine with two NVIDIA cards, or with one that is
        // not the card drawing. Without a key on both sides nothing is known.
        QCOMPARE(classifyDeviceMatch(gpu(kNvidia), gpu(kNvidia)), Match::Unknown);
        QCOMPARE(classifyDeviceMatch(gpu(kNvidia, "n"), gpu(kNvidia)), Match::Unknown);
        QCOMPARE(classifyDeviceMatch(gpu(kNvidia), gpu(kNvidia, "n")), Match::Unknown);
    }

    void sameVendorTwoCardsIsMismatched()
    {
        QCOMPARE(classifyDeviceMatch(gpu(kNvidia, "10de:2482@01:00.0"),
                                     gpu(kNvidia, "10de:25a2@02:00.0")),
                 Match::Mismatched);
    }

    void sameKeyIsMatched()
    {
        QCOMPARE(classifyDeviceMatch(gpu(kNvidia, "10de:2482@01:00.0"),
                                     gpu(kNvidia, "10de:2482@01:00.0")),
                 Match::Matched);
    }

    void countersCountEachRouteOnItsOwn()
    {
        countImport(ImportRoute::CudaInterop);
        countImport(ImportRoute::CudaInterop);
        countImport(ImportRoute::HwDownload);
        countEncode(EncodeRoute::GlPboHostNv12);
        noteFallback(Fallback::ExportCudaCopyFailed);
        notePlan(ExportPlan::HostPboNv12);

        const Snapshot s = snapshot();
        QCOMPARE(s.imports[int(ImportRoute::CudaInterop)], uint64_t(2));
        QCOMPARE(s.imports[int(ImportRoute::HwDownload)], uint64_t(1));
        QCOMPARE(s.imports[int(ImportRoute::SoftwareUpload)], uint64_t(0));
        QCOMPARE(s.encodes[int(EncodeRoute::GlPboHostNv12)], uint64_t(1));
        QCOMPARE(s.encodes[int(EncodeRoute::DirectCuda)], uint64_t(0));
        QCOMPARE(s.fallbacks[int(Fallback::ExportCudaCopyFailed)], uint64_t(1));
        QCOMPARE(int(s.plan), int(ExportPlan::HostPboNv12));
    }

    void countersSurviveManyThreads()
    {
        std::vector<std::thread> workers;
        for (int t = 0; t < 4; ++t)
            workers.emplace_back([] {
                for (int i = 0; i < 10000; ++i)
                    countImport(ImportRoute::SoftwareUpload);
            });
        for (auto &w : workers)
            w.join();
        QCOMPARE(snapshot().imports[int(ImportRoute::SoftwareUpload)], uint64_t(40000));
    }

    void resetClearsEverything()
    {
        countEncode(EncodeRoute::CpuSwscale);
        noteOrtProvider("cuda");
        reset();
        const Snapshot s = snapshot();
        QCOMPARE(s.encodes[int(EncodeRoute::CpuSwscale)], uint64_t(0));
        QVERIFY(s.ortProvider.empty());
    }

    void onlyAShortPlainProviderNameIsStored()
    {
        noteOrtProvider("cuda");
        QCOMPARE(QString::fromStdString(snapshot().ortProvider), QStringLiteral("cuda"));
        for (const char *bad : {"C:\\Users\\me\\models\\x.onnx", "/home/me/models/x.onnx",
                                "libonnxruntime_providers_cuda.so", "has space", "",
                                "averyveryveryverylongprovidernamethatisnotone"}) {
            noteOrtProvider(bad);
            QCOMPARE(QString::fromStdString(snapshot().ortProvider), QStringLiteral("other"));
        }
    }

    void everyIdIsAPlainLowercaseOrUpperTokenWithNoPathCharacters()
    {
        auto plain = [](const char *text) {
            const QString s = QString::fromLatin1(text);
            return !s.isEmpty() && !s.contains(QLatin1Char('/')) && !s.contains(QLatin1Char('\\'))
                   && !s.contains(QLatin1Char(' ')) && s != QLatin1String("unknown");
        };
        for (int i = 0; i < kImportRoutes; ++i)
            QVERIFY(plain(id(ImportRoute(i))));
        for (int i = 0; i < kEncodeRoutes; ++i)
            QVERIFY(plain(id(EncodeRoute(i))));
        for (int i = 0; i < kFallbacks; ++i)
            QVERIFY(plain(id(Fallback(i))));
        for (int i = 0; i < int(ExportPlan::Count_); ++i)
            QVERIFY(plain(id(ExportPlan(i))));
    }
};

QTEST_APPLESS_MAIN(TestRouteStats)
#include "tst_routestats.moc"
