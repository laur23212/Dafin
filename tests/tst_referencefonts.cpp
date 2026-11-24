// Dafin: the 43 exact reference faces of the approved titles (title-fonts/ref-*, listed in
// title-fonts/REFERENCE_FONTS.csv with the SHA-256 of each file). Headless: no window needed.

#include "engine/packages/assets/FontCatalog.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRawFont>
#include <QtTest>

namespace {
struct Row
{
    QString package, file, styleName, sha, family;
    int weight = 400;
    bool italic = false;
};
}

class TestReferenceFonts : public QObject
{
    Q_OBJECT
    QDir m_root{QStringLiteral(DRIFT_TEST_DATA_DIR "/../../title-fonts")};
    QList<Row> m_rows;

    QString familyOf(const QString &package) const
    {
        QFile f(m_root.filePath(package + QStringLiteral("/family.json")));
        if (!f.open(QIODevice::ReadOnly))
            return {};
        return QJsonDocument::fromJson(f.readAll()).object().value(QStringLiteral("family")).toString();
    }

private slots:
    void initTestCase()
    {
        QFile csv(m_root.filePath(QStringLiteral("REFERENCE_FONTS.csv")));
        QVERIFY(csv.open(QIODevice::ReadOnly | QIODevice::Text));
        csv.readLine(); // header
        while (!csv.atEnd()) {
            const QStringList c = QString::fromUtf8(csv.readLine()).trimmed().split(QLatin1Char(','));
            if (c.size() < 8)
                continue;
            Row r;
            r.package = c[0];
            r.file = c[1];
            r.weight = c[2].toInt();
            r.styleName = c[3];
            r.italic = c[4] == QLatin1String("1");
            r.sha = c[5];
            r.family = familyOf(r.package);
            m_rows.append(r);
        }
        QCOMPARE(m_rows.size(), 43);
        reloadFontCatalog({m_root.absolutePath()});
    }

    // The bytes are the ones the approved titles were rendered with.
    void everyFileHasItsExactHash()
    {
        for (const Row &r : m_rows) {
            QFile f(m_root.filePath(r.package + QLatin1Char('/') + r.file));
            QVERIFY2(f.open(QIODevice::ReadOnly), qPrintable(r.file));
            QCOMPARE(QString::fromLatin1(QCryptographicHash::hash(f.readAll(), QCryptographicHash::Sha256).toHex()), r.sha);
        }
    }

    // Each face loads, and asking its family for its weight (and slant) returns that very file.
    void everyFaceLoadsWithItsWeight()
    {
        for (const Row &r : m_rows) {
            QVERIFY2(!r.family.isEmpty(), qPrintable(r.package));
            QVERIFY2(fontFamilyForName(r.family), qPrintable(r.family));
            drift::TextStyle style;
            style.fontFamily = r.family;
            style.fontWeight = r.weight;
            style.italic = r.italic;
            const ResolvedFont resolved = resolveFont(style);
            QVERIFY2(resolved.bundled, qPrintable(r.file));
            QCOMPARE(QFileInfo(resolved.family).fileName(), r.file);
            QCOMPARE(resolved.weight, r.weight);
            QCOMPARE(resolved.italic, r.italic);
            QVERIFY2(QRawFont(resolved.family, 20).isValid(), qPrintable(r.file));
        }
    }

    // Within one family no two faces share a weight and slant (the lookup would be ambiguous).
    void noAmbiguousWeights()
    {
        QSet<QString> seen;
        for (const Row &r : m_rows) {
            const QString key = r.package + QLatin1Char('|') + QString::number(r.weight) + (r.italic ? QLatin1String("i") : QLatin1String("u"));
            QVERIFY2(!seen.contains(key), qPrintable(key));
            seen.insert(key);
        }
    }

    // Romanian letters, comma below. Documented gaps: EB Garamond 12 Bold (a 57 KB subset) and the
    // symbols font have none, so titles never rely on them for Romanian text.
    void romanianDiacritics()
    {
        const QList<uint> letters{0x103, 0xE2, 0xEE, 0x219, 0x21B, 0x218, 0x21A, 0x102, 0xC2, 0xCE};
        const QSet<QString> noRomanian{QStringLiteral("EBGaramond12-Bold.ttf"), QStringLiteral("NotoSansSymbols2-Regular.ttf")};
        for (const Row &r : m_rows) {
            const QRawFont raw(m_root.filePath(r.package + QLatin1Char('/') + r.file), 20);
            QVERIFY2(raw.isValid(), qPrintable(r.file));
            bool all = true;
            for (const uint ch : letters)
                all = all && raw.supportsCharacter(ch);
            QCOMPARE(all, !noRomanian.contains(r.file));
        }
    }
};

QTEST_MAIN(TestReferenceFonts)
#include "tst_referencefonts.moc"
