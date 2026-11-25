// Dafin: C20 — robust Filmora .wfp import. Relinking by verified identity (D-005), whole-fps and
// rational frame rate (D-013), speed curves mapped only when proven (D-014), and the cancellable
// background import with one single apply at the end (D-003). Real temporary files, no GUI, no GL,
// no media decoder: the media probe is injected.
#include "TestZip.h"

#include "core/interchange/ImportPipeline.h"
#include "core/interchange/ImportedTimelineProject.h"
#include "core/interchange/MediaRelink.h"
#include "core/project/Project.h"
#include "core/interchange/filmora/FilmoraProjectParser.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QtTest>

#include <atomic>
#include <future>
#include <thread>

using namespace drift::interchange;
using namespace drift::interchange::filmora;

namespace {

QString put(const QTemporaryDir &dir, const QString &rel, const QByteArray &bytes)
{
    const QString path = dir.filePath(rel);
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly) || f.write(bytes) != bytes.size())
        qFatal("cannot write %s", qPrintable(path));
    return QFileInfo(path).canonicalFilePath();
}

QByteArray bytesOf(char c, int n)
{
    return QByteArray(n, c);
}

// A probe that answers from a table; unknown paths are "known: false".
MediaProbeFn tableProbe(QHash<QString, int64_t> durations)
{
    return [durations](const QString &path) {
        MediaFacts facts;
        const auto it = durations.constFind(QFileInfo(path).canonicalFilePath());
        if (it != durations.constEnd()) {
            facts.known = true;
            facts.durationUs = *it;
        }
        return facts;
    };
}

constexpr int64_t kSec = 1'000'000;

QJsonObject clipJson(const QString &file, int64_t beginUs, int64_t endUs, int64_t inUs, int64_t outUs,
                     const QJsonObject &speed = {})
{
    QJsonObject c{{QStringLiteral("filename"), QStringLiteral("file://") + file},
                  {QStringLiteral("tlBegin"), double(beginUs) * 10.0},
                  {QStringLiteral("tlEnd"), double(endUs) * 10.0},
                  {QStringLiteral("inPoint"), double(inUs) * 10.0},
                  {QStringLiteral("outPoint"), double(outUs) * 10.0},
                  {QStringLiteral("type"), 1}};
    if (!speed.isEmpty())
        c.insert(QStringLiteral("speed"), speed);
    return c;
}

QJsonObject rampJson(const QList<QPair<QJsonValue, double>> &keys, bool reverse = false)
{
    QJsonArray sets;
    for (const auto &k : keys) {
        QJsonObject key{{QStringLiteral("_value"), k.second}};
        if (!k.first.isNull())
            key.insert(QStringLiteral("_time"), k.first);
        sets.append(key);
    }
    return QJsonObject{{QStringLiteral("reverse"), reverse},
                       {QStringLiteral("speedParam"), QJsonObject{{QStringLiteral("keyframeSets"), sets}}}};
}

// A .wfp with one video track of the given clips.
QString writeWfp(const QTemporaryDir &dir, const QString &name, int num, int den, const QJsonArray &clips)
{
    const QJsonObject timeline{
        {QStringLiteral("currentTimelineId"), 1},
        {QStringLiteral("timelineInfos"),
         QJsonArray{QJsonObject{
             {QStringLiteral("timelineId"), 1},
             {QStringLiteral("frameRate"), QJsonObject{{QStringLiteral("num"), num}, {QStringLiteral("den"), den}}},
             {QStringLiteral("resolutionWidth"), 1920},
             {QStringLiteral("resolutionHeight"), 1080},
             {QStringLiteral("sampleRate"), 48000},
             {QStringLiteral("trackInfos"),
              QJsonArray{QJsonObject{{QStringLiteral("trackType"), 1},
                                     {QStringLiteral("trackTag"), 1},
                                     {QStringLiteral("clipList"), clips}}}}}}}};
    const QString path = dir.filePath(name);
    if (!writeStoredZip(path,
                        {{QStringLiteral("ProjectFolder/project_info.json"),
                          QByteArray("{\"project_file_name\":\"built\",\"timeline_mediaId\":\"123\"}")},
                         {QStringLiteral("ProjectFolder/Medias/123/timeline.wesproj"),
                          QJsonDocument(timeline).toJson(QJsonDocument::Compact)}}))
        qFatal("cannot write %s", qPrintable(path));
    return path;
}


// A timeline with one video clip on the given stored path, built by hand.
ImportedTimeline oneClipTimeline(const QString &storedPath, int64_t startUs = 0, int64_t durationUs = 10 * kSec,
                                 int64_t srcInUs = 0)
{
    ImportedTimeline t;
    ImportedTrack track;
    ImportedClip c;
    c.path = storedPath;
    c.name = QFileInfo(storedPath).fileName();
    c.startUs = startUs;
    c.durationUs = durationUs;
    c.srcInUs = srcInUs;
    c.sourceId = QStringLiteral("t:video:0:0");
    track.clips.append(c);
    t.tracks.append(track);
    return t;
}

const CompatEntry *entryFor(const CompatibilityReport &report, const QString &construct)
{
    for (const CompatEntry &e : report.entries)
        if (e.construct == construct)
            return &e;
    return nullptr;
}

} // namespace

class TstWfpImport : public QObject
{
    Q_OBJECT

private slots:
    // ---- D-005: relink -----------------------------------------------------------------------

