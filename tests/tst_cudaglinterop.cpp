// Dafin: CUDA <-> GL interop error handling, with a simulated driver. No GPU.
//
// Every call the interop layer makes is failed in turn (and the second NV12 plane, and an unmap
// that fails twice) and the simulated driver is checked afterwards: nothing registered, nothing
// mapped, no context left pushed, every queued copy waited for before its arrays went away.
// What this cannot show is how a real NVIDIA driver behaves; that needs hardware (NOT_RUN).
#include "engine/gpu/CudaGlInterop.h"

#include <QtTest>

#include <string>
#include <vector>

using namespace drift::cudagl;

namespace {

struct Fake
{
    int failFrom[kCalls] = {};  // 1-based occurrence of the call that starts failing, 0 = never
    int failCount[kCalls] = {}; // how many consecutive occurrences fail
    int calls[kCalls] = {};

    int registered = 0;
    int mapped = 0;
    int ctxDepth = 0;
    int inFlight = 0;      // copies queued and not yet waited for
    int copiesQueued = 0;
    int maxCtxDepth = 0;
    bool unregisteredWhileMapped = false;
    bool unregisteredWithCopyInFlight = false;
    bool unmappedWithCopyInFlightAfterFailure = false;
    bool copyFailureSeen = false;
    std::vector<std::string> log;
    long nextHandle = 0x1000;
    Memcpy2D lastCopy;
};
Fake g;

bool hit(Call c)
{
    const int n = ++g.calls[int(c)];
    return g.failFrom[int(c)] > 0 && n >= g.failFrom[int(c)]
        && n < g.failFrom[int(c)] + g.failCount[int(c)];
}

constexpr int kFail = 700;

CUresult fakePush(CUcontext)
{
    g.log.push_back("push");
    if (hit(Call::ContextPush))
        return kFail;
    g.maxCtxDepth = std::max(g.maxCtxDepth, ++g.ctxDepth);
    return kSuccess;
}
CUresult fakePop(CUcontext *)
{
    g.log.push_back("pop");
    if (hit(Call::ContextPop)) {
        // A refused pop leaves the context on the stack: that is what the counter is for.
        return kFail;
    }
    --g.ctxDepth;
    return kSuccess;
}
CUresult fakeRegister(CUgraphicsResource *out, unsigned, unsigned, unsigned)
{
    g.log.push_back("register");
    if (hit(Call::Register))
        return kFail;
    ++g.registered;
    g.nextHandle += 0x10;
    *out = reinterpret_cast<void *>(g.nextHandle);
    return kSuccess;
}
CUresult fakeUnregister(CUgraphicsResource)
{
    g.log.push_back("unregister");
    if (g.mapped > 0)
        g.unregisteredWhileMapped = true;
    if (g.inFlight > 0)
        g.unregisteredWithCopyInFlight = true;
    if (hit(Call::Unregister))
        return kFail;
    --g.registered;
    return kSuccess;
}
CUresult fakeMap(unsigned n, CUgraphicsResource *, CUstream)
{
    g.log.push_back("map");
    if (hit(Call::Map))
        return kFail;
    g.mapped += int(n);
    return kSuccess;
}
CUresult fakeUnmap(unsigned n, CUgraphicsResource *, CUstream)
{
    g.log.push_back("unmap");
    if (g.copyFailureSeen && g.inFlight > 0)
        g.unmappedWithCopyInFlightAfterFailure = true;
    if (hit(Call::Unmap))
        return kFail;
    g.mapped -= int(n);
    return kSuccess;
}
CUresult fakeArray(CUarray *out, CUgraphicsResource, unsigned, unsigned)
{
    g.log.push_back("array");
    if (hit(Call::GetMappedArray))
        return kFail;
    *out = reinterpret_cast<void *>(0x9000 + g.calls[int(Call::GetMappedArray)]);
    return kSuccess;
}
CUresult fakeCopy(const Memcpy2D *op, CUstream)
{
    g.log.push_back("copy");
    if (hit(Call::Copy)) {
        g.copyFailureSeen = true;
        return kFail;
    }
    g.lastCopy = *op;
    ++g.copiesQueued;
    ++g.inFlight;
    return kSuccess;
}
CUresult fakeSync(CUstream)
{
    g.log.push_back("sync");
    if (hit(Call::StreamSync))
        return kFail;
    g.inFlight = 0;
    return kSuccess;
}

DriverApi api()
{
    DriverApi a;
    a.cuInit = nullptr;
    a.cuCtxPushCurrent = fakePush;
    a.cuCtxPopCurrent = fakePop;
    a.cuGraphicsGLRegisterImage = fakeRegister;
    a.cuGraphicsUnregisterResource = fakeUnregister;
    a.cuGraphicsMapResources = fakeMap;
    a.cuGraphicsUnmapResources = fakeUnmap;
    a.cuGraphicsSubResourceGetMappedArray = fakeArray;
    a.cuMemcpy2DAsync = fakeCopy;
    a.cuStreamSynchronize = fakeSync;
    a.ok = true;
    return a;
}

void inject(Call c, int from = 1, int count = 1)
{
    g.failFrom[int(c)] = from;
    g.failCount[int(c)] = count;
}

int indexOf(const std::string &what, int from = 0)
{
    for (int i = from; i < int(g.log.size()); ++i)
        if (g.log[size_t(i)] == what)
            return i;
    return -1;
}

int lastIndexOf(const std::string &what)
{
    for (int i = int(g.log.size()) - 1; i >= 0; --i)
        if (g.log[size_t(i)] == what)
            return i;
    return -1;
}

const Plane kY{reinterpret_cast<void *>(0x100000), 1920};
const Plane kUv{reinterpret_cast<void *>(0x200000), 1920};
CUstream stream() { return reinterpret_cast<void *>(0x77); }

} // namespace

