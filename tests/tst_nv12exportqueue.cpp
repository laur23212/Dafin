// Dafin: the NVENC export's in-flight frames when a CUDA copy fails. No GPU needed: a fake "GL
// slot" keeps the texture planes and the pixel buffer apart exactly as the real one does, so a
// frame prepared for CUDA leaves the pixel buffer holding whatever it held before.
//
// GPU runs of the same scenario (RTX 3070 / 3050 mobile, NVENC, forced CUDA copy failure) are
// NOT_RUN here; the simulated failure is not a native GPU result.
#include "engine/encode/export/Nv12ExportQueue.h"

#include <QtTest>

#include <vector>

using namespace drift::nv12q;

namespace {

constexpr int kStale = -1; // what an unwritten pixel buffer holds: not any frame of this export

struct Sent
{
    int64_t pts;
    int signature; // which frame's pixels reached the encoder; kStale = junk, -2 = placeholder
    const char *route;
};

// A frame's pixels are identified by its time, so "the right frame" is a plain comparison.
int signatureOf(int64_t timeUs) { return int(timeUs / 1000) + 100; }

class FakeGl
{
public:
    struct Slot
    {
        int texture = kStale; // the textures copyNv12SlotToCuda would read
        int pbo = kStale;     // the buffer mapNv12Slot would read
    };

    Slot state[Queue::kCapacity];
    std::vector<Sent> sent;
    std::vector<std::pair<int64_t, Mode>> prepares;
    std::vector<std::pair<int, int>> prepareSizes;
    std::vector<int64_t> discarded;

    int failCudaCopies = 0;    // the next N CUDA copies fail
    int failHostMaps = 0;      // the next N host maps fail
    int cudaCopyCalls = 0;
    bool muxFailsOnCuda = false;
    bool muxFailsOnHost = false;
    int64_t unpreparableTimeUs = -1; // prepare() refuses this frame, on any route

    Hooks hooks()
    {
        Hooks h;
        h.prepare = [this](const Job &job, Mode mode) {
            prepares.push_back({job.timeUs, mode});
            prepareSizes.push_back({job.width, job.height});
            if (job.timeUs == unpreparableTimeUs)
                return false;
            Slot &s = state[job.slot];
            (mode == Mode::Cuda ? s.texture : s.pbo) = signatureOf(job.timeUs);
            return true;
        };
        h.deliverCuda = [this](const Job &job) {
            ++cudaCopyCalls;
            if (failCudaCopies > 0) {
                --failCudaCopies;
                return Outcome::CopyFailed;
            }
            if (muxFailsOnCuda)
                return Outcome::Fatal;
            sent.push_back({job.pts, state[job.slot].texture, "cuda"});
            return Outcome::Ok;
        };
        h.deliverHost = [this](const Job &job) {
            if (failHostMaps > 0) {
                --failHostMaps;
                return Outcome::CopyFailed;
            }
            if (muxFailsOnHost)
                return Outcome::Fatal;
            // Like the real map before this fix: it succeeds on whatever the buffer holds.
            sent.push_back({job.pts, state[job.slot].pbo, "host"});
            return Outcome::Ok;
        };
        h.deliverPlaceholder = [this](const Job &job) {
            sent.push_back({job.pts, -2, "placeholder"});
            return Outcome::Ok;
        };
        h.discard = [this](const Job &job) { discarded.push_back(job.timeUs); };
        return h;
    }
};

void pushFrames(Queue &q, int from, int count, int w = 64, int h = 32)
{
    for (int i = from; i < from + count; ++i)
        q.push(i % Queue::kCapacity, i, int64_t(i) * 1000, w, h);
}

} // namespace

class TestNv12ExportQueue : public QObject
{
    Q_OBJECT

private slots:
    void cudaCopyFailureOnFirstFrameLeavesAllThreeFramesCorrectAndInOrder()
    {
        FakeGl gl;
        Queue q(gl.hooks(), true);
        pushFrames(q, 0, 3);
        gl.failCudaCopies = 1; // all three slots are packed for CUDA, then the first copy fails

        while (q.pending() > 0)
            QVERIFY(q.consumeHead());

        QCOMPARE(int(gl.sent.size()), 3);
        for (int i = 0; i < 3; ++i) {
            QCOMPARE(gl.sent[i].pts, int64_t(i));
            QCOMPARE(gl.sent[i].signature, signatureOf(int64_t(i) * 1000));
            QVERIFY2(gl.sent[i].signature != kStale, "a stale pixel buffer reached the encoder");
            QCOMPARE(QString::fromLatin1(gl.sent[i].route), QStringLiteral("host"));
        }
        QCOMPARE(q.counters().recomposed, 3);
        QCOMPARE(q.counters().placeholders, 0);
        QCOMPARE(q.counters().cudaFallbacks, 1);
        QVERIFY(!q.cudaEnabled());
        QCOMPARE(gl.cudaCopyCalls, 1); // later frames are not even tried on CUDA
    }