    void storedPathWinsOverEverything()
    {
        QTemporaryDir dir;
        const QString real = put(dir, QStringLiteral("a/clip.mp4"), bytesOf('a', 100));
        put(dir, QStringLiteral("b/clip.mp4"), bytesOf('b', 100));
        MediaRelinker finder(dir.filePath(QStringLiteral("p.wfp")));
        const RelinkOutcome r = finder.find(real, QString(), MediaNeed{});
        QCOMPARE(r.how, RelinkHow::AtStoredPath);
        QCOMPARE(r.path, real);
    }

    void relativePathBeatsAFileOfTheSameNameElsewhere()
    {
        QTemporaryDir dir;
        const QString wanted = put(dir, QStringLiteral("media/clip.mp4"), bytesOf('a', 100));
        put(dir, QStringLiteral("other/clip.mp4"), bytesOf('b', 100));
        MediaRelinker finder(dir.filePath(QStringLiteral("p.wfp")));
        const RelinkOutcome r = finder.find(QStringLiteral("D:/gone/clip.mp4"), QStringLiteral("media/clip.mp4"), MediaNeed{});
        QCOMPARE(r.how, RelinkHow::RelativeToProject);
        QCOMPARE(QFileInfo(r.path).canonicalFilePath(), wanted);
    }

    void aSingleSameNameFileIsTakenAndSaidToBeUnverifiedWithoutFacts()
    {
        QTemporaryDir dir;
        const QString only = put(dir, QStringLiteral("x/clip.mp4"), bytesOf('a', 100));
        MediaRelinker finder(dir.filePath(QStringLiteral("p.wfp")));
        const RelinkOutcome r = finder.find(QStringLiteral("D:/gone/clip.mp4"), QString(), MediaNeed{});
        QCOMPARE(r.how, RelinkHow::ByNameUnique);
        QCOMPARE(r.path, only);
        QVERIFY(!r.verified());
    }

    void aSingleFileLongEnoughIsVerifiedByDuration()
    {
        QTemporaryDir dir;
        const QString only = put(dir, QStringLiteral("x/clip.mp4"), bytesOf('a', 100));
        MediaRelinker finder(dir.filePath(QStringLiteral("p.wfp")), tableProbe({{only, 60 * kSec}}));
        MediaNeed need;
        need.requiredDurationUs = 30 * kSec;
        const RelinkOutcome r = finder.find(QStringLiteral("D:/gone/clip.mp4"), QString(), need);
        QCOMPARE(r.how, RelinkHow::ByNameUnique);
        QVERIFY(r.verified());
        QVERIFY(r.verifiedBy.contains(QStringLiteral("duration")));
    }

    void aSingleFileTooShortForTheClipIsNotTaken()
    {
        QTemporaryDir dir;
        const QString shortFile = put(dir, QStringLiteral("x/clip.mp4"), bytesOf('a', 100));
        MediaRelinker finder(dir.filePath(QStringLiteral("p.wfp")), tableProbe({{shortFile, 5 * kSec}}));
        MediaNeed need;
        need.requiredDurationUs = 30 * kSec;
        const RelinkOutcome r = finder.find(QStringLiteral("D:/gone/clip.mp4"), QString(), need);
        QCOMPARE(r.how, RelinkHow::NotFound);
        QCOMPARE(r.rejected, 1);
    }

    void durationPicksTheRightOneOfTwoSameNameFiles()
    {
        QTemporaryDir dir;
        const QString shortOne = put(dir, QStringLiteral("a/clip.mp4"), bytesOf('a', 100));
        const QString longOne = put(dir, QStringLiteral("b/clip.mp4"), bytesOf('b', 200));
        MediaRelinker finder(dir.filePath(QStringLiteral("p.wfp")),
                             tableProbe({{shortOne, 5 * kSec}, {longOne, 60 * kSec}}));
        MediaNeed need;
        need.requiredDurationUs = 30 * kSec;
        const RelinkOutcome r = finder.find(QStringLiteral("D:/gone/clip.mp4"), QString(), need);
        QCOMPARE(r.how, RelinkHow::ByNameUnique);
        QCOMPARE(r.path, longOne);
        QVERIFY(r.verified());
    }

    void twoDifferentFilesThatBothFitAreNeverResolvedBySilentlyTakingTheFirst()
    {
        QTemporaryDir dir;
        const QString a = put(dir, QStringLiteral("a/clip.mp4"), bytesOf('a', 100));
        const QString b = put(dir, QStringLiteral("b/clip.mp4"), bytesOf('b', 100)); // same size, other bytes
        MediaRelinker finder(dir.filePath(QStringLiteral("p.wfp")), tableProbe({{a, 60 * kSec}, {b, 60 * kSec}}));
        MediaNeed need;
        need.requiredDurationUs = 30 * kSec;
        const RelinkOutcome r = finder.find(QStringLiteral("D:/gone/clip.mp4"), QString(), need);
        QCOMPARE(r.how, RelinkHow::Ambiguous);
        QCOMPARE(r.path, QStringLiteral("D:/gone/clip.mp4")); // nothing chosen: still the stored path
        QVERIFY(!r.found());
        QStringList paths;
        for (const MediaCandidate &c : r.candidates)
            paths << c.path;
        paths.sort();
        QCOMPARE(paths, (QStringList{a, b}));
    }

