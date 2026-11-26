// Dafin: the strict .wfp reader (WfpArchive) and the Filmora importer's compatibility report.
//
// No GUI, no GL, no media, no network. Everything is built here or comes from WfpFixtures.h (the
// 14 synthetic fixtures of the Stage 21 Pass 4 research, none of them a Filmora file). The six
// malformed archives are only ever read by WfpArchive / filmora::parse inside this test, from a
// temporary folder, never by the application.
//
// Evidence class of what passes here: DAFIN_NATIVE for the reader and the report on synthetic
// archives. It says nothing about real Filmora exports (none were available) nor about how a
// Filmora project looks or sounds once imported.
#include "TestZip.h"
#include "WfpFixtures.h"

#include "core/base/ZipArchive.h"
#include "core/interchange/ImportedTimelineProject.h"
#include "core/interchange/filmora/FilmoraProjectParser.h"
#include "core/interchange/filmora/WfpArchive.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QtTest>

#include <zlib.h>

#include <cstring>
#include <optional>

using namespace drift::interchange;
using namespace drift::interchange::filmora;

namespace {

// --- a zip builder with every field open to tampering -------------------------------------------

void put16(QByteArray &b, quint16 v)
{
    b.append(char(v & 0xff));
    b.append(char(v >> 8));
}

void put32(QByteArray &b, quint32 v)
{
    put16(b, quint16(v & 0xffff));
    put16(b, quint16(v >> 16));
}

QByteArray deflateRaw(const QByteArray &data)
{
    z_stream strm;
    memset(&strm, 0, sizeof(strm));
    if (deflateInit2(&strm, Z_DEFAULT_COMPRESSION, Z_DEFLATED, -MAX_WBITS, 8, Z_DEFAULT_STRATEGY) != Z_OK)
        return {};
    QByteArray out(int(deflateBound(&strm, uLong(data.size()))) + 16, Qt::Uninitialized);
    strm.next_in = reinterpret_cast<Bytef *>(const_cast<char *>(data.constData()));
    strm.avail_in = uInt(data.size());
    strm.next_out = reinterpret_cast<Bytef *>(out.data());
    strm.avail_out = uInt(out.size());
    deflate(&strm, Z_FINISH);
    out.resize(int(strm.total_out));
    deflateEnd(&strm);
    return out;
}

quint32 crcOf(const QByteArray &data)
{
    return quint32(crc32(crc32(0L, Z_NULL, 0), reinterpret_cast<const Bytef *>(data.constData()), uInt(data.size())));
}

struct Spec
{
    QByteArray name;
    QByteArray data;
    quint16 method = 8; // 0 stored, 8 deflated
    quint16 flags = 0;
    bool dataDescriptor = false;
    std::optional<QByteArray> rawPayload; // stored in place of the (deflated) data
    std::optional<quint32> crc;
    std::optional<quint32> centralCompressed, centralUncompressed, localCompressed, localUncompressed;
    std::optional<QByteArray> localName;
    std::optional<quint32> centralLocalOffset;
    QByteArray centralExtra;
};

struct Layout
{
    quint16 eocdDisk = 0;
    bool zip64Locator = false;
    QByteArray comment;
    std::optional<quint32> cdOffset;
    std::optional<quint16> totalEntries;
};

QByteArray buildZip(const QList<Spec> &specs, const Layout &layout = {})
{
    QByteArray out;
    QByteArray central;
    for (const Spec &s : specs) {
        const QByteArray payload = s.rawPayload ? *s.rawPayload : (s.method == 8 ? deflateRaw(s.data) : s.data);
        const quint32 crc = s.crc.value_or(crcOf(s.data));
        const quint32 cC = s.centralCompressed.value_or(quint32(payload.size()));
        const quint32 cU = s.centralUncompressed.value_or(quint32(s.data.size()));
        const quint32 lC = s.localCompressed.value_or(cC);
        const quint32 lU = s.localUncompressed.value_or(cU);
        const QByteArray localName = s.localName.value_or(s.name);
        const quint16 flags = quint16(s.flags | (s.dataDescriptor ? 0x0008 : 0));
        const quint32 offset = quint32(out.size());

        put32(out, 0x04034b50);
        put16(out, 20);
        put16(out, flags);
        put16(out, s.method);
        put16(out, 0);
        put16(out, 0);
        put32(out, s.dataDescriptor ? 0 : crc);
        put32(out, s.dataDescriptor ? 0 : lC);
        put32(out, s.dataDescriptor ? 0 : lU);
        put16(out, quint16(localName.size()));
        put16(out, 0);
        out.append(localName);
        out.append(payload);
        if (s.dataDescriptor) {
            put32(out, crc);
            put32(out, cC);
            put32(out, cU);
        }

        put32(central, 0x02014b50);
        put16(central, 20);
        put16(central, 20);
        put16(central, flags);
        put16(central, s.method);
        put16(central, 0);
        put16(central, 0);
        put32(central, crc);
        put32(central, cC);
        put32(central, cU);
        put16(central, quint16(s.name.size()));
        put16(central, quint16(s.centralExtra.size()));
        put16(central, 0);
        put16(central, 0);
        put16(central, 0);
        put32(central, 0);
        put32(central, s.centralLocalOffset.value_or(offset));
        central.append(s.name);
        central.append(s.centralExtra);
    }
    const quint32 cdOffset = quint32(out.size());
    out.append(central);
    if (layout.zip64Locator) {
        put32(out, 0x07064b50);
        out.append(QByteArray(16, '\0'));
    }
    put32(out, 0x06054b50);
    put16(out, layout.eocdDisk);
    put16(out, 0);
    put16(out, quint16(specs.size()));
    put16(out, layout.totalEntries.value_or(quint16(specs.size())));
    put32(out, quint32(central.size()));
    put32(out, layout.cdOffset.value_or(cdOffset));
    put16(out, quint16(layout.comment.size()));
    out.append(layout.comment);
    return out;
}

Spec member(const QByteArray &name, const QByteArray &data, quint16 method = 8)
{
    Spec s;
    s.name = name;
    s.data = data;
    s.method = method;
    return s;
}

QJsonObject trackWithClip(int trackType, int tag, const QJsonObject &clip)
{
    return QJsonObject{{QStringLiteral("trackType"), trackType},
                       {QStringLiteral("trackTag"), tag},
                       {QStringLiteral("clipList"), QJsonArray{clip}}};
}

QJsonObject videoClip(const QString &file, double begin, double end, double inPoint, double outPoint)
{
    return QJsonObject{{QStringLiteral("filename"), file},
                       {QStringLiteral("tlBegin"), begin},
                       {QStringLiteral("tlEnd"), end},
                       {QStringLiteral("inPoint"), inPoint},
                       {QStringLiteral("outPoint"), outPoint},
                       {QStringLiteral("type"), 1}};
}

QByteArray timelineJson(const QJsonArray &tracks, int fpsNum = 25, int fpsDen = 1)
{
    const QJsonObject timeline{{QStringLiteral("frameRate"),
                                QJsonObject{{QStringLiteral("num"), fpsNum}, {QStringLiteral("den"), fpsDen}}},
                               {QStringLiteral("resolutionWidth"), 1920},
                               {QStringLiteral("resolutionHeight"), 1080},
                               {QStringLiteral("sampleRate"), 48000},
                               {QStringLiteral("trackInfos"), tracks},
                               {QStringLiteral("timelineId"), 10}};
    return QJsonDocument(QJsonObject{{QStringLiteral("currentTimelineId"), 10},
                                     {QStringLiteral("timelineInfos"), QJsonArray{timeline}}})
        .toJson(QJsonDocument::Compact);
}

QByteArray infoJson()
{
    return QByteArray("{\"project_file_name\":\"built\",\"timeline_mediaId\":\"123\"}");
}

QList<Spec> projectMembers(const QByteArray &timeline)
{
    return {member("ProjectFolder/project_info.json", infoJson()),
            member("ProjectFolder/Medias/123/timeline.wesproj", timeline)};
}

const WfpFixture &fixture(const QString &name)
{
    for (const WfpFixture &f : kWfpFixtures) {
        if (name == QLatin1String(f.name))
            return f;
    }
    qFatal("no fixture %s", qPrintable(name));
}

} // namespace