    void failureOnALaterFrameRedoesOnlyTheFramesStillPending()
    {
        FakeGl gl;
        Queue q(gl.hooks(), true);
        pushFrames(q, 0, 3);

        QVERIFY(q.consumeHead()); // frame 0 goes out over CUDA
        gl.failCudaCopies = 1;
        QVERIFY(q.consumeHead()); // frame 1's copy fails
        QVERIFY(q.consumeHead());

        QCOMPARE(int(gl.sent.size()), 3);
        QCOMPARE(QString::fromLatin1(gl.sent[0].route), QStringLiteral("cuda"));
        for (int i = 0; i < 3; ++i) {
            QCOMPARE(gl.sent[i].pts, int64_t(i));
            QCOMPARE(gl.sent[i].signature, signatureOf(int64_t(i) * 1000));
        }
        QCOMPARE(q.counters().recomposed, 2); // frames 1 and 2, not frame 0
    }

    void framesQueuedAfterTheFailureAreBuiltForPboFromTheStart()
    {
        FakeGl gl;
        Queue q(gl.hooks(), true);
        pushFrames(q, 0, 3);
        gl.failCudaCopies = 1;
        QVERIFY(q.consumeHead());

        gl.prepares.clear();
        pushFrames(q, 3, 1);
        QCOMPARE(int(gl.prepares.size()), 1);
        QCOMPARE(int(gl.prepares[0].second), int(Mode::Pbo)); // not composed for CUDA, then redone

        while (q.pending() > 0)
            QVERIFY(q.consumeHead());
        QCOMPARE(int(gl.sent.size()), 4);
        for (int i = 0; i < 4; ++i) {
            QCOMPARE(gl.sent[i].pts, int64_t(i));
            QCOMPARE(gl.sent[i].signature, signatureOf(int64_t(i) * 1000));
        }
    }

    void successfulCudaRouteIsUnchanged()
    {
        FakeGl gl;
        Queue q(gl.hooks(), true);
        pushFrames(q, 0, 3);
        while (q.pending() > 0)
            QVERIFY(q.consumeHead());

        QCOMPARE(int(gl.sent.size()), 3);
        for (const Sent &s : gl.sent)
            QCOMPARE(QString::fromLatin1(s.route), QStringLiteral("cuda"));
        QCOMPARE(int(gl.prepares.size()), 3); // each frame composed once
        QCOMPARE(q.counters().recomposed, 0);
        QCOMPARE(q.counters().cudaDelivered, 3);
        QVERIFY(q.cudaEnabled());
    }

    void pboOnlyExportNeverTouchesCuda()
    {
        FakeGl gl;
        Queue q(gl.hooks(), false);
        pushFrames(q, 0, 3);
        while (q.pending() > 0)
            QVERIFY(q.consumeHead());
        QCOMPARE(gl.cudaCopyCalls, 0);
        QCOMPARE(q.counters().hostDelivered, 3);
    }

    void aFrameThatCannotBeRedoneIsAPlaceholderNotAGoodFrame()
    {
        FakeGl gl;
        Queue q(gl.hooks(), true);
        pushFrames(q, 0, 3);
        gl.failCudaCopies = 1;
        // The recompose of frame 1 will fail; it was fine the first time round.
        gl.unpreparableTimeUs = -1;
        QVERIFY(q.consumeHead()); // frame 0 falls back and is correct

        gl.unpreparableTimeUs = 1000;
        // Frame 1 was already redone on the Pbo route during the fallback above, so make it
        // fail at the host map and then at the recompose too.
        gl.failHostMaps = 1;
        QVERIFY(q.consumeHead());
        QVERIFY(q.consumeHead());

        QCOMPARE(int(gl.sent.size()), 3);
        QCOMPARE(gl.sent[0].signature, signatureOf(0));
        QCOMPARE(QString::fromLatin1(gl.sent[1].route), QStringLiteral("placeholder"));
        QCOMPARE(gl.sent[2].signature, signatureOf(2000));
        QCOMPARE(q.counters().placeholders, 1);
    }

    void aPendingFrameWhoseRecomposeFailsBecomesAPlaceholder()
    {
        FakeGl gl;
        Queue q(gl.hooks(), true);
        pushFrames(q, 0, 3);
        gl.failCudaCopies = 1;
        gl.unpreparableTimeUs = 2000; // frame 2 composed fine for CUDA but cannot be redone
        // push() already ran, so only the redo is refused: frame 2 was "prepared" before.
        while (q.pending() > 0)
            QVERIFY(q.consumeHead());

        QCOMPARE(int(gl.sent.size()), 3);
        QCOMPARE(gl.sent[0].signature, signatureOf(0));
        QCOMPARE(gl.sent[1].signature, signatureOf(1000));
        QCOMPARE(QString::fromLatin1(gl.sent[2].route), QStringLiteral("placeholder"));
        QCOMPARE(q.counters().placeholders, 1);
    }