    void byteIdenticalCopiesAreNotAnAmbiguityAndTheNearerFolderWins()
    {
        QTemporaryDir dir;
        put(dir, QStringLiteral("backup/old/clip.mp4"), bytesOf('a', 300));
        const QString near = put(dir, QStringLiteral("shoot/day1/clip.mp4"), bytesOf('a', 300));
        MediaRelinker finder(dir.filePath(QStringLiteral("p.wfp")));
        const RelinkOutcome r = finder.find(QStringLiteral("D:/Shoot/Day1/clip.mp4"), QString(), MediaNeed{});
        QCOMPARE(r.how, RelinkHow::ByNameIdenticalCopies);
        QCOMPARE(r.path, near);
        QVERIFY(r.verifiedBy.contains(QStringLiteral("hash")));
    }

    void identicalCopiesPickTheSameOneEveryTime()
    {
        QTemporaryDir dir;
        put(dir, QStringLiteral("m/clip.mp4"), bytesOf('a', 300));
        put(dir, QStringLiteral("n/clip.mp4"), bytesOf('a', 300));
        QString first;
        for (int i = 0; i < 3; ++i) {
            MediaRelinker finder(dir.filePath(QStringLiteral("p.wfp")));
            const RelinkOutcome r = finder.find(QStringLiteral("D:/gone/clip.mp4"), QString(), MediaNeed{});
            QCOMPARE(r.how, RelinkHow::ByNameIdenticalCopies);
            if (first.isEmpty())
                first = r.path;
            QCOMPARE(r.path, first);
        }
    }

    void filesThatDifferNearTheEndAreTold()
    {
        // The partial hash reads the first and the last 64 KiB of a file, so a difference in
        // either is caught even when the sizes agree.
        QTemporaryDir dir;
        QByteArray a = bytesOf('a', 300 * 1024);
        QByteArray b = a;
        b[b.size() - 10] = 'z';
        const QString pa = put(dir, QStringLiteral("a/clip.mp4"), a);
        put(dir, QStringLiteral("b/clip.mp4"), b);
        MediaRelinker finder(dir.filePath(QStringLiteral("p.wfp")));
        const RelinkOutcome r = finder.find(QStringLiteral("D:/gone/clip.mp4"), QString(), MediaNeed{});
        QCOMPARE(r.how, RelinkHow::Ambiguous);
        Q_UNUSED(pa);
    }

    void expectedSizeAndHashSelectTheRightFile()
    {
        QTemporaryDir dir;
        const QString a = put(dir, QStringLiteral("a/clip.mp4"), bytesOf('a', 100));
        const QString b = put(dir, QStringLiteral("b/clip.mp4"), bytesOf('b', 150));
        MediaRelinker probeFinder(dir.filePath(QStringLiteral("p.wfp")));
        MediaNeed need;
        need.expectedSize = 150;
        const RelinkOutcome bySize = probeFinder.find(QStringLiteral("D:/gone/clip.mp4"), QString(), need);
        QCOMPARE(bySize.how, RelinkHow::ByNameUnique);
        QCOMPARE(bySize.path, b);
        QVERIFY(bySize.verifiedBy.contains(QStringLiteral("size")));

        MediaRelinker finder2(dir.filePath(QStringLiteral("p.wfp")));
        MediaNeed byHash;
        byHash.expectedPartialHash = partialHashOf(a);
        byHash.expectedSize = 100;
        const RelinkOutcome r2 = finder2.find(QStringLiteral("D:/gone/clip.mp4"), QString(), byHash);
        QCOMPARE(r2.path, a);
        QVERIFY(r2.verifiedBy.contains(QStringLiteral("hash")));

        MediaRelinker finder3(dir.filePath(QStringLiteral("p.wfp")));
        MediaNeed none;
        none.expectedSize = 999;
        QCOMPARE(finder3.find(QStringLiteral("D:/gone/clip.mp4"), QString(), none).how, RelinkHow::NotFound);
    }

    void aChoiceTheUserMadeIsUsedAndDecliningLeavesTheClipMissing()
    {
        QTemporaryDir dir;
        const QString a = put(dir, QStringLiteral("a/clip.mp4"), bytesOf('a', 100));
        const QString b = put(dir, QStringLiteral("b/clip.mp4"), bytesOf('b', 100));
        const QString stored = QStringLiteral("D:/gone/clip.mp4");

        MediaRelinker pick(dir.filePath(QStringLiteral("p.wfp")), {}, {}, {{relinkKey(stored), b}});
        const RelinkOutcome r = pick.find(stored, QString(), MediaNeed{});
        QCOMPARE(r.how, RelinkHow::Chosen);
        QCOMPARE(r.path, b);

        MediaRelinker decline(dir.filePath(QStringLiteral("p.wfp")), {}, {}, {{relinkKey(stored), QString()}});
        const RelinkOutcome d = decline.find(stored, QString(), MediaNeed{});
        QCOMPARE(d.how, RelinkHow::NotFound);
        QVERIFY(d.declined);
        Q_UNUSED(a);
    }

    void aChoiceOutsideTheCandidatesIsIgnored()
    {
        QTemporaryDir dir;
        put(dir, QStringLiteral("a/clip.mp4"), bytesOf('a', 100));
        put(dir, QStringLiteral("b/clip.mp4"), bytesOf('b', 100));
        const QString stranger = put(dir, QStringLiteral("c/other.mp4"), bytesOf('c', 100));
        const QString stored = QStringLiteral("D:/gone/clip.mp4");
        MediaRelinker finder(dir.filePath(QStringLiteral("p.wfp")), {}, {}, {{relinkKey(stored), stranger}});
        QCOMPARE(finder.find(stored, QString(), MediaNeed{}).how, RelinkHow::Ambiguous);
    }