class TestCudaGlInterop : public QObject
{
    Q_OBJECT

private slots:
    void init()
    {
        g = Fake{};
        resetCounters();
    }

    void successCopiesBothPlanesBothWays()
    {
        const DriverApi a = api();
        RegisteredPair pair;
        QVERIFY(!registerPair(a, 1, 2, 0x0DE1, kRegisterWriteDiscard, &pair).failed);
        QVERIFY(pair.valid());
        QCOMPARE(g.registered, 2);

        for (Direction d : {Direction::DeviceToTexture, Direction::TextureToDevice}) {
            g.copiesQueued = 0;
            const CopyResult r = copyNv12Planes(a, stream(), pair, d, kY, kUv, 1920, 1080);
            QVERIFY(r.ok);
            QVERIFY(!r.failure.failed);
            QCOMPARE(g.copiesQueued, 2);
            QCOMPARE(g.mapped, 0);
            QCOMPARE(g.inFlight, 0);
            // The UV plane is full width in bytes over half the rows.
            QCOMPARE(g.lastCopy.WidthInBytes, size_t(1920));
            QCOMPARE(g.lastCopy.Height, size_t(540));
            if (d == Direction::DeviceToTexture) {
                QCOMPARE(g.lastCopy.srcMemoryType, int(kMemoryDevice));
                QCOMPARE(g.lastCopy.dstMemoryType, int(kMemoryArray));
            } else {
                QCOMPARE(g.lastCopy.srcMemoryType, int(kMemoryArray));
                QCOMPARE(g.lastCopy.dstMemoryType, int(kMemoryDevice));
            }
        }
        QVERIFY(!unregisterPair(a, &pair).failed);
        QCOMPARE(g.registered, 0);
        const CounterSnapshot c = counters();
        for (int i = 0; i < kCalls; ++i)
            QCOMPARE(c.failures[i], uint64_t(0));
    }

    // -- registration ----------------------------------------------------------------------

    void failedFirstRegistrationLeavesNothing()
    {
        const DriverApi a = api();
        inject(Call::Register, 1);
        RegisteredPair pair;
        const Failure f = registerPair(a, 1, 2, 0x0DE1, kRegisterReadOnly, &pair);
        QVERIFY(f.failed);
        QCOMPARE(f.call, Call::Register);
        QCOMPARE(f.rc, kFail);
        QVERIFY(!pair.valid());
        QCOMPARE(g.registered, 0);
    }