    void aFrameThatNeverPreparedIsAPlaceholder()
    {
        FakeGl gl;
        gl.unpreparableTimeUs = 1000;
        Queue q(gl.hooks(), true);
        pushFrames(q, 0, 3);
        while (q.pending() > 0)
            QVERIFY(q.consumeHead());

        QCOMPARE(int(gl.sent.size()), 3);
        QCOMPARE(QString::fromLatin1(gl.sent[1].route), QStringLiteral("placeholder"));
        QCOMPARE(gl.sent[0].signature, signatureOf(0));
        QCOMPARE(gl.sent[2].signature, signatureOf(2000));
        QCOMPARE(gl.cudaCopyCalls, 2); // never tried a CUDA copy on the unprepared frame
    }

    void aPixelBufferThatCannotBeReadIsRedoneOnceThenReportedNotFaked()
    {
        {
            FakeGl gl; // the fence wait times out once: one more compose fixes it
            Queue q(gl.hooks(), false);
            pushFrames(q, 0, 1);
            gl.failHostMaps = 1;
            QVERIFY(q.consumeHead());
            QCOMPARE(gl.sent[0].signature, signatureOf(0));
            QCOMPARE(q.counters().recomposed, 1);
            QCOMPARE(q.counters().placeholders, 0);
        }
        {
            FakeGl gl; // it never becomes readable: a placeholder, counted, never a "good" frame
            Queue q(gl.hooks(), false);
            pushFrames(q, 0, 1);
            gl.failHostMaps = 5;
            QVERIFY(q.consumeHead());
            QCOMPARE(QString::fromLatin1(gl.sent[0].route), QStringLiteral("placeholder"));
            QCOMPARE(q.counters().hostDelivered, 0);
            QCOMPARE(q.counters().placeholders, 1);
        }
    }

    void muxFailureAfterASuccessfulCopyStopsTheExportAndDoesNotFallBack()
    {
        FakeGl gl;
        Queue q(gl.hooks(), true);
        pushFrames(q, 0, 3);
        gl.muxFailsOnCuda = true;
        const size_t preparesBefore = gl.prepares.size();

        QVERIFY(!q.consumeHead());
        QVERIFY(q.cudaEnabled()); // a muxer error is not a CUDA failure
        QCOMPARE(gl.prepares.size(), preparesBefore); // nothing was recomposed
        QVERIFY(gl.sent.empty());
    }

    void muxFailureOnTheHostRouteStopsTheExport()
    {
        FakeGl gl;
        Queue q(gl.hooks(), false);
        pushFrames(q, 0, 2);
        gl.muxFailsOnHost = true;
        QVERIFY(!q.consumeHead());
        QVERIFY(gl.sent.empty());
        QCOMPARE(q.counters().placeholders, 0);
    }

    void muxFailureAfterFallbackStopsTheExportToo()
    {
        FakeGl gl;
        Queue q(gl.hooks(), true);
        pushFrames(q, 0, 3);
        gl.failCudaCopies = 1;
        gl.muxFailsOnHost = true;
        QVERIFY(!q.consumeHead());
        QVERIFY(gl.sent.empty());
    }

    void cancelDropsPendingFramesWithoutSendingAnyOfThem()
    {
        FakeGl gl;
        Queue q(gl.hooks(), true);
        pushFrames(q, 0, 3);
        q.discardAll();

        QCOMPARE(q.pending(), 0);
        QVERIFY(gl.sent.empty());
        QCOMPARE(int(gl.discarded.size()), 3);
    }

    void cancelAfterAFallbackAlsoSendsNothing()
    {
        FakeGl gl;
        Queue q(gl.hooks(), true);
        pushFrames(q, 0, 3);
        gl.failCudaCopies = 1;
        QVERIFY(q.consumeHead()); // frame 0 sent via the fallback
        q.discardAll();           // user cancels with frames 1 and 2 pending

        QCOMPARE(int(gl.sent.size()), 1);
        QCOMPARE(int(gl.discarded.size()), 2);
        QCOMPARE(q.pending(), 0);
    }

    void redoneFramesKeepTheirOwnOutputSizeAndANewExportStartsClean()
    {
        {
            FakeGl gl;
            Queue q(gl.hooks(), true);
            pushFrames(q, 0, 3, 64, 32);
            gl.failCudaCopies = 1;
            gl.prepareSizes.clear();
            while (q.pending() > 0)
                QVERIFY(q.consumeHead());
            QCOMPARE(int(gl.prepareSizes.size()), 3); // the three redos
            for (const auto &size : gl.prepareSizes)
                QCOMPARE(size, std::make_pair(64, 32));
            QVERIFY(!q.cudaEnabled());
        }
        {
            // The next export, at another size: its own queue, CUDA offered again, no leftovers.
            FakeGl gl;
            Queue q(gl.hooks(), true);
            QVERIFY(q.cudaEnabled());
            pushFrames(q, 0, 3, 128, 64);
            while (q.pending() > 0)
                QVERIFY(q.consumeHead());
            for (const auto &size : gl.prepareSizes)
                QCOMPARE(size, std::make_pair(128, 64));
            QCOMPARE(q.counters().cudaDelivered, 3);
        }
    }
};

QTEST_APPLESS_MAIN(TestNv12ExportQueue)
#include "tst_nv12exportqueue.moc"
