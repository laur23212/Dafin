// Dafin: C20 / D-006 — a path an imported effect names (a texture, a default image or file) may only
// point inside the folder the effect came from. Real folders, real symlinks, no GL, no media.
#include "core/base/ConfinedPath.h"
#include "engine/packages/GpuPackageParse.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QtTest>

using drift::ConfineError;
using drift::resolveConfined;

namespace {

bool touch(const QString &path, const QByteArray &bytes = "x")
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    return f.open(QIODevice::WriteOnly) && f.write(bytes) == bytes.size();
}

} // namespace

class TstConfinedPath : public QObject
{
    Q_OBJECT

private slots:
    void insideFileResolves()
    {
        QTemporaryDir dir;
        QVERIFY(touch(dir.filePath(QStringLiteral("pkg/tex/noise.png"))));
        const auto r = resolveConfined(dir.filePath(QStringLiteral("pkg")), QStringLiteral("tex/noise.png"));
        QVERIFY(r.ok());
        QCOMPARE(r.path, QFileInfo(dir.filePath(QStringLiteral("pkg/tex/noise.png"))).canonicalFilePath());
    }

    void dotDotThatStaysInsideIsFine()
    {
        QTemporaryDir dir;
        QVERIFY(touch(dir.filePath(QStringLiteral("pkg/a.png"))));
        QVERIFY(resolveConfined(dir.filePath(QStringLiteral("pkg")), QStringLiteral("tex/../a.png")).ok());
    }

    void dotDotOutsideIsRefused()
    {
        QTemporaryDir dir;
        QVERIFY(touch(dir.filePath(QStringLiteral("pkg/a.png"))));
        QVERIFY(touch(dir.filePath(QStringLiteral("secret.png"))));
        const auto r = resolveConfined(dir.filePath(QStringLiteral("pkg")), QStringLiteral("../secret.png"));
        QVERIFY(!r.ok());
        QCOMPARE(r.error, ConfineError::Escapes);
    }

    void absolutePathsAreRefusedEvenInsideTheFolder()
    {
        QTemporaryDir dir;
        const QString inside = dir.filePath(QStringLiteral("pkg/a.png"));
        QVERIFY(touch(inside));
        QCOMPARE(resolveConfined(dir.filePath(QStringLiteral("pkg")), inside).error, ConfineError::Absolute);
        QCOMPARE(resolveConfined(dir.filePath(QStringLiteral("pkg")), QStringLiteral("C:\\Windows\\a.png")).error,
                 ConfineError::Absolute);
        QCOMPARE(resolveConfined(dir.filePath(QStringLiteral("pkg")), QStringLiteral("\\\\host\\share\\a.png")).error,
                 ConfineError::Absolute);
        QCOMPARE(resolveConfined(dir.filePath(QStringLiteral("pkg")), QStringLiteral(":/qt/a.png")).error,
                 ConfineError::Absolute);
    }

    void backslashDotDotIsRefusedToo()
    {
        QTemporaryDir dir;
        QVERIFY(touch(dir.filePath(QStringLiteral("pkg/a.png"))));
        QVERIFY(touch(dir.filePath(QStringLiteral("secret.png"))));
        QCOMPARE(resolveConfined(dir.filePath(QStringLiteral("pkg")), QStringLiteral("..\\secret.png")).error,
                 ConfineError::Escapes);
    }

    void symlinkedFileLeadingOutsideIsRefused()
    {
        QTemporaryDir dir;
        QVERIFY(touch(dir.filePath(QStringLiteral("pkg/a.png"))));
        QVERIFY(touch(dir.filePath(QStringLiteral("secret.png"))));
        QVERIFY(QFile::link(dir.filePath(QStringLiteral("secret.png")), dir.filePath(QStringLiteral("pkg/link.png"))));
        const auto r = resolveConfined(dir.filePath(QStringLiteral("pkg")), QStringLiteral("link.png"));
        QVERIFY(!r.ok());
        QCOMPARE(r.error, ConfineError::SymlinkEscapes);
    }

    void symlinkedFolderLeadingOutsideIsRefused()
    {
        QTemporaryDir dir;
        QVERIFY(touch(dir.filePath(QStringLiteral("pkg/a.png"))));
        QVERIFY(touch(dir.filePath(QStringLiteral("outside/secret.png"))));
        QVERIFY(QFile::link(dir.filePath(QStringLiteral("outside")), dir.filePath(QStringLiteral("pkg/tex"))));
        QCOMPARE(resolveConfined(dir.filePath(QStringLiteral("pkg")), QStringLiteral("tex/secret.png")).error,
                 ConfineError::SymlinkEscapes);
    }

    void symlinkThatStaysInsideIsFine()
    {
        QTemporaryDir dir;
        QVERIFY(touch(dir.filePath(QStringLiteral("pkg/real/a.png"))));
        QVERIFY(QFile::link(dir.filePath(QStringLiteral("pkg/real")), dir.filePath(QStringLiteral("pkg/alias"))));
        QVERIFY(resolveConfined(dir.filePath(QStringLiteral("pkg")), QStringLiteral("alias/a.png")).ok());
    }