class TstWfpSafe : public QObject
{
    Q_OBJECT

    QTemporaryDir m_dir;

    QString write(const QString &name, const QByteArray &bytes)
    {
        const QString path = m_dir.filePath(name);
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size())
            qFatal("cannot write %s", qPrintable(path));
        return path;
    }

    QString writeFixture(const QString &name)
    {
        const WfpFixture &f = fixture(name);
        return write(name, QByteArray(reinterpret_cast<const char *>(f.data), f.size));
    }

    // Opens `bytes` with `limits`; returns the open() status and leaves the archive open on success.
    static WfpStatus openStatus(const QString &path, const WfpLimits &limits = {})
    {
        WfpArchive archive(path, limits);
        WfpStatus status;
        archive.open(&status);
        return status;
    }

    // The status of reading the first member of a built archive.
    WfpStatus readFirst(const QList<Spec> &specs, const WfpLimits &limits = {}, const Layout &layout = {})
    {
        const QString path = write(QStringLiteral("built.wfp"), buildZip(specs, layout));
        WfpArchive archive(path, limits);
        WfpStatus status;
        if (!archive.open(&status))
            return status;
        QByteArray out;
        archive.read(archive.members().first(), &out, &status);
        return status;
    }

private slots:
    void initTestCase() { QVERIFY(m_dir.isValid()); }

    // --- the fixtures are what the pack lists --------------------------------------------------

    void embeddedFixturesMatchThePackIndex_data()
    {
        QTest::addColumn<QString>("name");
        for (const WfpFixture &f : kWfpFixtures)
            QTest::newRow(f.name) << QString::fromLatin1(f.name);
    }

    void embeddedFixturesMatchThePackIndex()
    {
        QFETCH(QString, name);
        const WfpFixture &f = fixture(name);
        const QByteArray bytes(reinterpret_cast<const char *>(f.data), f.size);
        QCOMPARE(QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex()),
                 QString::fromLatin1(f.sha256));
    }

    // --- the six malformed fixtures ------------------------------------------------------------

    void malformedFixturesAreRefused_data()
    {
        QTest::addColumn<QString>("name");
        QTest::addColumn<QString>("stage"); // "open": refused by open(); "verify": by reading the data
        QTest::addColumn<QString>("key");
        QTest::newRow("duplicate names") << "negative_duplicate_metadata.zip" << "open" << "duplicate_casefold_member";
        QTest::newRow("forged 2 GiB size") << "negative_forged_2gib.zip" << "open" << "declared_uncompressed_bytes";
        QTest::newRow("local name differs") << "negative_local_name_mismatch.zip" << "open"
                                            << "local_central_name_mismatch";
        QTest::newRow("path traversal") << "negative_path_traversal.zip" << "open" << "unsafe_member_path";
        QTest::newRow("truncated deflate") << "negative_truncated_deflate.zip" << "verify" << "incomplete_deflate_stream";
        QTest::newRow("wrong crc") << "negative_wrong_crc.zip" << "verify" << "crc32_mismatch";
    }

    void malformedFixturesAreRefused()
    {
        QFETCH(QString, name);
        QFETCH(QString, stage);
        QFETCH(QString, key);
        const QString path = writeFixture(name);
        WfpArchive archive(path);
        WfpStatus status;
        if (stage == QLatin1String("open")) {
            QVERIFY(!archive.open(&status));
        } else {
            QVERIFY2(archive.open(&status), qPrintable(status.key()));
            QVERIFY(!archive.verifyAll(&status));
        }
        QCOMPARE(status.key(), key);
        QVERIFY(status.isIntegrityFailure());
        QVERIFY(!status.message().isEmpty());
        // Nothing the reader refused can be handed out afterwards.
        QVERIFY(archive.members().isEmpty() || stage == QLatin1String("verify"));
    }

    void malformedFixturesFailGracefullyThroughParse_data() { malformedFixturesAreRefused_data(); }

    void malformedFixturesFailGracefullyThroughParse()
    {
        QFETCH(QString, name);
        // As a .wfp, the way a hostile file would reach the importer, in this process only.
        const WfpFixture &f = fixture(name);
        const QString path = write(name + QStringLiteral(".wfp"),
                                   QByteArray(reinterpret_cast<const char *>(f.data), f.size));
        QString error;
        const auto timeline = filmora::parse(path, &error);
        QVERIFY(!timeline.has_value());
        QVERIFY(!error.isEmpty());
    }

    // --- the valid fixtures: reader and report -------------------------------------------------

    void validFixturesOpenAndVerify_data()
    {
        QTest::addColumn<QString>("name");
        for (const WfpFixture &f : kWfpFixtures) {
            if (QString::fromLatin1(f.name).endsWith(QLatin1String(".wfp")))
                QTest::newRow(f.name) << QString::fromLatin1(f.name);
        }
    }

    void validFixturesOpenAndVerify()
    {
        QFETCH(QString, name);
        WfpArchive archive(writeFixture(name));
        WfpStatus status;
        QVERIFY2(archive.open(&status), qPrintable(status.key()));
        QVERIFY2(archive.verifyAll(&status), qPrintable(status.key()));
        QVERIFY(archive.find(QStringLiteral("ProjectFolder/project_info.json")));
        // Lookups ignore case and slash direction, as the old reader's did.
        QVERIFY(archive.find(QStringLiteral("projectfolder\\PROJECT_INFO.JSON")));
    }

    void basicFixtureIsPreservedWithoutLoss()
    {
        QString error;
        const auto t = filmora::parse(writeFixture(QStringLiteral("basic_constant_25fps.wfp")), &error);
        QVERIFY2(t.has_value(), qPrintable(error));
        QCOMPARE(t->tracks.size(), 2);
        QCOMPARE(t->fps, 25.0);
        QVERIFY(!t->report.hasLoss());
        QCOMPARE(t->report.count(CompatStatus::Preserved), 3); // frame rate + two clips
        QCOMPARE(t->skippedClips, 0);
    }

    void attachedEffectIsReportedAsOmitted()
    {
        QString error;
        const auto t = filmora::parse(writeFixture(QStringLiteral("attached_effect_success_status.wfp")), &error);
        QVERIFY2(t.has_value(), qPrintable(error));
        QCOMPARE(t->tracks.size(), 1);
        QCOMPARE(t->tracks.first().clips.size(), 1); // the media is kept…
        QCOMPARE(t->skippedClips, 0);                // …which is why the old counts said "all fine"
        QVERIFY(t->report.hasLoss());
        QCOMPARE(t->report.count(CompatStatus::Omitted), 1);
        const CompatEntry e = t->report.entries.at(1); // [0] is the frame rate
        QCOMPARE(e.construct, QStringLiteral("attached_effect"));
        QCOMPARE(e.status, CompatStatus::Omitted);
        QCOMPARE(e.sourceId, QStringLiteral("10:video:0:0"));
        QCOMPARE(e.sourceTimeUs, int64_t(0));
        QVERIFY(e.reason.contains(QStringLiteral("synthetic-blur")));
        // and the message the person reads says so
        const ProjectBuildResult built = toProject(*t, QStringLiteral("x.wfp"));
        QVERIFY(summary(*t, built, QStringLiteral("Filmora")).contains(QStringLiteral("effects attached")));
    }

    void fractionalFrameRateIsReportedAsRoundedTo30()
    {
        QString error;
        const auto t = filmora::parse(writeFixture(QStringLiteral("fractional_2997fps.wfp")), &error);
        QVERIFY2(t.has_value(), qPrintable(error));
        QCOMPARE(t->fpsNum, 30000);
        QCOMPARE(t->fpsDen, 1001);
        QVERIFY(qAbs(t->fps - 30000.0 / 1001.0) < 1e-9);
        QVERIFY(t->report.hasLoss());
        const CompatEntry e = t->report.entries.first();
        QCOMPARE(e.construct, QStringLiteral("frame_rate"));
        QCOMPARE(e.status, CompatStatus::Approximated);
        QVERIFY(e.reason.contains(QStringLiteral("30000/1001")));
        QVERIFY(e.applied.contains(QStringLiteral("30 fps")));
        // what the project builder actually does matches what was reported
        const ProjectBuildResult built = toProject(*t, QStringLiteral("x.wfp"));
        QCOMPARE(built.project.fps(), 30);
        QCOMPARE(projectFpsFor(t->fps), 30);
        QVERIFY(summary(*t, built, QStringLiteral("Filmora")).contains(QStringLiteral("30 fps")));
    }

    void rampAndAttachedEffectAreReportedSeparately()
    {
        QString error;
        const auto t = filmora::parse(writeFixture(QStringLiteral("ramp_and_attached_effect.wfp")), &error);
        QVERIFY2(t.has_value(), qPrintable(error));
        QCOMPARE(t->tracks.first().clips.first().speed, 2.0); // the 2x scalar: (80 - 0) / (40 - 0) s
        QCOMPARE(t->report.count(CompatStatus::Approximated), 1);
        QCOMPARE(t->report.count(CompatStatus::Omitted), 1);
        QStringList constructs;
        for (const CompatEntry &e : t->report.entries)
            constructs << e.construct + QLatin1Char('/') + compatStatusKey(e.status);
        QVERIFY(constructs.contains(QStringLiteral("speed_curve/approximated")));
        QVERIFY(constructs.contains(QStringLiteral("attached_effect/omitted")));
        for (const CompatEntry &e : t->report.entries) {
            if (e.construct == QLatin1String("speed_curve")) {
                QVERIFY(e.reason.contains(QStringLiteral("0.5")));
                QVERIFY(e.reason.contains(QStringLiteral("3.5")));
                QVERIFY(e.applied.contains(QStringLiteral("2")));
            }
        }
        QVERIFY(t->notes.contains(QStringLiteral("Speed ramps became constant speeds."))); // unchanged
    }

    void currentTimelineIdIsHonouredAndTheOtherIsReported()
    {
        QString error;
        const auto t = filmora::parse(writeFixture(QStringLiteral("multi_timeline_current_id.wfp")), &error);
        QVERIFY2(t.has_value(), qPrintable(error));
        QVERIFY(t->tracks.first().clips.first().path.endsWith(QStringLiteral("T2.mp4")));
        QCOMPARE(t->fps, 30.0);
        QCOMPARE(t->report.count(CompatStatus::Omitted), 1);
        for (const CompatEntry &e : t->report.entries) {
            if (e.status == CompatStatus::Omitted) {
                QCOMPARE(e.construct, QStringLiteral("other_timeline"));
                QCOMPARE(e.sourceId, QStringLiteral("timeline:21"));
            }
        }
    }

    void titleTrackIsReportedAsUnsupported()
    {
        QString error;
        const auto t = filmora::parse(writeFixture(QStringLiteral("unsupported_title_track.wfp")), &error);
        QVERIFY2(t.has_value(), qPrintable(error));
        QCOMPARE(t->skippedClips, 1);
        QCOMPARE(t->report.count(CompatStatus::Unsupported), 1);
        for (const CompatEntry &e : t->report.entries) {
            if (e.status == CompatStatus::Unsupported) {
                QCOMPARE(e.construct, QStringLiteral("non_media_clip"));
                QCOMPARE(e.sourceId, QStringLiteral("10:type3:1:0"));
                QCOMPARE(e.sourceTimeUs, int64_t(0));
            }
        }
    }

    void audioFollowerStillTakesTheVideosSound()
    {
        QString error;
        const auto t = filmora::parse(writeFixture(QStringLiteral("video_audio_follower.wfp")), &error);
        QVERIFY2(t.has_value(), qPrintable(error));
        QCOMPARE(t->tracks.size(), 2);
        QVERIFY(!t->tracks.first().clips.first().keepsOwnAudio);
        QVERIFY(!t->report.hasLoss());
    }

    void emptyTimelineInfosIsAnErrorNotACrash()
    {
        QString error;
        const auto t = filmora::parse(writeFixture(QStringLiteral("empty_timeline_infos.wfp")), &error);
        QVERIFY(!t.has_value());
        QVERIFY(error.contains(QStringLiteral("no timeline")));
    }

    void missingMediaBecomesAMissingDependencyEntry()
    {
        QString error;
        const auto t = filmora::parse(writeFixture(QStringLiteral("basic_constant_25fps.wfp")), &error);
        QVERIFY2(t.has_value(), qPrintable(error));
        const ProjectBuildResult built = toProject(*t, m_dir.filePath(QStringLiteral("basic_constant_25fps.wfp")));
        QCOMPARE(built.missingMedia.size(), 2);
        QCOMPARE(built.report.count(CompatStatus::MissingDependency), 2);
        QCOMPARE(built.report.entries.first().construct, QStringLiteral("media_file"));
        QCOMPARE(built.report.entries.first().sourceId, QStringLiteral("10:video:0:0"));
        QVERIFY(built.report.hasLoss());
    }

    void projectFpsRuleIsOneFunction()
    {
        QCOMPARE(projectFpsFor(30000.0 / 1001.0), 30);
        QCOMPARE(projectFpsFor(24000.0 / 1001.0), 24);
        QCOMPARE(projectFpsFor(25.0), 25);
        QCOMPARE(projectFpsFor(1000.0), 240);
        QCOMPARE(projectFpsFor(0.1), 1);
    }

    // --- the reader's own rules, on archives built here -----------------------------------------

    void roundTripStoredAndDeflated()
    {
        const QByteArray text = QByteArray(2000, 'x') + "tail";
        for (const quint16 method : {quint16(0), quint16(8)}) {
            const QString path = write(QStringLiteral("rt.wfp"), buildZip({member("a/b.txt", text, method)}));
            WfpArchive archive(path);
            WfpStatus status;
            QVERIFY2(archive.open(&status), qPrintable(status.key()));
            QByteArray out;
            QVERIFY2(archive.read(archive.members().first(), &out, &status), qPrintable(status.key()));
            QCOMPARE(out, text);
            QCOMPARE(archive.inflatedBytes(), qint64(text.size()));
        }
    }

    void dataDescriptorEntriesAreRead()
    {
        Spec s = member("d.json", "{\"a\":1}");
        s.dataDescriptor = true;
        QVERIFY(readFirst({s}).ok());
        s.method = 0;
        QVERIFY(readFirst({s}).ok());
    }

    void emptyMembersAreRead()
    {
        QVERIFY(readFirst({member("e.json", "", 0)}).ok());
        QVERIFY(readFirst({member("e.json", "", 8)}).ok());
        Spec none = member("e.json", "", 8);
        none.rawPayload = QByteArray(); // method 8 with no stream at all
        QVERIFY(readFirst({none}).ok());
    }

    void declaredSizeOverTheLimitIsRefusedBeforeAnyAllocation()
    {
        WfpLimits limits;
        limits.maxMemberBytes = 50;
        QCOMPARE(readFirst({member("big.bin", QByteArray(100, 'a'), 0)}, limits).key(),
                 QStringLiteral("declared_uncompressed_bytes"));
        // A size deflate could really produce from 2 MB, claimed as nearly 2 GB, with the default
        // limits: refused at read, nothing inflated.
        Spec s = member("bomb.bin", "x");
        s.rawPayload = QByteArray(2'000'000, '\0');
        s.centralUncompressed = 2'000'000'000u;
        const QString path = write(QStringLiteral("bomb.wfp"), buildZip({s}));
        WfpArchive archive(path);
        WfpStatus status;
        QVERIFY2(archive.open(&status), qPrintable(status.key()));
        QByteArray out;
        QVERIFY(!archive.read(archive.members().first(), &out, &status));
        QCOMPARE(status.key(), QStringLiteral("declared_uncompressed_bytes"));
        QVERIFY(out.isEmpty());
        QCOMPARE(archive.inflatedBytes(), qint64(0));
    }

    void sizeThatDeflateCannotProduceIsRefusedAtOpen()
    {
        Spec s = member("lie.bin", "x");
        s.rawPayload = QByteArray(10, '\0');
        s.centralUncompressed = 1'000'000u; // 10 bytes cannot inflate to 1 MB
        QCOMPARE(openStatus(write(QStringLiteral("lie.wfp"), buildZip({s}))).key(),
                 QStringLiteral("declared_uncompressed_bytes"));
    }

    void storedMemberWithTwoSizesIsRefused()
    {
        Spec s = member("s.bin", QByteArray(20, 'a'), 0);
        s.centralUncompressed = 10;
        QCOMPARE(openStatus(write(QStringLiteral("s.wfp"), buildZip({s}))).key(), QStringLiteral("size_inconsistent"));
    }

    void totalInflationIsOneBudget()
    {
        WfpLimits limits;
        limits.maxMemberBytes = 50;
        limits.maxTotalInflatedBytes = 60;
        const QString path = write(QStringLiteral("two.wfp"),
                                   buildZip({member("a.bin", QByteArray(40, 'a'), 0), member("b.bin", QByteArray(40, 'b'), 0)}));
        WfpArchive archive(path, limits);
        WfpStatus status;
        QVERIFY2(archive.open(&status), qPrintable(status.key()));
        QByteArray out;
        QVERIFY(archive.read(archive.members().at(0), &out, &status));
        QVERIFY(!archive.read(archive.members().at(1), &out, &status));
        QCOMPARE(status.key(), QStringLiteral("total_inflation_limit"));
        // checking the archive is not reading it: it does not draw on the budget
        WfpArchive again(path, limits);
        QVERIFY(again.open(&status));
        QVERIFY(again.verifyAll(&status));
        QCOMPARE(again.inflatedBytes(), qint64(0));
    }

    void inflatingBeyondTheDeclaredSizeIsRefused()
    {
        Spec s = member("o.bin", QByteArray(100, 'x'));
        s.centralUncompressed = 50;
        QCOMPARE(readFirst({s}).key(), QStringLiteral("size_mismatch"));
    }

    void inflatingShortOfTheDeclaredSizeIsRefused()
    {
        Spec s = member("o.bin", QByteArray(100, 'x'));
        s.centralUncompressed = 200;
        QCOMPARE(readFirst({s}).key(), QStringLiteral("size_mismatch"));
    }

    void bytesAfterTheDeflateEndAreRefused()
    {
        Spec s = member("o.bin", QByteArray(100, 'x'));
        s.rawPayload = deflateRaw(s.data) + "JUNK";
        QCOMPARE(readFirst({s}).key(), QStringLiteral("size_mismatch"));
    }

    void aStreamThatEndsEarlyIsRefusedEvenWhenOutputIsComplete()
    {
        Spec s = member("o.bin", QByteArray(100, 'x'));
        QByteArray payload = deflateRaw(s.data);
        payload.chop(1);
        s.rawPayload = payload;
        QCOMPARE(readFirst({s}).key(), QStringLiteral("incomplete_deflate_stream"));
    }

    void garbageDeflateIsRefused()
    {
        Spec s = member("o.bin", QByteArray(100, 'x'));
        s.rawPayload = QByteArray(40, '\xff');
        s.centralUncompressed = 100;
        const QString key = readFirst({s}).key();
        QVERIFY2(key == QLatin1String("corrupt_deflate_stream") || key == QLatin1String("incomplete_deflate_stream"),
                 qPrintable(key));
    }

    void wrongCrcIsRefusedForStoredToo()
    {
        Spec s = member("c.bin", "hello", 0);
        s.crc = 1234;
        s.centralUncompressed = 5;
        QCOMPARE(readFirst({s}).key(), QStringLiteral("crc32_mismatch"));
    }

    void zip64IsRefused()
    {
        Spec big = member("a.bin", "abc", 0);
        big.centralCompressed = 0xFFFFFFFFu;
        QCOMPARE(openStatus(write(QStringLiteral("z.wfp"), buildZip({big}))).key(), QStringLiteral("zip64_unsupported"));

        Spec extra = member("a.bin", "abc", 0);
        extra.centralExtra = QByteArray::fromHex("01000000"); // ZIP64 extra field, id 1, no body
        QCOMPARE(openStatus(write(QStringLiteral("z.wfp"), buildZip({extra}))).key(), QStringLiteral("zip64_unsupported"));

        Layout locator;
        locator.zip64Locator = true;
        QCOMPARE(openStatus(write(QStringLiteral("z.wfp"), buildZip({member("a.bin", "abc", 0)}, locator))).key(),
                 QStringLiteral("zip64_unsupported"));

        Layout sentinel;
        sentinel.totalEntries = 0xFFFF;
        QCOMPARE(openStatus(write(QStringLiteral("z.wfp"), buildZip({member("a.bin", "abc", 0)}, sentinel))).key(),
                 QStringLiteral("zip64_unsupported"));
    }

    void multiDiskIsRefused()
    {
        Layout layout;
        layout.eocdDisk = 1;
        QCOMPARE(openStatus(write(QStringLiteral("m.wfp"), buildZip({member("a.bin", "abc", 0)}, layout))).key(),
                 QStringLiteral("multi_disk"));
    }

    void directoryOutOfPlaceIsRefused()
    {
        Layout layout;
        layout.cdOffset = 5;
        QCOMPARE(openStatus(write(QStringLiteral("d.wfp"), buildZip({member("a.bin", "abc", 0)}, layout))).key(),
                 QStringLiteral("central_directory_invalid"));
    }

    void tooManyEntriesAndTooLargeArchive()
    {
        WfpLimits limits;
        limits.maxEntries = 2;
        const QString three = write(QStringLiteral("t.wfp"),
                                    buildZip({member("a", "1", 0), member("b", "2", 0), member("c", "3", 0)}));
        QCOMPARE(openStatus(three, limits).key(), QStringLiteral("too_many_entries"));
        WfpLimits small;
        small.maxArchiveBytes = 100;
        QCOMPARE(openStatus(three, small).key(), QStringLiteral("archive_too_large"));
    }

    void overlappingMembersAreRefused()
    {
        Spec a = member("a.bin", QByteArray(100, 'a'), 0);
        a.centralCompressed = a.centralUncompressed = 150; // claims to run into the next member
        QCOMPARE(openStatus(write(QStringLiteral("ov.wfp"), buildZip({a, member("b.bin", QByteArray(100, 'b'), 0)}))).key(),
                 QStringLiteral("overlapping_members"));
    }

    void localAndCentralMetadataMustAgree()
    {
        Spec s = member("a.bin", QByteArray(10, 'a'), 0);
        s.localUncompressed = s.localCompressed = 11;
        QCOMPARE(openStatus(write(QStringLiteral("l.wfp"), buildZip({s}))).key(),
                 QStringLiteral("local_central_metadata_mismatch"));
        Spec crc = member("a.bin", QByteArray(10, 'a'), 0);
        QByteArray bytes = buildZip({crc});
        bytes[14] = char(bytes[14] ^ 0x55); // the local header's CRC
        QCOMPARE(openStatus(write(QStringLiteral("l.wfp"), bytes)).key(), QStringLiteral("local_central_metadata_mismatch"));
        QByteArray sig = buildZip({member("a.bin", "abc", 0)});
        sig[0] = 'X';
        QCOMPARE(openStatus(write(QStringLiteral("l.wfp"), sig)).key(), QStringLiteral("local_header_invalid"));
    }

    void unsafeNamesAreRefused_data()
    {
        QTest::addColumn<QByteArray>("name");
        QTest::newRow("absolute") << QByteArray("/abs.json");
        QTest::newRow("absolute backslash") << QByteArray("\\abs.json");
        QTest::newRow("drive") << QByteArray("C:/x.json");
        QTest::newRow("stream") << QByteArray("x.json:stream");
        QTest::newRow("parent") << QByteArray("a/../b.json");
        QTest::newRow("parent first") << QByteArray("../x.json");
        QTest::newRow("parent backslash") << QByteArray("a\\..\\b.json");
        QTest::newRow("empty component") << QByteArray("a//b.json");
        QTest::newRow("dot component") << QByteArray("./x.json");
        QTest::newRow("control") << QByteArray("a\x01" "b.json");
        QTest::newRow("nul") << QByteArray("a\0b.json", 8);
        QTest::newRow("too long") << QByteArray(2000, 'a');
    }

    void unsafeNamesAreRefused()
    {
        QFETCH(QByteArray, name);
        Spec s = member(name, "abc", 0);
        QCOMPARE(openStatus(write(QStringLiteral("n.wfp"), buildZip({s}))).key(), QStringLiteral("unsafe_member_path"));
    }

    void duplicatesAreFoundAfterCanonicalisation_data()
    {
        QTest::addColumn<QByteArray>("first");
        QTest::addColumn<QByteArray>("second");
        QTest::newRow("case") << QByteArray("A/B.json") << QByteArray("a/b.json");
        QTest::newRow("slash direction") << QByteArray("A/B.json") << QByteArray("a\\b.json");
        QTest::newRow("folder and file") << QByteArray("dir/") << QByteArray("DIR");
        QTest::newRow("non-ASCII case") << QByteArray("\xC3\x89t\xC3\xA9.json") << QByteArray("\xC3\xA9T\xC3\x89.json");
    }

    void duplicatesAreFoundAfterCanonicalisation()
    {
        QFETCH(QByteArray, first);
        QFETCH(QByteArray, second);
        QCOMPARE(openStatus(write(QStringLiteral("dup.wfp"),
                                  buildZip({member(first, "1", 0), member(second, "2", 0)})))
                     .key(),
                 QStringLiteral("duplicate_casefold_member"));
    }

    void namesInOtherEncodingsAreKeptApart()
    {
        // Not UTF-8: read as Latin-1, so two different byte strings stay two different names.
        const QString path = write(QStringLiteral("enc.wfp"),
                                   buildZip({member("caf\xE9.json", "1", 0), member("caf\xE8.json", "2", 0)}));
        WfpArchive archive(path);
        WfpStatus status;
        QVERIFY2(archive.open(&status), qPrintable(status.key()));
        QCOMPARE(archive.members().size(), 2);
    }

    void encryptedAndUnknownMethodsAreRefusedAtRead()
    {
        Spec enc = member("e.bin", "abc", 0);
        enc.flags = 0x0001;
        QCOMPARE(readFirst({enc}).key(), QStringLiteral("encrypted_member"));
        Spec method = member("m.bin", "abc", 0);
        method.method = 12;
        method.rawPayload = QByteArray("abc");
        QCOMPARE(readFirst({method}).key(), QStringLiteral("unsupported_method"));
    }

    void notAZipIsRefused()
    {
        QCOMPARE(openStatus(write(QStringLiteral("x.wfp"), QByteArray(100, 'z'))).key(), QStringLiteral("not_zip"));
        QCOMPARE(openStatus(write(QStringLiteral("x.wfp"), QByteArray("PK"))).key(), QStringLiteral("not_zip"));
        QByteArray trailing = buildZip({member("a.bin", "abc", 0)});
        trailing.append('!'); // the record no longer ends at the end of the file
        QCOMPARE(openStatus(write(QStringLiteral("x.wfp"), trailing)).key(), QStringLiteral("not_zip"));
        QByteArray cut = buildZip({member("a.bin", "abc", 0)});
        cut.chop(4);
        QCOMPARE(openStatus(write(QStringLiteral("x.wfp"), cut)).key(), QStringLiteral("not_zip"));
        QCOMPARE(openStatus(m_dir.path()).key(), QStringLiteral("cannot_open"));
        QCOMPARE(openStatus(m_dir.filePath(QStringLiteral("nothing.wfp"))).key(), QStringLiteral("cannot_open"));
    }

    void jsonIsBoundedAndMustBeAnObject()
    {
        WfpLimits limits;
        limits.maxJsonDepth = 8;
        const auto jsonStatus = [&](const QByteArray &json, QJsonObject *object = nullptr) {
            const QString path = write(QStringLiteral("j.wfp"), buildZip({member("j.json", json)}));
            WfpArchive archive(path, limits);
            WfpStatus status;
            if (!archive.open(&status))
                return status;
            QJsonObject local;
            archive.readJsonObject(archive.members().first(), object ? object : &local, &status);
            return status;
        };
        QCOMPARE(jsonStatus("[[[[[[[[[1]]]]]]]]]").key(), QStringLiteral("json_too_deep"));
        QCOMPARE(jsonStatus("{\"a\":{\"b\":{\"c\":{\"d\":{\"e\":{\"f\":{\"g\":{\"h\":{\"i\":1}}}}}}}}}").key(),
                 QStringLiteral("json_too_deep"));
        // brackets inside strings are not nesting
        QVERIFY(jsonStatus("{\"a\":\"[[[[[[[[[[[[[[[[[[ \\\" [[[[\"}").ok());
        QCOMPARE(jsonStatus("{").key(), QStringLiteral("json_invalid"));
        QCOMPARE(jsonStatus("[1]").key(), QStringLiteral("json_not_object"));
        QVERIFY(!jsonStatus("{").isIntegrityFailure()); // a syntax problem is not a damaged zip
        QJsonObject object;
        QVERIFY(jsonStatus("\xEF\xBB\xBF{\"a\":1}", &object).ok());
        QCOMPARE(object.value(QStringLiteral("a")).toInt(), 1);
    }

    // --- the importer on archives built here ----------------------------------------------------

    void parseStopsOnAMemberThatFailsItsCrc()
    {
        QList<Spec> specs = projectMembers(timelineJson(
            QJsonArray{trackWithClip(1, 1, videoClip(QStringLiteral("file:///x/a.mp4"), 0, 1e7, 0, 1e7))}));
        specs[1].crc = 99;
        QString error;
        QVERIFY(!filmora::parse(write(QStringLiteral("p.wfp"), buildZip(specs)), &error).has_value());
        QVERIFY2(error.contains(QStringLiteral("was not opened")), qPrintable(error));
    }

    void parseStopsOnJsonThatIsNestedTooDeep()
    {
        QByteArray deep = QByteArray(300, '[') + QByteArray(300, ']');
        const QList<Spec> specs = projectMembers("{\"timelineInfos\":[{\"x\":" + deep + "}]}");
        QString error;
        QVERIFY(!filmora::parse(write(QStringLiteral("p.wfp"), buildZip(specs)), &error).has_value());
        QVERIFY2(error.contains(QStringLiteral("nested")), qPrintable(error));
    }

    void parseStillToleratesAProjectInfoThatIsNotJson()
    {
        const QByteArray timeline = timelineJson(
            QJsonArray{trackWithClip(1, 1, videoClip(QStringLiteral("file:///x/a.mp4"), 0, 1e7, 0, 1e7))});
        const QList<Spec> specs = {member("ProjectFolder/project_info.json", "{not json"),
                                   member("ProjectFolder/Medias/9/timeline.wesproj", timeline)};
        QString error;
        const auto t = filmora::parse(write(QStringLiteral("p.wfp"), buildZip(specs)), &error);
        QVERIFY2(t.has_value(), qPrintable(error));
        QCOMPARE(t->tracks.size(), 1);
    }

    void parseFindsMembersIgnoringCaseAndSlashes()
    {
        const QByteArray timeline = timelineJson(
            QJsonArray{trackWithClip(1, 1, videoClip(QStringLiteral("file:///x/a.mp4"), 0, 1e7, 0, 1e7))});
        const QList<Spec> specs = {member("projectfolder\\PROJECT_INFO.JSON", infoJson()),
                                   member("PROJECTFOLDER\\medias\\123\\TIMELINE.WESPROJ", timeline)};
        QString error;
        const auto t = filmora::parse(write(QStringLiteral("p.wfp"), buildZip(specs)), &error);
        QVERIFY2(t.has_value(), qPrintable(error));
        QCOMPARE(t->tracks.size(), 1);
    }

    void parseNeverCastsAnOutOfRangeTime()
    {
        // 1e300 ticks does not fit any integer; casting it is undefined behaviour. It is a clip
        // left out, with a report entry that says why.
        const QByteArray timeline = timelineJson(QJsonArray{
            trackWithClip(1, 1, videoClip(QStringLiteral("file:///x/a.mp4"), 0, 1e300, 0, 1e7))});
        QString error;
        const auto t = filmora::parse(write(QStringLiteral("p.wfp"), buildZip(projectMembers(timeline))), &error);
        QVERIFY2(t.has_value(), qPrintable(error));
        QCOMPARE(t->tracks.first().clips.size(), 0);
        QCOMPARE(t->report.count(CompatStatus::Omitted), 1);
        bool found = false;
        for (const CompatEntry &e : t->report.entries) {
            if (e.construct == QLatin1String("empty_clip")) {
                found = true;
                QVERIFY(e.reason.contains(QStringLiteral("not valid numbers")));
            }
        }
        QVERIFY(found);
    }

    void unrecognisedEffectLayoutIsReportedAsUnknown()
    {
        QJsonObject clip = videoClip(QStringLiteral("file:///x/a.mp4"), 0, 1e7, 0, 1e7);
        clip.insert(QStringLiteral("effectChainList"), QJsonArray{QJsonObject{{QStringLiteral("mystery"), 1}}});
        const QByteArray timeline = timelineJson(QJsonArray{trackWithClip(1, 1, clip)});
        QString error;
        const auto t = filmora::parse(write(QStringLiteral("p.wfp"), buildZip(projectMembers(timeline))), &error);
        QVERIFY2(t.has_value(), qPrintable(error));
        QCOMPARE(t->report.count(CompatStatus::Unknown), 1);
        QVERIFY(t->report.hasLoss());
    }

    void emptyEffectChainsAreNotALoss()
    {
        QJsonObject clip = videoClip(QStringLiteral("file:///x/a.mp4"), 0, 1e7, 0, 1e7);
        clip.insert(QStringLiteral("effectChainList"), QJsonArray{QJsonObject{{QStringLiteral("effectList"), QJsonArray{}}}});
        const QByteArray timeline = timelineJson(QJsonArray{trackWithClip(1, 1, clip)});
        QString error;
        const auto t = filmora::parse(write(QStringLiteral("p.wfp"), buildZip(projectMembers(timeline))), &error);
        QVERIFY2(t.has_value(), qPrintable(error));
        QVERIFY(!t->report.hasLoss());
    }

    // Archives written by the writers Dafin's own tests and exports use must still open.
    void archivesFromDafinsOwnWritersStillOpen()
    {
        const QByteArray timeline = timelineJson(
            QJsonArray{trackWithClip(1, 1, videoClip(QStringLiteral("file:///x/a.mp4"), 0, 1e7, 0, 1e7))});
        // tests/TestZip.h (the helper tst_editorstate builds its .wfp with)
        const QString stored = m_dir.filePath(QStringLiteral("stored.wfp"));
        QVERIFY(writeStoredZip(stored, {{QStringLiteral("ProjectFolder/project_info.json"), infoJson()},
                                        {QStringLiteral("ProjectFolder/Medias/123/timeline.wesproj"), timeline}}));
        QString error;
        QVERIFY2(filmora::parse(stored, &error).has_value(), qPrintable(error));
        // drift::zip::Writer (library exports): stored entries, UTF-8 names, patched headers
        const QString written = m_dir.filePath(QStringLiteral("written.wfp"));
        {
            QFile out(written);
            QVERIFY(out.open(QIODevice::WriteOnly));
            drift::zip::Writer writer(&out);
            QVERIFY(writer.addData(QStringLiteral("ProjectFolder/project_info.json"), infoJson()));
            QVERIFY(writer.addData(QStringLiteral("ProjectFolder/Medias/123/timeline.wesproj"), timeline));
            QVERIFY(writer.finish());
        }
        QVERIFY2(filmora::parse(written, &error).has_value(), qPrintable(error));
        WfpArchive archive(written);
        WfpStatus status;
        QVERIFY2(archive.open(&status), qPrintable(status.key()));
        QVERIFY2(archive.verifyAll(&status), qPrintable(status.key()));
    }

    void reportSerialisesToJson()
    {
        QString error;
        const auto t = filmora::parse(writeFixture(QStringLiteral("ramp_and_attached_effect.wfp")), &error);
        QVERIFY2(t.has_value(), qPrintable(error));
        const QJsonObject json = t->report.toJson();
        QCOMPARE(json.value(QStringLiteral("entries")).toArray().size(), int(t->report.entries.size()));
        QCOMPARE(json.value(QStringLiteral("entries")).toArray().at(1).toObject().value(QStringLiteral("status")).toString(),
                 QStringLiteral("approximated"));
    }
};

QTEST_GUILESS_MAIN(TstWfpSafe)
#include "tst_wfpsafe.moc"