    void failedSecondRegistrationUnregistersTheFirst()
    {
        const DriverApi a = api();
        inject(Call::Register, 2);
        RegisteredPair pair;
        const Failure f = registerPair(a, 1, 2, 0x0DE1, kRegisterReadOnly, &pair);
        QVERIFY(f.failed);
        QVERIFY(!pair.valid());
        QCOMPARE(g.registered, 0); // the first plane was taken back
        QCOMPARE(counters().failures[int(Call::Register)], uint64_t(1));
    }

    void refusedUndoOfTheFirstRegistrationIsCountedNotHidden()
    {
        const DriverApi a = api();
        inject(Call::Register, 2);
        inject(Call::Unregister, 1);
        RegisteredPair pair;
        QVERIFY(registerPair(a, 1, 2, 0x0DE1, kRegisterReadOnly, &pair).failed);
        QCOMPARE(counters().failures[int(Call::Unregister)], uint64_t(1));
    }

    void unregisterTriesBothEvenWhenTheFirstIsRefused()
    {
        const DriverApi a = api();
        RegisteredPair pair;
        QVERIFY(!registerPair(a, 1, 2, 0x0DE1, kRegisterReadOnly, &pair).failed);
        inject(Call::Unregister, 1);
        const Failure f = unregisterPair(a, &pair);
        QVERIFY(f.failed);
        QCOMPARE(f.call, Call::Unregister);
        QCOMPARE(g.registered, 1); // the second one went
        QVERIFY(!pair.valid());
        QVERIFY(pair.y == nullptr && pair.uv == nullptr);
    }

    // -- the copy: a failure injected at each step -----------------------------------------

    void copyFailureAtEachStep_data()
    {
        QTest::addColumn<int>("call");
        QTest::addColumn<int>("from");
        QTest::addColumn<int>("count");
        QTest::addColumn<bool>("leftMapped");
        QTest::addColumn<bool>("expectCopiesQueued"); // some copy was on the stream when it failed

        QTest::newRow("map") << int(Call::Map) << 1 << 1 << false << false;
        QTest::newRow("array of the first plane") << int(Call::GetMappedArray) << 1 << 1 << false << false;
        QTest::newRow("array of the second plane") << int(Call::GetMappedArray) << 2 << 1 << false << false;
        QTest::newRow("copy of the first plane") << int(Call::Copy) << 1 << 1 << false << false;
        QTest::newRow("copy of the second plane") << int(Call::Copy) << 2 << 1 << false << true;
        QTest::newRow("unmap, recovered by the retry") << int(Call::Unmap) << 1 << 1 << false << true;
        QTest::newRow("unmap refused twice") << int(Call::Unmap) << 1 << 2 << true << true;
        QTest::newRow("stream sync") << int(Call::StreamSync) << 1 << 1 << false << true;
    }

    void copyFailureAtEachStep()
    {
        QFETCH(int, call);
        QFETCH(int, from);
        QFETCH(int, count);
        QFETCH(bool, leftMapped);
        QFETCH(bool, expectCopiesQueued);

        for (Direction d : {Direction::DeviceToTexture, Direction::TextureToDevice}) {
            init();
            const DriverApi a = api();
            RegisteredPair pair;
            QVERIFY(!registerPair(a, 1, 2, 0x0DE1, kRegisterWriteDiscard, &pair).failed);
            inject(Call(call), from, count);

            const CopyResult r = copyNv12Planes(a, stream(), pair, d, kY, kUv, 1280, 720);
            QVERIFY(!r.ok);
            QVERIFY(r.failure.failed);
            QCOMPARE(int(r.failure.call), call);
            QCOMPARE(r.failure.rc, kFail);
            QCOMPARE(r.leftMapped, leftMapped);

            if (leftMapped) {
                QVERIFY(g.mapped > 0); // honestly reported: the driver still holds them mapped
                QCOMPARE(counters().leftMapped, uint64_t(1));
            } else {
                QCOMPARE(g.mapped, 0);
            }
            // Nothing queued may still be running when the arrays go away.
            QVERIFY(!g.unmappedWithCopyInFlightAfterFailure || call == int(Call::StreamSync));
            if (expectCopiesQueued && !leftMapped && call != int(Call::StreamSync))
                QCOMPARE(g.inFlight, 0);

            // The caller's cleanup: legal only when nothing is left mapped.
            if (!r.leftMapped) {
                QVERIFY(!unregisterPair(a, &pair).failed);
                QCOMPARE(g.registered, 0);
                QVERIFY(!g.unregisteredWhileMapped);
                QVERIFY(!g.unregisteredWithCopyInFlight || call == int(Call::StreamSync));
            }
            QVERIFY(counters().failures[call] >= 1);
        }
    }