    void theFolderItselfMayBeReachedThroughASymlink()
    {
        QTemporaryDir dir;
        QVERIFY(touch(dir.filePath(QStringLiteral("real/pkg/a.png"))));
        QVERIFY(QFile::link(dir.filePath(QStringLiteral("real")), dir.filePath(QStringLiteral("alias"))));
        QVERIFY(resolveConfined(dir.filePath(QStringLiteral("alias/pkg")), QStringLiteral("a.png")).ok());
    }

    void siblingFolderWithTheSamePrefixIsOutside()
    {
        QTemporaryDir dir;
        QVERIFY(touch(dir.filePath(QStringLiteral("pkg/a.png"))));
        QVERIFY(touch(dir.filePath(QStringLiteral("pkg-evil/a.png"))));
        QCOMPARE(resolveConfined(dir.filePath(QStringLiteral("pkg")), QStringLiteral("../pkg-evil/a.png")).error,
                 ConfineError::Escapes);
    }

    void missingFileAndEmptyNameAreTold()
    {
        QTemporaryDir dir;
        QVERIFY(QDir().mkpath(dir.filePath(QStringLiteral("pkg"))));
        QCOMPARE(resolveConfined(dir.filePath(QStringLiteral("pkg")), QStringLiteral("nope.png")).error,
                 ConfineError::NotFound);
        QCOMPARE(resolveConfined(dir.filePath(QStringLiteral("pkg")), QString()).error, ConfineError::Empty);
        QCOMPARE(resolveConfined(QString(), QStringLiteral("a.png")).error, ConfineError::RootInvalid);
    }

    // --- the effect loader goes through it -----------------------------------------------------

    void gpuPipelineRefusesATextureOutsideItsFolder()
    {
        QTemporaryDir dir;
        QVERIFY(touch(dir.filePath(QStringLiteral("pkg/ok.png"))));
        QVERIFY(touch(dir.filePath(QStringLiteral("pkg/main.frag")), "void main(){}"));
        QVERIFY(touch(dir.filePath(QStringLiteral("secret.png"))));
        const QString pkg = dir.filePath(QStringLiteral("pkg"));

        const auto pipeline = [](const QString &texFile) {
            return QJsonObject{
                {QStringLiteral("pipeline"),
                 QJsonObject{
                     {QStringLiteral("textures"),
                      QJsonArray{QJsonObject{{QStringLiteral("id"), QStringLiteral("t")}, {QStringLiteral("file"), texFile}}}},
                     {QStringLiteral("passes"),
                      QJsonArray{QJsonObject{{QStringLiteral("fragmentShader"), QStringLiteral("main.frag")},
                                             {QStringLiteral("output"), QStringLiteral("screen")},
                                             {QStringLiteral("inputs"),
                                              QJsonArray{QJsonObject{{QStringLiteral("type"), QStringLiteral("texture")},
                                                                     {QStringLiteral("id"), QStringLiteral("t")}}}}}}}}}};
        };

        drift::GpuEffectDefinition good;
        QString err;
        // The inside texture must not be refused for being outside. (Whether the rest of the
        // pipeline is valid is not this test's business; only the message about the file.)
        GpuPackageParse::loadGpuPipeline(pipeline(QStringLiteral("ok.png")), pkg, 0, &good, &err);
        QVERIFY2(!err.contains(QStringLiteral("outside")), qPrintable(err));
        QVERIFY2(!err.contains(QStringLiteral("missing texture")), qPrintable(err));

        for (const QString &bad : {QStringLiteral("../secret.png"), dir.filePath(QStringLiteral("secret.png"))}) {
            drift::GpuEffectDefinition out;
            QString e;
            QVERIFY(!GpuPackageParse::loadGpuPipeline(pipeline(bad), pkg, 0, &out, &e));
            QVERIFY2(e.contains(QStringLiteral("outside")), qPrintable(e));
        }
    }

    void packageAssetsOutsideTheFolderResolveToNothing()
    {
        QTemporaryDir dir;
        QVERIFY(touch(dir.filePath(QStringLiteral("pkg/thumbnail.png"))));
        QVERIFY(touch(dir.filePath(QStringLiteral("secret.png"))));
        const QString pkg = dir.filePath(QStringLiteral("pkg"));
        QVERIFY(!GpuPackageParse::resolvePackageAsset(pkg, QStringLiteral("thumbnail.png")).isEmpty());
        QVERIFY(GpuPackageParse::resolvePackageAsset(pkg, QStringLiteral("../secret.png")).isEmpty());
        QVERIFY(GpuPackageParse::resolvePackageAsset(pkg, dir.filePath(QStringLiteral("secret.png"))).isEmpty());
        QVERIFY(GpuPackageParse::resolvePackageAsset(QString(), QStringLiteral("thumbnail.png")).isEmpty());
    }
};

QTEST_GUILESS_MAIN(TstConfinedPath)
#include "tst_confinedpath.moc"
