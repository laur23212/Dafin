// Dafin: a GPU package pass must not sample the buffer it draws into (C10).
//
// Three things: the detector on hand-built pipelines, loadGpuPipeline's two policies, and an
// inventory of every shipped package manifest, which is the gate for flipping the default from
// Warn to Reject. No GL: this proves the DETECTION. Whether an aliased pass really glitches on a
// given driver was never reproduced and is NOT claimed here.
#include "engine/packages/GpuPackageParse.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QtTest>

#ifndef DAFIN_SOURCE_DIR
#error "DAFIN_SOURCE_DIR must name the repository root"
#endif

using namespace GpuPackageParse;

namespace {

QJsonObject source(int index = 0)
{
    return {{QStringLiteral("type"), QStringLiteral("source_texture")},
            {QStringLiteral("index"), index}};
}
QJsonObject bufferIn(const QString &id)
{
    return {{QStringLiteral("type"), QStringLiteral("buffer")}, {QStringLiteral("id"), id}};
}
QJsonObject bufferOut(const QString &id) { return bufferIn(id); }
QJsonObject canvasOut() { return {{QStringLiteral("type"), QStringLiteral("canvas")}}; }

QJsonObject pass(int index, const QJsonArray &inputs, const QJsonObject &output)
{
    return {{QStringLiteral("passIndex"), index},
            {QStringLiteral("fragmentShader"), QStringLiteral("main.frag")},
            {QStringLiteral("inputs"), inputs},
            {QStringLiteral("output"), output}};
}

QJsonObject package(const QJsonArray &bufferIds, const QJsonArray &passes)
{
    QJsonArray buffers;
    for (const QJsonValue &id : bufferIds)
        buffers.append(QJsonObject{{QStringLiteral("id"), id.toString()},
                                   {QStringLiteral("scale"), 1.0}});
    return {{QStringLiteral("pipeline"),
             QJsonObject{{QStringLiteral("intermediateBuffers"), buffers},
                         {QStringLiteral("passes"), passes}}}};
}

class Fixture
{
public:
    Fixture()
    {
        QFile f(m_dir.filePath(QStringLiteral("main.frag")));
        if (f.open(QIODevice::WriteOnly))
            f.write("void main() {}\n");
    }
    QString path() const { return m_dir.path(); }

    drift::GpuEffectDefinition load(const QJsonObject &root, QString *error, bool *ok,
                                    AliasPolicy policy = AliasPolicy::Warn) const
    {
        drift::GpuEffectDefinition def;
        *ok = loadGpuPipeline(root, path(), 0, &def, error, policy);
        return def;
    }

private:
    QTemporaryDir m_dir;
};

// Independent of the parser: the same rule read straight off the JSON.
int aliasedPassesInJson(const QJsonObject &root)
{
    const QJsonObject pipeline = root.value(QStringLiteral("pipeline")).toObject();
    int n = 0;
    for (const QJsonValue &pv : pipeline.value(QStringLiteral("passes")).toArray()) {
        const QJsonObject p = pv.toObject();
        const QJsonObject out = p.value(QStringLiteral("output")).toObject();
        if (out.value(QStringLiteral("type")).toString() != QLatin1String("buffer"))
            continue;
        for (const QJsonValue &iv : p.value(QStringLiteral("inputs")).toArray()) {
            const QJsonObject in = iv.toObject();
            if (in.value(QStringLiteral("type")).toString() == QLatin1String("buffer")
                && in.value(QStringLiteral("id")) == out.value(QStringLiteral("id")))
                ++n;
        }
    }
    return n;
}

} // namespace

class TestGpuPackageAlias : public QObject
{
    Q_OBJECT

private slots:
    void aPassThatSamplesItsOwnOutputIsFound()
    {
        Fixture fx;
        QString error;
        bool ok = false;
        const auto def = fx.load(package({QStringLiteral("a")},
                                         {pass(0, {source()}, bufferOut(QStringLiteral("a"))),
                                          pass(1, {bufferIn(QStringLiteral("a"))},
                                               bufferOut(QStringLiteral("a"))),
                                          pass(2, {bufferIn(QStringLiteral("a"))}, canvasOut())}),
                                 &error, &ok);
        QVERIFY2(ok, qPrintable(error));
        const QList<PassAlias> found = findAliasedPasses(def);
        QCOMPARE(found.size(), 1);
        QCOMPARE(found[0].passIndex, 1);
        QCOMPARE(found[0].inputIndex, 0);
        QCOMPARE(found[0].bufferId, QStringLiteral("a"));
        QVERIFY(describeAlias(found[0]).contains(QStringLiteral("pass 1")));
        QVERIFY(describeAlias(found[0]).contains(QStringLiteral("'a'")));
    }

    void anAliasedSecondInputIsReportedWithItsPosition()
    {
        Fixture fx;
        QString error;
        bool ok = false;
        const auto def = fx.load(
            package({QStringLiteral("a"), QStringLiteral("b")},
                    {pass(0, {source()}, bufferOut(QStringLiteral("a"))),
                     pass(1, {bufferIn(QStringLiteral("a")), bufferIn(QStringLiteral("b"))},
                          bufferOut(QStringLiteral("b"))),
                     pass(2, {bufferIn(QStringLiteral("b"))}, canvasOut())}),
            &error, &ok);
        QVERIFY2(ok, qPrintable(error));
        const QList<PassAlias> found = findAliasedPasses(def);
        QCOMPARE(found.size(), 1);
        QCOMPARE(found[0].inputIndex, 1);
        QCOMPARE(found[0].bufferId, QStringLiteral("b"));
    }