    void symlinksInTheSearchedFolderAreNotCandidates()
    {
        QTemporaryDir dir;
        const QString real = put(dir, QStringLiteral("a/clip.mp4"), bytesOf('a', 100));
        QDir().mkpath(dir.filePath(QStringLiteral("b")));
        QVERIFY(QFile::link(real, dir.filePath(QStringLiteral("b/clip.mp4"))));
        MediaRelinker finder(dir.filePath(QStringLiteral("p.wfp")));
        const RelinkOutcome r = finder.find(QStringLiteral("D:/gone/clip.mp4"), QString(), MediaNeed{});
        QCOMPARE(r.how, RelinkHow::ByNameUnique);
        QCOMPARE(r.path, real);
    }

    void nameMatchIgnoresCase()
    {
        QTemporaryDir dir;
        const QString only = put(dir, QStringLiteral("a/CLIP.MP4"), bytesOf('a', 100));
        MediaRelinker finder(dir.filePath(QStringLiteral("p.wfp")));
        QCOMPARE(finder.find(QStringLiteral("D:\\gone\\clip.mp4"), QString(), MediaNeed{}).path, only);
    }

    void aCancelledSearchStopsAndSaysSo()
    {
        QTemporaryDir dir;
        for (int i = 0; i < 600; ++i)
            put(dir, QStringLiteral("f/%1.bin").arg(i), bytesOf('a', 4));
        auto cancel = std::make_shared<CancelToken>();
        cancel->cancel();
        MediaRelinker finder(dir.filePath(QStringLiteral("p.wfp")), {}, cancel);
        const RelinkOutcome r = finder.find(QStringLiteral("D:/gone/599.bin"), QString(), MediaNeed{});
        QCOMPARE(r.how, RelinkHow::NotFound);
        QVERIFY(r.cancelled);
    }

    // ---- D-013: frame rate ---------------------------------------------------------------------

    void ntscRatesAreKeptAndOtherFractionsAreNot()
    {
        const KeptRate a = keptFrameRate(30000, 1001);
        QCOMPARE(a.num, 30000);
        QCOMPARE(a.den, 1001);
        QVERIFY(keptFrameRate(24000, 1001).valid());
        QVERIFY(keptFrameRate(60000, 1001).valid());
        QVERIFY(keptFrameRate(48000, 1001).valid());
        QVERIFY(keptFrameRate(2997, 100).valid());
        const KeptRate reduced = keptFrameRate(60000, 2002);
        QCOMPARE(reduced.num, 30000);
        QCOMPARE(reduced.den, 1001);
        QVERIFY(!keptFrameRate(30, 1).valid());     // whole already
        QVERIFY(!keptFrameRate(60, 2).valid());     // whole once reduced
        QVERIFY(!keptFrameRate(25, 2).valid());     // 12.5: rounding would be 4 % off
        QVERIFY(!keptFrameRate(1, 0).valid());
        QVERIFY(!keptFrameRate(0, 1001).valid());
        QVERIFY(!keptFrameRate(-30000, 1001).valid());
    }

    void aKeptRateReachesTheProjectAndSurvivesSaving()
    {
        QTemporaryDir dir;
        const QString wfp = writeWfp(dir, QStringLiteral("p.wfp"), 24000, 1001, {});
        QString error;
        const auto t = filmora::parse(wfp, &error);
        QVERIFY2(t.has_value(), qPrintable(error));
        const ProjectBuildResult built = toProject(*t, wfp);
        QCOMPARE(built.project.fps(), 24);
        int n = 0, d = 0;
        built.project.frameRate(&n, &d);
        QCOMPARE(n, 24000);
        QCOMPARE(d, 1001);

        const QJsonObject json = built.project.toJson();
        QCOMPARE(json.value(QStringLiteral("fpsNum")).toInt(), 24000);
        QCOMPARE(json.value(QStringLiteral("fpsDen")).toInt(), 1001);
        QString loadError;
        const drift::Project again = drift::Project::fromJson(json, &loadError);
        again.frameRate(&n, &d);
        QCOMPARE(n, 24000);
        QCOMPARE(d, 1001);
    }

    void aWholeRateWritesNoNewKeysAndAnOldFileReadsAsBefore()
    {
        drift::Project p;
        p.setFps(25);
        const QJsonObject json = p.toJson();
        QVERIFY(!json.contains(QStringLiteral("fpsNum")));
        QVERIFY(!json.contains(QStringLiteral("fpsDen")));
        QString loadError;
        const drift::Project again = drift::Project::fromJson(json, &loadError);
        int n = 0, d = 0;
        again.frameRate(&n, &d);
        QCOMPARE(n, 25);
        QCOMPARE(d, 1);
    }

    void changingTheProjectFpsDropsAStaleExactRate()
    {
        drift::Project p;
        p.setFps(30);
        p.setExactFrameRate(30000, 1001);
        QVERIFY(p.hasExactFrameRate());
        p.setFps(25);
        QVERIFY(!p.hasExactFrameRate());
        p.setExactFrameRate(30000, 1001); // does not round to 25: refused
        QVERIFY(!p.hasExactFrameRate());
    }

