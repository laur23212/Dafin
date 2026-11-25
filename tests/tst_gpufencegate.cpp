// Dafin: what a fence answer means for slot reuse. No GL context: the waits are scripted.
//
// The rule under test: a slot whose fence timed out is BUSY, keeps its fence, and is not
// written again until a later wait says the GPU is done; a fence the driver refused to wait for
// is settled with a full finish before the slot is reused. Real glClientWaitSync behaviour on
// a driver is NOT_RUN here.
#include "engine/gpu/GpuFenceGate.h"

#include <QtTest>

#include <vector>

using namespace drift::fence;

namespace {

// A pixel-buffer slot, as the export has them, with a scripted fence.
struct Slot
{
    int syncObject = 0;
    int *fence = nullptr; // null: no work pending
    int gpuStillReading = 0; // the GPU is using the buffer: writing now would corrupt a frame
    int finishes = 0;
    int drops = 0;
    int writes = 0;
    int corrupted = 0;
    std::vector<Wait> script;
    size_t next = 0;

    Wait nextWait()
    {
        const Wait w = next < script.size() ? script[next] : Wait::Signaled;
        ++next;
        return w;
    }

    Verdict tryReuse()
    {
        return settle(
            fence, [&](int *) { return nextWait(); }, [&](int *) { ++drops; },
            [&] {
                ++finishes;
                gpuStillReading = 0; // glFinish: everything is done
            });
    }

    // What packCanvasToNv12Slot does: reuse the slot only when the fence lets it.
    bool pack()
    {
        if (tryReuse() == Verdict::Busy)
            return false;
        gpuStillReading = 0;
        ++writes;
        syncObject = writes;
        fence = &syncObject;
        gpuStillReading = 1; // the readback has been queued
        return true;
    }

    // The pre-fix behaviour, to show what the test guards: delete the fence whatever was said.
    bool packIgnoringTheAnswer()
    {
        if (fence) {
            nextWait();
            ++drops;
            fence = nullptr;
        }
        if (gpuStillReading)
            ++corrupted;
        ++writes;
        syncObject = writes;
        fence = &syncObject;
        gpuStillReading = 1;
        return true;
    }
};

} // namespace

class TestGpuFenceGate : public QObject
{
    Q_OBJECT

private slots:
    void init() { resetCounters(); }

    void aSlotWithNoFenceIsReusableWithoutWaiting()
    {
        Slot s;
        QCOMPARE(s.tryReuse(), Verdict::Reusable);
        QCOMPARE(s.next, size_t(0));
        QCOMPARE(s.drops, 0);
    }

    void aSignaledFenceIsDroppedAndTheSlotReusable()
    {
        Slot s;
        QVERIFY(s.pack());
        s.script = {Wait::Signaled};
        s.next = 0;
        QCOMPARE(s.tryReuse(), Verdict::Reusable);
        QVERIFY(s.fence == nullptr);
        QCOMPARE(s.drops, 1);
        QCOMPARE(s.finishes, 0);
    }

    void aTimeoutKeepsTheFenceAndTheSlotBusy()
    {
        Slot s;
        QVERIFY(s.pack());
        s.script = {Wait::TimedOut};
        s.next = 0;
        QCOMPARE(s.tryReuse(), Verdict::Busy);
        QVERIFY(s.fence != nullptr); // not deleted: the next ask waits for the same fence
        QCOMPARE(s.drops, 0);
        QCOMPARE(s.finishes, 0);
        QCOMPARE(counters().timeouts, uint64_t(1));
    }

    void aSlotThatTimedOutIsNotWrittenUntilALaterWaitSucceeds()
    {
        Slot s;
        QVERIFY(s.pack());
        QCOMPARE(s.writes, 1);
        s.script = {Wait::TimedOut, Wait::TimedOut, Wait::TimedOut, Wait::Signaled};
        s.next = 0;
        for (int i = 0; i < 3; ++i) {
            QVERIFY(!s.pack());
            QCOMPARE(s.writes, 1); // still the first frame's
            QVERIFY(s.gpuStillReading == 1);
        }
        QVERIFY(s.pack()); // the GPU finished
        QCOMPARE(s.writes, 2);
        QCOMPARE(s.corrupted, 0);
        QCOMPARE(counters().timeouts, uint64_t(3));
    }

    void theOldBehaviourWouldHaveWrittenOverAReadingGpu()
    {
        Slot s;
        QVERIFY(s.packIgnoringTheAnswer());
        s.script = {Wait::TimedOut};
        s.next = 0;
        QVERIFY(s.packIgnoringTheAnswer());
        QCOMPARE(s.corrupted, 1); // this is the bug the gate removes
    }

    void aRefusedWaitIsSettledWithAFullFinishBeforeReuse()
    {
        Slot s;
        QVERIFY(s.pack());
        s.script = {Wait::Failed};
        s.next = 0;
        QVERIFY(s.pack());
        QCOMPARE(s.finishes, 1);
        QCOMPARE(s.corrupted, 0);
        QCOMPARE(s.writes, 2);
        QCOMPARE(counters().waitFailures, uint64_t(1));
        QCOMPARE(counters().timeouts, uint64_t(0));
    }

    void finishRunsBeforeTheFenceIsDropped()
    {
        std::vector<const char *> order;
        int object = 1;
        int *fence = &object;
        const Verdict v = settle(
            fence, [](int *) { return Wait::Failed; }, [&](int *) { order.push_back("drop"); },
            [&] { order.push_back("finish"); });
        QCOMPARE(v, Verdict::Reusable);
        QCOMPARE(order.size(), size_t(2));
        QCOMPARE(QString::fromLatin1(order[0]), QStringLiteral("finish"));
        QCOMPARE(QString::fromLatin1(order[1]), QStringLiteral("drop"));
    }

    void threeSlotsAreIndependent()
    {
        Slot ring[3];
        for (Slot &s : ring)
            QVERIFY(s.pack());
        ring[1].script = {Wait::TimedOut};
        // Slot 0 and 2 are free to go; slot 1 is held back and does not hold up the others.
        QVERIFY(ring[0].pack());
        QVERIFY(!ring[1].pack());
        QVERIFY(ring[2].pack());
        QCOMPARE(ring[1].writes, 1);
        for (const Slot &s : ring)
            QCOMPARE(s.corrupted, 0);
    }
};

QTEST_APPLESS_MAIN(TestGpuFenceGate)
#include "tst_gpufencegate.moc"