    void pingPongAndPlainChainsAreNotFlagged()
    {
        Fixture fx;
        QString error;
        bool ok = false;
        const auto def = fx.load(
            package({QStringLiteral("a"), QStringLiteral("b")},
                    {pass(0, {source()}, bufferOut(QStringLiteral("a"))),
                     pass(1, {bufferIn(QStringLiteral("a"))}, bufferOut(QStringLiteral("b"))),
                     pass(2, {bufferIn(QStringLiteral("b"))}, bufferOut(QStringLiteral("a"))),
                     pass(3, {bufferIn(QStringLiteral("a")), source()}, canvasOut())}),
            &error, &ok);
        QVERIFY2(ok, qPrintable(error));
        QVERIFY(findAliasedPasses(def).isEmpty());
        QVERIFY(def.warnings.isEmpty());
    }

    void aCanvasPassCannotAlias()
    {
        Fixture fx;
        QString error;
        bool ok = false;
        const auto def = fx.load(package({}, {pass(0, {source()}, canvasOut())}), &error, &ok);
        QVERIFY2(ok, qPrintable(error));
        QVERIFY(findAliasedPasses(def).isEmpty());
    }

    void warnLoadsAndRecordsTheSentence()
    {
        Fixture fx;
        QString error;
        bool ok = false;
        const auto def = fx.load(
            package({QStringLiteral("a")},
                    {pass(0, {bufferIn(QStringLiteral("a"))}, bufferOut(QStringLiteral("a"))),
                     pass(1, {bufferIn(QStringLiteral("a"))}, canvasOut())}),
            &error, &ok, AliasPolicy::Warn);
        QVERIFY2(ok, qPrintable(error));
        QVERIFY(def.valid);
        QCOMPARE(def.warnings.size(), 1);
        QVERIFY(def.warnings[0].contains(QStringLiteral("ping-pong")));
    }

    void rejectRefusesWithTheSameSentence()
    {
        Fixture fx;
        QString error;
        bool ok = true;
        const auto def = fx.load(
            package({QStringLiteral("a")},
                    {pass(0, {bufferIn(QStringLiteral("a"))}, bufferOut(QStringLiteral("a"))),
                     pass(1, {bufferIn(QStringLiteral("a"))}, canvasOut())}),
            &error, &ok, AliasPolicy::Reject);
        QVERIFY(!ok);
        QVERIFY(!def.valid);
        QVERIFY(error.contains(QStringLiteral("pass 0")));
        QVERIFY(error.contains(QStringLiteral("'a'")));
    }

    void rejectLeavesACleanPackageAlone()
    {
        Fixture fx;
        QString error;
        bool ok = false;
        const auto def = fx.load(
            package({QStringLiteral("a")},
                    {pass(0, {source()}, bufferOut(QStringLiteral("a"))),
                     pass(1, {bufferIn(QStringLiteral("a"))}, canvasOut())}),
            &error, &ok, AliasPolicy::Reject);
        QVERIFY2(ok, qPrintable(error));
        QVERIFY(def.valid);
        QVERIFY(def.warnings.isEmpty());
        QCOMPARE(def.passes.size(), 2);
    }

    void theDefaultPolicyIsStillWarn()
    {
        // The switch to Reject is a decision taken after the inventory below is clean AND the
        // packages people install themselves have been considered; this pins the current state.
        QCOMPARE(int(kAliasedPassPolicy), int(AliasPolicy::Warn));
    }

    // The gate for kAliasedPassPolicy: every pipeline manifest in the repository.
    void noShippedPackageAliasesAPass()
    {
        const QString root = QString::fromUtf8(DAFIN_SOURCE_DIR);
        QVERIFY(QDir(root).exists());

        int manifests = 0;
        int multiPass = 0;
        int loaded = 0;
        QStringList aliased;
        QDirIterator it(root, {QStringLiteral("*.json")}, QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            const QString path = it.next();
            if (path.contains(QStringLiteral("/.git/")) || path.contains(QStringLiteral("/build")))
                continue;
            QFile f(path);
            if (!f.open(QIODevice::ReadOnly))
                continue;
            const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
            if (!doc.isObject())
                continue;
            const QJsonObject pipeline = doc.object().value(QStringLiteral("pipeline")).toObject();
            if (pipeline.value(QStringLiteral("passes")).toArray().isEmpty())
                continue;
            ++manifests;
            if (pipeline.value(QStringLiteral("passes")).toArray().size() > 1)
                ++multiPass;

            // Read off the JSON, so a package that fails to load for another reason (a shader
            // this machine cannot see) still counts.
            if (aliasedPassesInJson(doc.object()) > 0)
                aliased << path;

            // And through the real parser, for the packages it can load from where they live.
            for (int maxSource : {0, 1}) {
                drift::GpuEffectDefinition def;
                QString error;
                if (loadGpuPipeline(doc.object(), QFileInfo(path).absolutePath(), maxSource, &def,
                                    &error, AliasPolicy::Reject)) {
                    ++loaded;
                    break;
                }
                if (error.contains(QStringLiteral("ping-pong")))
                    aliased << path;
            }
        }
        qInfo("inventory: %d pipeline manifests (%d multi-pass), %d loaded through the parser, "
              "%lld aliased",
              manifests, multiPass, loaded, static_cast<long long>(aliased.size()));
        // Not vacuous: the repository ships hundreds of GPU packages.
        QVERIFY2(manifests >= 100, "found too few package manifests; is DAFIN_SOURCE_DIR right?");
        QVERIFY2(loaded >= 100, "the parser could not load the shipped packages from their folders");
        QVERIFY2(aliased.isEmpty(), qPrintable(aliased.join(QLatin1Char('\n'))));
    }
};

QTEST_APPLESS_MAIN(TestGpuPackageAlias)
#include "tst_gpupackagealias.moc"