    void failedSecondPlaneCopyIsDrainedBeforeUnmapAndUnregister()
    {
        const DriverApi a = api();
        RegisteredPair pair;
        QVERIFY(!registerPair(a, 1, 2, 0x0DE1, kRegisterWriteDiscard, &pair).failed);
        g.log.clear();
        inject(Call::Copy, 2);

        const CopyResult r =
            copyNv12Planes(a, stream(), pair, Direction::DeviceToTexture, kY, kUv, 640, 480);
        QVERIFY(!r.ok);
        QCOMPARE(r.failure.call, Call::Copy);

        const int firstCopy = indexOf("copy");
        const int sync = indexOf("sync");
        const int unmap = indexOf("unmap");
        QVERIFY(firstCopy >= 0 && sync > firstCopy && unmap > sync);
        QCOMPARE(g.mapped, 0);
        QCOMPARE(g.inFlight, 0);
        QVERIFY(!unregisterPair(a, &pair).failed);
        QVERIFY(!g.unregisteredWithCopyInFlight);
    }

    void noCopyWasQueuedMeansNoPointlessDrain()
    {
        const DriverApi a = api();
        RegisteredPair pair;
        QVERIFY(!registerPair(a, 1, 2, 0x0DE1, kRegisterWriteDiscard, &pair).failed);
        g.log.clear();
        inject(Call::GetMappedArray, 2);
        QVERIFY(!copyNv12Planes(a, stream(), pair, Direction::DeviceToTexture, kY, kUv, 640, 480).ok);
        QCOMPARE(indexOf("sync"), -1);
        QCOMPARE(g.mapped, 0);
    }

    void aSyncFailureAfterAGoodCopyIsReportedAsThatCall()
    {
        const DriverApi a = api();
        RegisteredPair pair;
        QVERIFY(!registerPair(a, 1, 2, 0x0DE1, kRegisterWriteDiscard, &pair).failed);
        inject(Call::StreamSync, 1);
        const CopyResult r =
            copyNv12Planes(a, stream(), pair, Direction::TextureToDevice, kY, kUv, 640, 480);
        QVERIFY(!r.ok);
        QCOMPARE(r.failure.call, Call::StreamSync);
        QCOMPARE(g.mapped, 0);
    }

    // -- contexts --------------------------------------------------------------------------

    void aRefusedPushPopsNothing()
    {
        const DriverApi a = api();
        inject(Call::ContextPush, 1);
        {
            ContextScope scope(a, reinterpret_cast<void *>(0x1));
            QVERIFY(!scope.ok());
            QCOMPARE(scope.pushResult(), kFail);
        }
        QCOMPARE(g.ctxDepth, 0);
        QCOMPARE(indexOf("pop"), -1);
        QCOMPARE(counters().failures[int(Call::ContextPush)], uint64_t(1));
    }