    void aRateDafinCannotKeepIsRoundedAndReported()
    {
        QTemporaryDir dir;
        const QString wfp = writeWfp(dir, QStringLiteral("p.wfp"), 25, 2, {});
        QString error;
        const auto t = filmora::parse(wfp, &error);
        QVERIFY2(t.has_value(), qPrintable(error));
        QCOMPARE(t->keptFpsNum, 0);
        const CompatEntry *e = entryFor(t->report, QStringLiteral("frame_rate"));
        QVERIFY(e);
        QCOMPARE(e->status, CompatStatus::Approximated);
        QVERIFY(t->report.hasLoss());
        QVERIFY(!toProject(*t, wfp).project.hasExactFrameRate());
    }

    void aWholeRateIsPreservedWithoutAnyExactRate()
    {
        QTemporaryDir dir;
        const QString wfp = writeWfp(dir, QStringLiteral("p.wfp"), 25, 1, {});
        const auto t = filmora::parse(wfp);
        QVERIFY(t.has_value());
        QCOMPARE(entryFor(t->report, QStringLiteral("frame_rate"))->status, CompatStatus::Preserved);
        QCOMPARE(t->keptFpsNum, 0);
    }

    // ---- D-014: speed curves -------------------------------------------------------------------

    // A ramp 1x to 2x in straight lines over 10 s of source plays for 10 * ln 2 = 6.9315 s.
    void aProvenRampBecomesADafinSpeedCurve()
    {
        QTemporaryDir dir;
        const QJsonArray clips{clipJson(QStringLiteral("/m/a.mp4"), 0, 6'931'472, 0, 10 * kSec,
                                        rampJson({{0.0, 1.0}, {1.0, 2.0}}))};
        const QString wfp = writeWfp(dir, QStringLiteral("p.wfp"), 30, 1, clips);
        QString error;
        const auto t = filmora::parse(wfp, &error);
        QVERIFY2(t.has_value(), qPrintable(error));
        const ImportedClip &c = t->tracks.first().clips.first();
        QCOMPARE(c.speedPoints.size(), 2);
        QCOMPARE(c.speed, 1.0);
        QCOMPARE(c.srcSpanUs, 10 * kSec);
        const CompatEntry *e = entryFor(t->report, QStringLiteral("speed_curve"));
        QVERIFY(e);
        QCOMPARE(e->status, CompatStatus::Preserved);
        QVERIFY(!t->notes.contains(QStringLiteral("Speed ramps became constant speeds.")));

        const ProjectBuildResult built = toProject(*t, wfp);
        const drift::Clip &clip = built.project.tracks().first().clips.first();
        QVERIFY(clip.hasSpeedCurve());
        QCOMPARE(clip.srcOut - clip.srcIn, 10 * kSec);
        QCOMPARE(clip.timelineDuration, int64_t(6'931'472)); // the length the project gave it
        QVERIFY(qAbs(clip.speedCurve.retimedDurationUs(10 * kSec) - clip.timelineDuration) < 20'000);
    }

    void keyTimesInSourceTicksAreAcceptedToo()
    {
        QTemporaryDir dir;
        const double end = double(10 * kSec) * 10.0; // ticks of 100 ns
        const QJsonArray clips{clipJson(QStringLiteral("/m/a.mp4"), 0, 6'931'472, 0, 10 * kSec,
                                        rampJson({{0.0, 1.0}, {end, 2.0}}))};
        const auto t = filmora::parse(writeWfp(dir, QStringLiteral("p.wfp"), 30, 1, clips));
        QVERIFY(t.has_value());
        QCOMPARE(t->tracks.first().clips.first().speedPoints.size(), 2);
    }

    void aRampIsMappedWithItsReversalKept()
    {
        QTemporaryDir dir;
        const QJsonArray clips{clipJson(QStringLiteral("/m/a.mp4"), 0, 6'931'472, 0, 10 * kSec,
                                        rampJson({{0.0, 1.0}, {1.0, 2.0}}, true))};
        const QString wfp = writeWfp(dir, QStringLiteral("p.wfp"), 30, 1, clips);
        const auto t = filmora::parse(wfp);
        QVERIFY(t.has_value());
        const auto built = toProject(*t, wfp); // kept alive: the clip below lives inside it
        const drift::Clip &clip = built.project.tracks().first().clips.first();
        QVERIFY(clip.reverse);
        QVERIFY(clip.hasSpeedCurve());
    }

    void aRampThatDoesNotLastTheRecordedLengthIsNotMapped()
    {
        QTemporaryDir dir;
        // Same keys, but the project says the clip lasts 5 s, not 6.93 s.
        const QJsonArray clips{clipJson(QStringLiteral("/m/a.mp4"), 0, 5 * kSec, 0, 10 * kSec,
                                        rampJson({{0.0, 1.0}, {1.0, 2.0}}))};
        const auto t = filmora::parse(writeWfp(dir, QStringLiteral("p.wfp"), 30, 1, clips));
        QVERIFY(t.has_value());
        const ImportedClip &c = t->tracks.first().clips.first();
        QVERIFY(c.speedPoints.isEmpty());
        QCOMPARE(c.speed, 2.0); // the source span over the timeline span, as before
        const CompatEntry *e = entryFor(t->report, QStringLiteral("speed_curve"));
        QVERIFY(e);
        QCOMPARE(e->status, CompatStatus::Approximated);
        QVERIFY(e->reason.contains(QStringLiteral("not mapped")));
        QVERIFY(t->notes.contains(QStringLiteral("Speed ramps became constant speeds.")));
    }

    void rampsThatCannotBeProvenStayApproximated_data()
    {
        QTest::addColumn<QJsonObject>("speed");
        QTest::addColumn<QString>("why");
        QTest::newRow("no key times (the layout the fixtures have)")
            << rampJson({{QJsonValue(), 1.0}, {QJsonValue(), 2.0}}) << QStringLiteral("where on the clip");
        QTest::newRow("speed past Dafin's range") << rampJson({{0.0, 1.0}, {1.0, 20.0}}) << QStringLiteral("outside");
        QTest::newRow("speed below Dafin's range") << rampJson({{0.0, 0.01}, {1.0, 1.0}}) << QStringLiteral("outside");
        QTest::newRow("keys out of order") << rampJson({{0.0, 1.0}, {0.7, 2.0}, {0.4, 2.0}, {1.0, 1.0}})
                                           << QStringLiteral("not in order");
        QTest::newRow("first key not at the start") << rampJson({{0.2, 1.0}, {1.0, 2.0}}) << QStringLiteral("start");
        QTest::newRow("last key not at the end") << rampJson({{0.0, 1.0}, {0.8, 2.0}}) << QStringLiteral("end");
        QTest::newRow("positions that fit nothing") << rampJson({{0.0, 1.0}, {123456.0, 2.0}})
                                                    << QStringLiteral("source ends");
        QTest::newRow("a position that is not a number") << rampJson({{QJsonValue(QStringLiteral("x")), 1.0}, {1.0, 2.0}})
                                                         << QStringLiteral("usable number");
    }

    void rampsThatCannotBeProvenStayApproximated()
    {
        QFETCH(QJsonObject, speed);
        QFETCH(QString, why);
        QTemporaryDir dir;
        const QJsonArray clips{clipJson(QStringLiteral("/m/a.mp4"), 0, 6'931'472, 0, 10 * kSec, speed)};
        const auto t = filmora::parse(writeWfp(dir, QStringLiteral("p.wfp"), 30, 1, clips));
        QVERIFY(t.has_value());
        QVERIFY(t->tracks.first().clips.first().speedPoints.isEmpty());
        const CompatEntry *e = entryFor(t->report, QStringLiteral("speed_curve"));
        QVERIFY2(e, "an unproven ramp is still reported");
        QCOMPARE(e->status, CompatStatus::Approximated);
        QVERIFY2(e->reason.contains(why), qPrintable(e->reason));
    }

    void aConstantSpeedStillNeedsNoRampAndIsUntouched()
    {
        QTemporaryDir dir;
        const QJsonArray clips{clipJson(QStringLiteral("/m/a.mp4"), 0, 5 * kSec, 0, 10 * kSec,
                                        rampJson({{0.0, 2.0}, {1.0, 2.0}}))};
        const auto t = filmora::parse(writeWfp(dir, QStringLiteral("p.wfp"), 30, 1, clips));
        QVERIFY(t.has_value());
        const ImportedClip &c = t->tracks.first().clips.first();
        QVERIFY(c.speedPoints.isEmpty());
        QCOMPARE(c.speed, 2.0);
        QVERIFY(!t->report.hasLoss() || !entryFor(t->report, QStringLiteral("speed_curve")));
    }

    // ---- D-005 inside the project builder ----------------------------------------------------

    void builderLeavesATiedFileUnlinkedAndAsksInsteadOfTakingTheFirst()
    {
        QTemporaryDir dir;
        const QString a = put(dir, QStringLiteral("a/clip.mp4"), bytesOf('a', 100));
        const QString b = put(dir, QStringLiteral("b/clip.mp4"), bytesOf('b', 100));
        const ImportedTimeline t = oneClipTimeline(QStringLiteral("D:/gone/clip.mp4"));
        const ProjectBuildResult built = toProject(t, dir.filePath(QStringLiteral("p.wfp")));
        QCOMPARE(built.questions.size(), 1);
        QCOMPARE(built.questions.first().key, QStringLiteral("D:/gone/clip.mp4"));
        QCOMPARE(built.questions.first().candidates.size(), 2);
        const drift::Clip &clip = built.project.tracks().first().clips.first();
        QVERIFY(clip.path != a && clip.path != b);
        QCOMPARE(built.missingMedia.size(), 1);
        const CompatEntry *e = entryFor(built.report, QStringLiteral("media_file"));
        QVERIFY(e);
        QCOMPARE(e->status, CompatStatus::MissingDependency);
        QVERIFY(e->reason.contains(QStringLiteral("different files")));
    }

    void builderUsesThePersonsAnswer()
    {
        QTemporaryDir dir;
        put(dir, QStringLiteral("a/clip.mp4"), bytesOf('a', 100));
        const QString b = put(dir, QStringLiteral("b/clip.mp4"), bytesOf('b', 100));
        BuildOptions options;
        options.relinkChoices.insert(QStringLiteral("D:/gone/clip.mp4"), b);
        const ProjectBuildResult built =
            toProject(oneClipTimeline(QStringLiteral("D:/gone/clip.mp4")), dir.filePath(QStringLiteral("p.wfp")), options);
        QVERIFY(built.questions.isEmpty());
        QVERIFY(built.missingMedia.isEmpty());
        QCOMPARE(built.project.tracks().first().clips.first().path, b);
        const CompatEntry *e = entryFor(built.report, QStringLiteral("media_relinked"));
        QVERIFY(e);
        QCOMPARE(e->status, CompatStatus::Preserved);
    }

    void builderSaysWhenAFileWasTakenByNameAloneAndWhenItWasChecked()
    {
        QTemporaryDir dir;
        const QString only = put(dir, QStringLiteral("a/clip.mp4"), bytesOf('a', 100));
        const QString wfp = dir.filePath(QStringLiteral("p.wfp"));
        const ImportedTimeline t = oneClipTimeline(QStringLiteral("D:/gone/clip.mp4"));

        const ProjectBuildResult unverified = toProject(t, wfp);
        QCOMPARE(unverified.project.tracks().first().clips.first().path, only);
        const CompatEntry *u = entryFor(unverified.report, QStringLiteral("media_relinked"));
        QVERIFY(u);
        QCOMPARE(u->status, CompatStatus::Approximated);
        QVERIFY(unverified.report.hasLoss());

        BuildOptions options;
        options.probe = tableProbe({{only, 60 * kSec}});
        const ProjectBuildResult verified = toProject(t, wfp, options);
        const CompatEntry *v = entryFor(verified.report, QStringLiteral("media_relinked"));
        QVERIFY(v);
        QCOMPARE(v->status, CompatStatus::Preserved);
        QVERIFY(v->applied.contains(QStringLiteral("duration")));
    }

    void theLongestUseOfAFileDecidesWhichCopyIsLongEnough()
    {
        QTemporaryDir dir;
        const QString shortOne = put(dir, QStringLiteral("a/clip.mp4"), bytesOf('a', 100));
        const QString longOne = put(dir, QStringLiteral("b/clip.mp4"), bytesOf('b', 200));
        ImportedTimeline t = oneClipTimeline(QStringLiteral("D:/gone/clip.mp4"), 0, 5 * kSec, 0);
        ImportedClip later = t.tracks.first().clips.first();
        later.startUs = 20 * kSec;
        later.srcInUs = 40 * kSec; // this use needs 45 s of the file
        t.tracks.first().clips.append(later);
        BuildOptions options;
        options.probe = tableProbe({{shortOne, 30 * kSec}, {longOne, 90 * kSec}});
        const ProjectBuildResult built = toProject(t, dir.filePath(QStringLiteral("p.wfp")), options);
        QVERIFY(built.questions.isEmpty());
        for (const drift::Clip &clip : built.project.tracks().first().clips)
            QCOMPARE(clip.path, longOne);
        QCOMPARE(built.report.count(CompatStatus::Approximated) + built.report.count(CompatStatus::MissingDependency), 0);
    }

    void aFileUsedByManyClipsIsAskedAboutOnce()
    {
        QTemporaryDir dir;
        put(dir, QStringLiteral("a/clip.mp4"), bytesOf('a', 100));
        put(dir, QStringLiteral("b/clip.mp4"), bytesOf('b', 100));
        ImportedTimeline t = oneClipTimeline(QStringLiteral("D:/gone/clip.mp4"));
        ImportedClip again = t.tracks.first().clips.first();
        again.startUs = 20 * kSec;
        t.tracks.first().clips.append(again);
        const ProjectBuildResult built = toProject(t, dir.filePath(QStringLiteral("p.wfp")));
        QCOMPARE(built.questions.size(), 1);
        QCOMPARE(built.report.count(CompatStatus::MissingDependency), 1);
    }

    // ---- D-003: the import as one background job ---------------------------------------------

    void importOfAGoodProjectReturnsOneCompleteProject()
    {
        QTemporaryDir dir;
        const QString media = put(dir, QStringLiteral("clip.mp4"), bytesOf('a', 100));
        const QString wfp = writeWfp(dir, QStringLiteral("p.wfp"), 30, 1,
                                     {clipJson(media, 0, 4 * kSec, 0, 4 * kSec)});
        int probes = 0;
        ImportRequest request;
        request.wfpPath = wfp;
        request.probe = [&probes](const QString &) {
            ++probes;
            MediaFacts f;
            f.known = true;
            f.durationUs = 60 * kSec;
            f.size = QSize(1000, 500);
            return f;
        };
        const ImportOutcome out = runFilmoraImport(request);
        QCOMPARE(out.status, ImportOutcome::Status::Ok);
        QCOMPARE(out.built.clipCount, 1);
        const drift::Clip &clip = out.built.project.tracks().first().clips.first();
        // fitted to its media's shape: 1000x500 in 1920x1080 -> 1920x960, centred
        QCOMPARE(clip.transformW.evaluateAt(0), 1920.0);
        QCOMPARE(clip.transformH.evaluateAt(0), 960.0);
        QCOMPARE(clip.transformY.evaluateAt(0), 60.0);
        QCOMPARE(probes, 1); // one file, probed once, for the search and for the layout together
    }

    void aDamagedProjectFailsWithAMessageAndNothingElse()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("bad.wfp"));
        QVERIFY(QFile::copy(put(dir, QStringLiteral("seed.bin"), QByteArray("not a zip at all")), path));
        int probes = 0;
        ImportRequest request;
        request.wfpPath = path;
        request.probe = [&probes](const QString &) {
            ++probes;
            return MediaFacts{};
        };
        const ImportOutcome out = runFilmoraImport(request);
        QCOMPARE(out.status, ImportOutcome::Status::Failed);
        QVERIFY(!out.error.isEmpty());
        QCOMPARE(probes, 0);
        QCOMPARE(out.built.clipCount, 0);
    }

    void anAlreadyCancelledImportDoesNothing()
    {
        QTemporaryDir dir;
        const QString media = put(dir, QStringLiteral("clip.mp4"), bytesOf('a', 100));
        ImportRequest request;
        request.wfpPath = writeWfp(dir, QStringLiteral("p.wfp"), 30, 1, {clipJson(media, 0, 4 * kSec, 0, 4 * kSec)});
        request.cancel = std::make_shared<CancelToken>();
        request.cancel->cancel();
        int probes = 0;
        request.probe = [&probes](const QString &) {
            ++probes;
            return MediaFacts{};
        };
        const ImportOutcome out = runFilmoraImport(request);
        QCOMPARE(out.status, ImportOutcome::Status::Cancelled);
        QCOMPARE(probes, 0);
    }

    void cancellingWhileMediaIsBeingProbedEndsTheImportWithoutAProject()
    {
        QTemporaryDir dir;
        QJsonArray clips;
        for (int i = 0; i < 5; ++i)
            clips.append(clipJson(put(dir, QStringLiteral("c%1.mp4").arg(i), bytesOf('a', 10 + i)), i * kSec,
                                  (i + 1) * kSec, 0, kSec));
        ImportRequest request;
        request.wfpPath = writeWfp(dir, QStringLiteral("p.wfp"), 30, 1, clips);
        request.cancel = std::make_shared<CancelToken>();
        int probes = 0;
        const CancelPtr token = request.cancel;
        request.probe = [&probes, token](const QString &) {
            if (++probes == 2)
                token->cancel(); // the person presses Cancel while the second file is being read
            return MediaFacts{};
        };
        const ImportOutcome out = runFilmoraImport(request);
        QCOMPARE(out.status, ImportOutcome::Status::Cancelled);
        QVERIFY(probes < 5); // it stopped, it did not finish the list
        QCOMPARE(out.built.clipCount, 0);
    }

    void cancellingFromAnotherThreadStopsAWorkerThatIsBlockedInTheProbe()
    {
        QTemporaryDir dir;
        const QString media = put(dir, QStringLiteral("clip.mp4"), bytesOf('a', 100));
        ImportRequest request;
        request.wfpPath = writeWfp(dir, QStringLiteral("p.wfp"), 30, 1, {clipJson(media, 0, 4 * kSec, 0, 4 * kSec),
                                                                          clipJson(media, 5 * kSec, 9 * kSec, 0, 4 * kSec)});
        request.cancel = std::make_shared<CancelToken>();
        std::promise<void> inProbe;
        std::promise<void> release;
        std::shared_future<void> released = release.get_future().share();
        std::atomic<bool> signalled{false};
        request.probe = [&](const QString &) {
            if (!signalled.exchange(true))
                inProbe.set_value();
            released.wait();
            return MediaFacts{};
        };
        std::promise<ImportOutcome> result;
        std::thread worker([&] { result.set_value(runFilmoraImport(request)); });
        inProbe.get_future().wait(); // the worker is now inside the probe
        request.cancel->cancel();    // the GUI thread presses Cancel
        release.set_value();
        const ImportOutcome out = result.get_future().get();
        worker.join();
        QCOMPARE(out.status, ImportOutcome::Status::Cancelled);
    }

    void aTieStopsTheImportAtANeedsChoiceAndTheAnswerFinishesIt()
    {
        QTemporaryDir dir;
        put(dir, QStringLiteral("a/clip.mp4"), bytesOf('a', 100));
        const QString b = put(dir, QStringLiteral("b/clip.mp4"), bytesOf('b', 100));
        ImportRequest request;
        request.wfpPath = writeWfp(dir, QStringLiteral("p.wfp"), 30, 1,
                                   {clipJson(QStringLiteral("/gone/clip.mp4"), 0, 4 * kSec, 0, 4 * kSec)});
        const ImportOutcome first = runFilmoraImport(request);
        QCOMPARE(first.status, ImportOutcome::Status::NeedsChoice);
        QCOMPARE(first.built.questions.size(), 1);

        request.relinkChoices.insert(first.built.questions.first().key, b);
        const ImportOutcome second = runFilmoraImport(request);
        QCOMPARE(second.status, ImportOutcome::Status::Ok);
        QCOMPARE(second.built.project.tracks().first().clips.first().path, b);
    }

    void decliningTheChoiceStillFinishesTheImportWithAMissingClip()
    {
        QTemporaryDir dir;
        put(dir, QStringLiteral("a/clip.mp4"), bytesOf('a', 100));
        put(dir, QStringLiteral("b/clip.mp4"), bytesOf('b', 100));
        ImportRequest request;
        request.wfpPath = writeWfp(dir, QStringLiteral("p.wfp"), 30, 1,
                                   {clipJson(QStringLiteral("/gone/clip.mp4"), 0, 4 * kSec, 0, 4 * kSec)});
        request.relinkChoices.insert(QStringLiteral("/gone/clip.mp4"), QString());
        const ImportOutcome out = runFilmoraImport(request);
        QCOMPARE(out.status, ImportOutcome::Status::Ok);
        QCOMPARE(out.built.missingMedia.size(), 1);
    }
};

QTEST_GUILESS_MAIN(TstWfpImport)
#include "tst_wfpimport.moc"