    void theContextIsPoppedOnEveryPath()
    {
        const DriverApi a = api();
        {
            ContextScope scope(a, reinterpret_cast<void *>(0x1));
            QVERIFY(scope.ok());
            QCOMPARE(g.ctxDepth, 1);
        }
        QCOMPARE(g.ctxDepth, 0);
        {
            ContextScope outer(a, reinterpret_cast<void *>(0x1));
            ContextScope inner(a, reinterpret_cast<void *>(0x2));
            QCOMPARE(g.maxCtxDepth, 2);
            QVERIFY(inner.release());
            QVERIFY(inner.release()); // a second release is a no-op
        }
        QCOMPARE(g.ctxDepth, 0);
    }

    void aRefusedPopIsCounted()
    {
        const DriverApi a = api();
        inject(Call::ContextPop, 1);
        {
            ContextScope scope(a, reinterpret_cast<void *>(0x1));
            QVERIFY(scope.ok());
            QVERIFY(!scope.release());
        }
        QCOMPARE(counters().failures[int(Call::ContextPop)], uint64_t(1));
    }

    // -- repeated, two devices, resolution changes -----------------------------------------

    void repeatedFailuresAcrossTwoDevicesAndSizesReturnToBaseline()
    {
        const DriverApi a = api();
        void *const contexts[2] = {reinterpret_cast<void *>(0xA1), reinterpret_cast<void *>(0xB2)};
        const int sizes[][2] = {{1920, 1080}, {1280, 720}, {3840, 2160}, {640, 360}};
        const Call calls[] = {Call::Map,  Call::GetMappedArray, Call::Copy,
                              Call::Copy, Call::Unmap,          Call::StreamSync};
        const int froms[] = {1, 2, 1, 2, 1, 1};

        int step = 0;
        for (int round = 0; round < 4; ++round) {
            for (int dev = 0; dev < 2; ++dev) {
                const int w = sizes[round][0];
                const int h = sizes[round][1];
                const Direction d = (round + dev) % 2 ? Direction::TextureToDevice
                                                      : Direction::DeviceToTexture;
                ContextScope scope(a, contexts[dev]);
                QVERIFY(scope.ok());

                RegisteredPair pair;
                QVERIFY(!registerPair(a, 10 + step, 20 + step, 0x0DE1, kRegisterWriteDiscard, &pair)
                             .failed);
                // An injected failure for some frames, a clean copy for the rest.
                if (step % 3 != 2) {
                    const int idx = step % 6;
                    g.calls[int(calls[idx])] = 0;
                    inject(calls[idx], froms[idx]);
                } else {
                    for (int i = 0; i < kCalls; ++i)
                        g.failFrom[i] = 0;
                }
                const CopyResult r = copyNv12Planes(a, stream(), pair, d, kY, kUv, w, h);
                QCOMPARE(r.ok, step % 3 == 2);
                QVERIFY(!r.leftMapped);
                QVERIFY(!unregisterPair(a, &pair).failed);
                QVERIFY(scope.release());

                QCOMPARE(g.registered, 0);
                QCOMPARE(g.mapped, 0);
                QCOMPARE(g.ctxDepth, 0);
                QCOMPARE(g.inFlight, 0);
                ++step;
            }
        }
        QVERIFY(!g.unregisteredWhileMapped);
        QVERIFY(!g.unregisteredWithCopyInFlight);
        QCOMPARE(g.ctxDepth, 0);
    }

    void countersAreResettable()
    {
        const DriverApi a = api();
        inject(Call::Map, 1);
        RegisteredPair pair;
        QVERIFY(!registerPair(a, 1, 2, 0x0DE1, kRegisterReadOnly, &pair).failed);
        QVERIFY(!copyNv12Planes(a, stream(), pair, Direction::DeviceToTexture, kY, kUv, 64, 64).ok);
        QCOMPARE(counters().failures[int(Call::Map)], uint64_t(1));
        resetCounters();
        QCOMPARE(counters().failures[int(Call::Map)], uint64_t(0));
        QCOMPARE(QString::fromLatin1(callName(Call::Unmap)), QStringLiteral("cuGraphicsUnmapResources"));
    }
};

QTEST_APPLESS_MAIN(TestCudaGlInterop)
#include "tst_cudaglinterop.moc"
