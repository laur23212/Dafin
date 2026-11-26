// Dafin: Scene Titles (docs/scene-titles.md). The loader against tools/scene-ref/scene_core.py
// value for value, timing and errors; the measured layout (fitBox, after, fillGroup) against
// render.py's; and the native renderer against the reference renderer's frames of the approved
// ports (03b, 13, 13C, 14, 15 and the C10 frame sets) at the spec's gate (actor SSIM >= 0.97,
// position <= 0.5 px).
// Smart Keycaps (docs/scene-keycaps.md): phase A against keycaps.py exactly, phase B (laid out with
// the painter's own measurement) against keycaps.layout within 0.5 px, the approved shortcut titles
// at the same gate, a param edit that relays the keys out, and the errors that name a key.
// Headless: QImage and Skia's CPU raster only, so it runs on the Linux CI without a GPU.

#include "core/project/Project.h"
#include "core/scene/SceneCatalog.h"
#include "core/scene/SceneKeycaps.h"
#include "core/scene/SceneTitle.h"
#include "engine/render2d/scene/ScenePainter.h"

#include <QFile>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QtTest>

#include <cmath>
#include <cstdio>

using namespace drift;

namespace {

QJsonObject readJson(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    return QJsonDocument::fromJson(f.readAll()).object();
}

QString scenePath(const char *name)
{
    return QStringLiteral(DAFIN_SOURCE_DIR "/tools/scene-ref/scenes/%1.scene.json").arg(QLatin1String(name));
}

// The dump format of tools/scene-ref/core_values.py, line for line.
QStringList dump(const scene::Scene &sc)
{
    QStringList out;
    const auto line = [&out](const char *fmt, auto... args) {
        char buf[512];
        std::snprintf(buf, sizeof buf, fmt, args...);
        out << QString::fromUtf8(buf);
    };
    line("hold %.9f %.9f", sc.holdStart, sc.holdEnd);
    for (const scene::Actor &a : sc.actors)
        line("actor %s %s text=[%s] w=%.3f", a.id.toUtf8().constData(), a.type.toUtf8().constData(),
             a.text.toUtf8().constData(), a.width);
    for (double d : {6.0, 4.0, 10.0, 2.0})
        for (double t : {0.0, 0.3, 0.9, 1.6, 3.0, 4.9, 5.5, 5.99})
            line("tm %.2f %.2f %.9f", d, t, scene::timeMap(sc, d, t));
    for (const scene::Actor &a : sc.actors)
        for (double t : {0.0, 0.2, 0.4, 0.6, 0.8, 1.0, 1.5, 2.0, 4.7, 4.8, 5.0, 5.2}) {
            const scene::Channels c = scene::channels(a, t);
            line("ch %s %.2f %.9f %.9f %.9f %.9f %.9f", a.id.toUtf8().constData(), t, c.opacity, c.wipe, c.progress,
                 c.dx, c.dy);
        }
    return out;
}

// The --geometry format of tools/scene-ref/core_values.py: what phase B of Smart Keycaps gave
// every generated actor.
QStringList geometry(const scene::Scene &sc)
{
    QStringList out;
    const auto p = [](std::initializer_list<double> v) {
        QStringList s;
        for (double x : v)
            s << QString::asprintf("%.6f", x);
        return s.join(QLatin1Char(','));
    };
    for (const scene::Actor &a : sc.actors) {
        if (a.keycapsBlock.isEmpty())
            continue;
        if (a.type == QLatin1String("group"))
            out << QStringLiteral("geo %1 group at=%2 size=%3").arg(a.id, p({a.groupAt.x(), a.groupAt.y()}), p({a.groupW, a.groupH}));
        else if (a.type == QLatin1String("line"))
            out << QStringLiteral("geo %1 line from=%2 to=%3").arg(a.id, p({a.from.x(), a.from.y()}), p({a.to.x(), a.to.y()}));
        else if (a.type == QLatin1String("text"))
            out << QStringLiteral("geo %1 text size=%2 baseline=%3 center=%4 dy=%5 hidden=%6")
                       .arg(a.id, p({a.size}), p({a.baseline.x(), a.baseline.y()}))
                       .arg(a.centerInGroup ? 1 : 0)
                       .arg(p({a.centerInGroup ? a.centerDy : 0.0}))
                       .arg(a.hidden ? 1 : 0);
        else
            out << QStringLiteral("geo %1 box box=%2").arg(a.id, p({a.box[0], a.box[1], a.box[2], a.box[3]}));
    }
    return out;
}

// The format of tools/scene-ref/core_values.py --layout: what Sprite2x.layout() resolves.
QStringList layoutDump(const scene::Scene &sc)
{
    QStringList out;
    char buf[512];
    for (const scene::Actor &a : sc.actors) {
        const QByteArray id = a.id.toUtf8();
        if (a.type == QLatin1String("group")) {
            std::snprintf(buf, sizeof buf, "group %s %.3f %.3f %.3f %.3f", id.constData(), a.groupAt.x(), a.groupAt.y(),
                          a.groupW, a.groupH);
            out << QString::fromUtf8(buf);
        } else if (a.type == QLatin1String("text") && a.anchorKind == QLatin1String("baseline")) {
            std::snprintf(buf, sizeof buf, "text %s %.3f %.3f", id.constData(), a.baseline.x(), a.baseline.y());
            out << QString::fromUtf8(buf);
        }
        if (a.hasFillGroup) {
            std::snprintf(buf, sizeof buf, "box %s %.3f %.3f %.3f %.3f", id.constData(), a.box[0], a.box[1], a.box[2], a.box[3]);
            out << QString::fromUtf8(buf);
        }
    }
    return out;
}

// One geometry line against the reference's: same actor and kind; a text's size, centring and
// hidden flag exactly; every position and extent within 0.5 px. Empty when they agree.
QString geometryMismatch(const QString &got, const QString &want)
{
    const QStringList g = got.split(QLatin1Char(' ')), w = want.split(QLatin1Char(' '));
    if (g.size() != w.size() || g.size() < 3 || g.mid(0, 3) != w.mid(0, 3))
        return QStringLiteral("native [%1] vs reference [%2]").arg(got, want);
    const bool text = g.at(2) == QLatin1String("text");
    for (int i = 3; i < g.size(); ++i) {
        const QString key = g.at(i).section(QLatin1Char('='), 0, 0);
        const QStringList gv = g.at(i).section(QLatin1Char('='), 1).split(QLatin1Char(','));
        const QStringList wv = w.at(i).section(QLatin1Char('='), 1).split(QLatin1Char(','));
        const bool exact = text && key != QLatin1String("baseline");
        if (key != w.at(i).section(QLatin1Char('='), 0, 0) || gv.size() != wv.size())
            return QStringLiteral("native [%1] vs reference [%2]").arg(got, want);
        for (int k = 0; k < gv.size(); ++k) {
            const double d = std::fabs(gv.at(k).toDouble() - wv.at(k).toDouble());
            if (exact ? d > 1e-6 : d > 0.5)
                return QStringLiteral("%1 %2: native %3 vs reference %4 (%5 px)")
                    .arg(g.at(1), key, g.at(i), w.at(i))
                    .arg(d);
        }
    }
    return {};
}

QStringList readLines(const QString &name)
{
    QFile f(QStringLiteral(DRIFT_TEST_DATA_DIR "/scene-titles/") + name);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    return QString::fromUtf8(f.readAll()).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
}

QJsonObject params(const char *json)
{
    return QJsonDocument::fromJson(QByteArray(json)).object();
}

// The document with one actor's key set (top-level actors and one level of group children).
QJsonObject withActorKey(QJsonObject doc, const QString &id, const QString &key, const QJsonValue &value)
{
    QJsonArray actors = doc.value(QStringLiteral("actors")).toArray();
    for (int i = 0; i < actors.size(); ++i) {
        QJsonObject a = actors.at(i).toObject();
        if (a.value(QStringLiteral("id")).toString() == id) {
            a.insert(key, value);
            actors.replace(i, a);
            break;
        }
        QJsonArray kids = a.value(QStringLiteral("children")).toArray();
        for (int k = 0; k < kids.size(); ++k) {
            QJsonObject c = kids.at(k).toObject();
            if (c.value(QStringLiteral("id")).toString() == id) {
                c.insert(key, value);
                kids.replace(k, c);
                a.insert(QStringLiteral("children"), kids);
                actors.replace(i, a);
            }
        }
    }
    doc.insert(QStringLiteral("actors"), actors);
    return doc;
}

QImage overBase(const QImage &frame)
{
    return skia::compositeOverMatte(frame, skia::sceneBaseMatte(frame.size())).convertToFormat(QImage::Format_RGB32);
}

QVector<double> luma(const QImage &img, const QRect &r)
{
    QVector<double> v(r.width() * r.height());
    for (int y = 0; y < r.height(); ++y) {
        const auto *row = reinterpret_cast<const QRgb *>(img.constScanLine(r.top() + y));
        for (int x = 0; x < r.width(); ++x) {
            const QRgb p = row[r.left() + x];
            v[y * r.width() + x] = 0.299 * qRed(p) + 0.587 * qGreen(p) + 0.114 * qBlue(p);
        }
    }
    return v;
}

// Gaussian SSIM (11x11, sigma 1.5) on luma, as tools/scene-ref/compare.py.
double ssim(const QVector<double> &a, const QVector<double> &b, int w, int h)
{
    if (w < 11 || h < 11)
        return a == b ? 1.0 : 0.0;
    double k[11], ks = 0;
    for (int i = 0; i < 11; ++i)
        ks += k[i] = std::exp(-((i - 5) * (i - 5)) / (2 * 1.5 * 1.5));
    for (double &x : k)
        x /= ks;
    const auto blur = [&](const QVector<double> &src) {
        QVector<double> tmp(src.size()), out(src.size());
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x) {
                double s = 0;
                for (int i = -5; i <= 5; ++i)
                    s += k[i + 5] * src[y * w + qBound(0, x + i, w - 1)];
                tmp[y * w + x] = s;
            }
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x) {
                double s = 0;
                for (int i = -5; i <= 5; ++i)
                    s += k[i + 5] * tmp[qBound(0, y + i, h - 1) * w + x];
                out[y * w + x] = s;
            }
        return out;
    };
    QVector<double> aa(a.size()), bb(a.size()), ab(a.size());
    for (int i = 0; i < a.size(); ++i) {
        aa[i] = a[i] * a[i];
        bb[i] = b[i] * b[i];
        ab[i] = a[i] * b[i];
    }
    const QVector<double> ma = blur(a), mb = blur(b), saa = blur(aa), sbb = blur(bb), sab = blur(ab);
    const double c1 = std::pow(0.01 * 255, 2), c2 = std::pow(0.03 * 255, 2);
    double sum = 0;
    int n = 0;
    for (int y = 5; y < h - 5; ++y)
        for (int x = 5; x < w - 5; ++x) {
            const int i = y * w + x;
            const double va = saa[i] - ma[i] * ma[i], vb = sbb[i] - mb[i] * mb[i], cov = sab[i] - ma[i] * mb[i];
            sum += ((2 * ma[i] * mb[i] + c1) * (2 * cov + c2)) / ((ma[i] * ma[i] + mb[i] * mb[i] + c1) * (va + vb + c2));
            ++n;
        }
    return n ? sum / n : 1.0;
}

// Alpha-weighted centroid shift between two frames inside r, px.
double centroidShift(const QImage &a, const QImage &b, const QRect &r)
{
    const auto centroid = [&](const QImage &img) {
        double sx = 0, sy = 0, s = 0;
        for (int y = r.top(); y <= r.bottom(); ++y) {
            const auto *row = reinterpret_cast<const QRgb *>(img.constScanLine(y));
            for (int x = r.left(); x <= r.right(); ++x) {
                const double w = qAlpha(row[x]);
                sx += w * x;
                sy += w * y;
                s += w;
            }
        }
        return s > 0 ? QPointF(sx / s, sy / s) : QPointF();
    };
    const QPointF d = centroid(a) - centroid(b);
    return std::hypot(d.x(), d.y());
}

// The Smart Keycaps rows (core_values.py and core_values.py --geometry of each).
void keycapRows()
{
    QTest::newRow("13A default") << scenePath("13_A_Shortcut_Keys") << QString() << QStringLiteral("13_A_default");
    QTest::newRow("13A relayout") << scenePath("13_A_Shortcut_Keys")
                                  << QStringLiteral(R"({"shortcut":"Ctrl+Alt+Shift+Win+Esc+Tab"})")
                                  << QStringLiteral("13_A_relayout");
    QTest::newRow("13C U1 default") << scenePath("13C_U1_Universal_Horizontal_Copy") << QString()
                                    << QStringLiteral("13C_U1_default");
    QTest::newRow("13C U2 default") << scenePath("13C_U2_Universal_Vertical_Paste") << QString()
                                    << QStringLiteral("13C_U2_default");
    QTest::newRow("13C U3 default") << scenePath("13C_U3_Mac_Dual_Names_Force_Quit") << QString()
                                    << QStringLiteral("13C_U3_default");
    QTest::newRow("T06 default") << scenePath("T06_dual-key-names") << QString() << QStringLiteral("T06_default");
    QTest::newRow("T11 default") << scenePath("T11_chained-sequence") << QString() << QStringLiteral("T11_default");
    QTest::newRow("T12 default") << scenePath("T12_alternatives") << QString() << QStringLiteral("T12_default");
    QTest::newRow("T16 wrap") << scenePath("T16_vertical-9x16")
                              << QStringLiteral(R"({"shortcutWindows":"Ctrl+Alt+Shift+Win+Esc+Tab+F2+F3 OR Ctrl+Y THEN Ctrl+Z"})")
                              << QStringLiteral("T16_wrap");
}

} // namespace

class SceneTitleTest : public QObject
{
    Q_OBJECT

private slots:
    void coreMatchesReference_data()
    {
        QTest::addColumn<QString>("scene");
        QTest::addColumn<QString>("params");
        QTest::addColumn<QString>("expected");
        QTest::newRow("15A default") << scenePath("15_A_Step_Focus") << QString() << QStringLiteral("core_15_A_default.txt");
        QTest::newRow("15A params") << scenePath("15_A_Step_Focus")
                                    << QStringLiteral(R"({"totalSteps":7,"currentStep":9,"title":"Ș{{x}}"})")
                                    << QStringLiteral("core_15_A_params.txt");
        QTest::newRow("03b default") << scenePath("03b_LT_B_25") << QString() << QStringLiteral("core_03b_default.txt");
        // Smart Keycaps, phase A: no font is involved, so the expanded actors match exactly.
        keycapRows();
        // C10: progress, menu path, anchors (tools/scene-ref/core_values.py, same format).
        QTest::newRow("15B default") << scenePath("15_B_Definition_Card") << QString() << QStringLiteral("core_15_B_default.txt");
        QTest::newRow("15B clamped") << scenePath("15_B_Definition_Card") << QStringLiteral(R"({"totalEntries":8,"entryIndex":40})")
                                     << QStringLiteral("core_15_B_clamped.txt");
        QTest::newRow("14A default") << scenePath("14_A_Menu_Path") << QString() << QStringLiteral("core_14_A_default.txt");
        QTest::newRow("14A two") << scenePath("14_A_Menu_Path") << QStringLiteral(R"({"itemCount":2})") << QStringLiteral("core_14_A_two.txt");
        QTest::newRow("14B default") << scenePath("14_B_Setting_Change") << QString() << QStringLiteral("core_14_B_default.txt");
        QTest::newRow("14B no unit") << scenePath("14_B_Setting_Change") << QStringLiteral(R"({"unit":""})")
                                     << QStringLiteral("core_14_B_nounit.txt");
        QTest::newRow("11 default") << scenePath("11_AnchorNote_v4") << QString() << QStringLiteral("core_11_default.txt");
        QTest::newRow("12A default") << scenePath("12_A_Anchor_Bracket") << QString() << QStringLiteral("core_12_A_default.txt");
        QTest::newRow("12B default") << scenePath("12_B_Anchor_Measure") << QString() << QStringLiteral("core_12_B_default.txt");
        QTest::newRow("12BC default") << scenePath("12B_C_Anchor_Index_Manual") << QString() << QStringLiteral("core_12B_C_default.txt");
        QTest::newRow("12BC blank") << scenePath("12B_C_Anchor_Index_Manual") << QStringLiteral(R"({"badge1":"","badge2":""})")
                                    << QStringLiteral("core_12B_C_blank.txt");
    }


    void coreMatchesReference()
    {
        QFETCH(QString, scene);
        QFETCH(QString, params);
        QFETCH(QString, expected);
        if (!expected.endsWith(QLatin1String(".txt")))
            expected = QStringLiteral("core_%1.txt").arg(expected);
        QString error;
        const std::optional<scene::Scene> sc =
            scene::loadScene(readJson(scene), QJsonDocument::fromJson(params.toUtf8()).object(), &error);
        QVERIFY2(sc, qPrintable(error));
        QFile f(QStringLiteral(DRIFT_TEST_DATA_DIR "/scene-titles/") + expected);
        QVERIFY(f.open(QIODevice::ReadOnly));
        const QStringList want = QString::fromUtf8(f.readAll()).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        const QStringList got = dump(*sc);
        QCOMPARE(got.size(), want.size());
        for (int i = 0; i < want.size(); ++i)
            QCOMPARE(got.at(i), want.at(i));
    }

    void expressionsAndSlots()
    {
        const QVariantMap env{{QStringLiteral("i"), qlonglong(2)}, {QStringLiteral("n"), qlonglong(5)},
                              {QStringLiteral("AQUA"), QVariantList{1, 2, 3}}, {QStringLiteral("F"), QVariantList{9, 9, 9}}};
        QCOMPARE(scene::evalExpression(QStringLiteral("590 + i*154"), env).toLongLong(), 898);
        QCOMPARE(scene::evalExpression(QStringLiteral("AQUA if i < n else F"), env).toList().at(0).toInt(), 1);
        QCOMPARE(scene::evalExpression(QStringLiteral("-7 // 2"), env).toLongLong(), -4);
        QCOMPARE(scene::evalExpression(QStringLiteral("-7 % 3"), env).toLongLong(), 2);
        QCOMPARE(scene::evalExpression(QStringLiteral("round(2.5) + round(3.5)"), env).toLongLong(), 6);
        QCOMPARE(scene::evalExpression(QStringLiteral("1 < i <= 2 and not n == 4"), env).toBool(), true);
        QString error;
        QVERIFY(!scene::evalExpression(QStringLiteral("__import__('os')"), env, &error).isValid());
        QVERIFY(!error.isEmpty());
        QCOMPARE(scene::formatText(QStringLiteral("STEP {i:02}  /  {n:02} {{x}}"), env), QStringLiteral("STEP 02  /  05 {x}"));
        QCOMPARE(scene::formatValue(2.5, QStringLiteral(".1f")), QStringLiteral("2.5"));
    }

    void loaderRefusesWhatTheSpecRefuses()
    {
        const QJsonObject step = readJson(scenePath("15_A_Step_Focus"));
        QString error;
        QVERIFY(!scene::loadScene(step, QJsonObject{{QStringLiteral("totalSteps"), 2.5}}, &error));
        QVERIFY(error.contains(QLatin1String("integer")));
        QVERIFY(!scene::loadScene(step, QJsonObject{{QStringLiteral("nope"), 1}}, &error));
        // currentStep is clamped to totalSteps; totalSteps has no clamp.
        QCOMPARE(scene::loadScene(step, QJsonObject{{QStringLiteral("currentStep"), 9}})->values.value(QStringLiteral("currentStep")).toLongLong(), 5);
        QVERIFY(!scene::loadScene(step, QJsonObject{{QStringLiteral("totalSteps"), 40}}, &error));

        QJsonObject reserved = step;
        QJsonArray actors = reserved.value(QStringLiteral("actors")).toArray();
        QJsonObject first = actors.at(0).toObject();
        first.insert(QStringLiteral("effect"), QJsonObject{{QStringLiteral("type"), QStringLiteral("sdfMorph")}});
        actors.replace(0, first);
        reserved.insert(QStringLiteral("actors"), actors);
        QVERIFY(!scene::loadScene(reserved, {}, &error));
        QVERIFY(error.contains(QLatin1String("reserved")));

        // offsetSnap is "round" (default) or "trunc" (the Stage13 keycaps), nothing else.
        QJsonObject snapped = step;
        actors = snapped.value(QStringLiteral("actors")).toArray();
        first = actors.at(0).toObject();
        first.insert(QStringLiteral("offsetSnap"), QStringLiteral("floor"));
        actors.replace(0, first);
        snapped.insert(QStringLiteral("actors"), actors);
        QVERIFY(!scene::loadScene(snapped, {}, &error));
        QVERIFY(error.contains(QLatin1String("offsetSnap")));
        first.insert(QStringLiteral("offsetSnap"), QStringLiteral("trunc"));
        actors.replace(0, first);
        snapped.insert(QStringLiteral("actors"), actors);
        QVERIFY2(scene::loadScene(snapped, {}, &error), qPrintable(error));

        QJsonObject disabled = step;
        actors = disabled.value(QStringLiteral("actors")).toArray();
        first = actors.at(0).toObject();
        first.insert(QStringLiteral("enabled"), false);
        actors.replace(0, first);
        disabled.insert(QStringLiteral("actors"), actors);
        QVERIFY(!scene::loadScene(disabled, {}, &error));
        QVERIFY(error.contains(QLatin1String("not optional")));

        QJsonObject wrong = step;
        wrong.insert(QStringLiteral("format"), QStringLiteral("dafin.title-template"));
        QVERIFY(!scene::loadScene(wrong, {}, &error));

        // C10 fields: what the spec allows loads, anything else is refused with a reason.
        const QJsonObject bracket = readJson(scenePath("12_A_Anchor_Bracket"));
        QVERIFY2(scene::loadScene(bracket, {}, &error), qPrintable(error));
        QVERIFY(!scene::loadScene(withActorKey(bracket, QStringLiteral("label"), QStringLiteral("ss"), 3), {}, &error));
        QVERIFY(error.contains(QLatin1String("must be 1 or 2")));
        const QJsonObject menu = readJson(scenePath("14_A_Menu_Path"));
        QVERIFY(!scene::loadScene(withActorKey(menu, QStringLiteral("card1"), QStringLiteral("ss"), 4), {}, &error));
        QVERIFY(error.contains(QLatin1String("must be 2 or 3")));
        QVERIFY(!scene::loadScene(withActorKey(menu, QStringLiteral("card1"), QStringLiteral("flatten"), false), {}, &error));
        QVERIFY(error.contains(QLatin1String("fitBox belongs on a flattened group")));
        QVERIFY(!scene::loadScene(withActorKey(menu, QStringLiteral("card1"), QStringLiteral("fitBox"),
                                               QJsonObject{{QStringLiteral("actor"), QStringLiteral("caption")}}),
                                  {}, &error));
        QVERIFY(error.contains(QLatin1String("text child")));
        const QJsonObject setting = readJson(scenePath("14_B_Setting_Change"));
        QVERIFY(!scene::loadScene(withActorKey(setting, QStringLiteral("currentUnit"), QStringLiteral("after"),
                                               QJsonObject{{QStringLiteral("actor"), QStringLiteral("nope")}}),
                                  {}, &error));
        QVERIFY(error.contains(QLatin1String("after unknown actor")));
        QVERIFY(!scene::loadScene(withActorKey(setting, QStringLiteral("rule"), QStringLiteral("after"),
                                               QJsonObject{{QStringLiteral("actor"), QStringLiteral("title")}}),
                                  {}, &error));
        QVERIFY(error.contains(QLatin1String("groups and texts")));
    }

    // fitBox / after / fillGroup with the native font stack, against render.py's Sprite2x.layout()
    // (tools/scene-ref/core_values.py --layout), value for value.
    void layoutMatchesReference_data()
    {
        QTest::addColumn<QString>("scene");
        QTest::addColumn<QString>("params");
        QTest::addColumn<QString>("expected");
        QTest::newRow("14A default") << scenePath("14_A_Menu_Path") << QString() << QStringLiteral("layout_14_A_default.txt");
        QTest::newRow("14A long") << scenePath("14_A_Menu_Path")
                                  << QStringLiteral(R"({"itemCount":5,"item2":"Export settings and more"})")
                                  << QStringLiteral("layout_14_A_long.txt");
        QTest::newRow("14B default") << scenePath("14_B_Setting_Change") << QString() << QStringLiteral("layout_14_B_default.txt");
        QTest::newRow("14B long") << scenePath("14_B_Setting_Change")
                                  << QStringLiteral(R"({"currentValue":"1080","newValue":"2160","unit":"PX"})")
                                  << QStringLiteral("layout_14_B_long.txt");
    }

    void layoutMatchesReference()
    {
        QFETCH(QString, scene);
        QFETCH(QString, params);
        QFETCH(QString, expected);
        const skia::SceneBuild build = skia::sceneRenderer(readJson(scene), QJsonDocument::fromJson(params.toUtf8()).object());
        QVERIFY2(build.renderer, qPrintable(build.error));
        const QStringList want = readLines(expected);
        const QStringList got = layoutDump(skia::sceneOf(*build.renderer));
        QVERIFY(!want.isEmpty());
        QCOMPARE(got.size(), want.size());
        for (int i = 0; i < want.size(); ++i)
            QCOMPARE(got.at(i), want.at(i));
    }

    void menuCardsFollowTheirLabels()
    {
        const QJsonObject doc = readJson(scenePath("14_A_Menu_Path"));
        const auto build = [&](const char *json) {
            const skia::SceneBuild b = skia::sceneRenderer(doc, params(json));
            return b.renderer ? skia::sceneOf(*b.renderer) : scene::Scene();
        };
        const scene::Scene def = build("{}");
        // The approved geometry: 208 / 274 / 244 px cards at 341, 643 and 1035.
        QCOMPARE(def.actor(QStringLiteral("card1"))->groupW, 208.0);
        QCOMPARE(def.actor(QStringLiteral("card2"))->groupW, 274.0);
        QCOMPARE(def.actor(QStringLiteral("card3"))->groupW, 244.0);
        QCOMPARE(def.actor(QStringLiteral("card2"))->groupAt.x(), 643.0);
        QCOMPARE(def.actor(QStringLiteral("card3"))->groupAt.x(), 1035.0);
        // A long label widens its card and pushes the separator and the next card right.
        const scene::Scene longer = build(R"({"item2":"A much longer menu entry"})");
        QVERIFY(longer.actor(QStringLiteral("card2"))->groupW > 274.0);
        QVERIFY(longer.actor(QStringLiteral("chev2"))->groupAt.x() > def.actor(QStringLiteral("chev2"))->groupAt.x());
        QCOMPARE(longer.actor(QStringLiteral("card3"))->groupAt.x() - longer.actor(QStringLiteral("card2"))->groupAt.x(),
                 longer.actor(QStringLiteral("card2"))->groupW + 118.0); // gap 94 + 24 before the destination
        // The plate fills the fitted card.
        QCOMPARE(longer.actor(QStringLiteral("card2Plate"))->box[2], longer.actor(QStringLiteral("card2"))->groupW - 3.0);
        // Short labels keep the minimum; two items drop the third card and its separator.
        QCOMPARE(build(R"({"item1":"A","item2":"B","item3":"C"})").actor(QStringLiteral("card2"))->groupW, 274.0);
        const scene::Scene two = build(R"({"itemCount":2})");
        QVERIFY(!two.actor(QStringLiteral("card3")) && !two.actor(QStringLiteral("chev2")));
        QVERIFY(skia::sceneRenderer(doc, params(R"({"item2":""})")).renderer);
    }

    void unitFollowsTheValue()
    {
        const QJsonObject doc = readJson(scenePath("14_B_Setting_Change"));
        const skia::SceneBuild a = skia::sceneRenderer(doc);
        const skia::SceneBuild b = skia::sceneRenderer(doc, params(R"({"currentValue":"1080"})"));
        QVERIFY(a.renderer && b.renderer);
        const double ua = skia::sceneOf(*a.renderer).actor(QStringLiteral("currentUnit"))->baseline.x();
        const double ub = skia::sceneOf(*b.renderer).actor(QStringLiteral("currentUnit"))->baseline.x();
        QCOMPARE(ua, 544.0); // the approved frame: "30" at 347 + 71
        QVERIFY(ub > ua + 50);
        const skia::SceneBuild none = skia::sceneRenderer(doc, params(R"({"unit":""})"));
        QVERIFY(none.renderer && !skia::sceneOf(*none.renderer).actor(QStringLiteral("currentUnit")));
    }

    void emptyBadgeIsAPlainRing()
    {
        const QJsonObject doc = readJson(scenePath("12B_C_Anchor_Index_Manual"));
        const auto inside = [&](const char *json) {
            const skia::SceneBuild b = skia::sceneRenderer(doc, params(json));
            if (!b.renderer)
                return -1;
            const QImage img = skia::renderSceneFrame(b.renderer, 3.0);
            int ink = 0;
            for (int y = 380 - 12; y <= 380 + 12; ++y)
                for (int x = 1310 - 12; x <= 1310 + 12; ++x)
                    ink += qAlpha(img.pixel(x, y)) > 0;
            // the ring itself is there either way
            return qAlpha(img.pixel(1310 + 22, 380)) > 128 && qAlpha(img.pixel(1310, 380 - 22)) > 128 ? ink : -2;
        };
        QVERIFY(inside("{}") > 20);                         // "A" inside its ring
        QCOMPARE(inside(R"({"badge1":"","badge2":""})"), 0); // plain ring
        QVERIFY(inside(R"({"badge1":"","badge2":"B"})") == 0);
        const skia::SceneBuild one = skia::sceneRenderer(doc, params(R"({"badge1":"X","badge2":""})"));
        QVERIFY(one.renderer);
        const scene::Scene &sc = skia::sceneOf(*one.renderer);
        QVERIFY(sc.actor(QStringLiteral("badge1Text")) && !sc.actor(QStringLiteral("badge2Text")) && sc.actor(QStringLiteral("badge2")));
    }

    void definitionAccentIsShort()
    {
        const QJsonObject doc = readJson(scenePath("15_B_Definition_Card"));
        const auto end = [&](const char *json) {
            const std::optional<scene::Scene> sc = scene::loadScene(doc, params(json));
            return sc ? sc->actor(QStringLiteral("accent"))->to.x() - 358.0 : -1.0;
        };
        QCOMPARE(end("{}"), 89.0); // 4/8 of the 178 px track
        QCOMPARE(end(R"({"entryIndex":8,"totalEntries":8})"), 178.0);
        QCOMPARE(end(R"({"entryIndex":99,"totalEntries":8})"), 178.0); // clamped, never a big bar
        QCOMPARE(end(R"({"entryIndex":3,"totalEntries":5})"), 107.0);
        QVERIFY(skia::sceneRenderer(doc).renderer); // loads natively: no new fields
    }

    void cadenceSamplesWithoutChangingLength()
    {
        QJsonObject doc = readJson(scenePath("15_A_Step_Focus"));
        QJsonObject timing = doc.value(QStringLiteral("timing")).toObject();
        timing.insert(QStringLiteral("cadence"), 25);
        doc.insert(QStringLiteral("timing"), timing);
        const scene::Scene sc = *scene::loadScene(doc);
        // At 60 fps the motion steps at 25: frames 0..2 (0, 1/60, 2/60 s) all sample t = 0.
        QCOMPARE(scene::cadenceTime(sc, 2.0 / 60.0, 60.0), 0.0);
        QCOMPARE(scene::cadenceTime(sc, 3.0 / 60.0, 60.0), 0.04);
        // At or below the cadence, time is continuous.
        QCOMPARE(scene::cadenceTime(sc, 0.05, 25.0), 0.05);
        // The clip's length is the clip's: a 9 s clip ends its exits at 9 s.
        QVERIFY(qAbs(scene::timeMap(sc, 9.0, 9.0) - sc.duration) < 1e-9);
        QCOMPARE(scene::timeMap(sc, 9.0, 0.4), 0.4);
    }

    void sceneClipSurvivesSave()
    {
        Project project;
        Track track;
        track.type = TrackType::Shape;
        Clip clip;
        clip.id = QStringLiteral("s1");
        clip.type = ClipType::Scene;
        clip.scene.document = readJson(scenePath("03b_LT_B_25"));
        clip.scene.params = QJsonObject{{QStringLiteral("name"), QStringLiteral("Ana Pop")}};
        clip.timelineDuration = secondsToUs(6);
        clip.srcOut = clip.timelineDuration;
        QVERIFY(track.allowsClipType(ClipType::Scene));
        track.clips << clip;
        project.tracks() << track;
        const QJsonObject json = project.toJson();
        QString error;
        const Project back = Project::fromJson(json, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(json.value(QStringLiteral("version")).toInt(), 24);
        const Clip &c = back.tracks().constLast().clips.constFirst();
        QCOMPARE(c.type, ClipType::Scene);
        QVERIFY(c.scene == clip.scene);
    }

    void fitToFollowsTheText()
    {
        const QJsonObject doc = readJson(scenePath("03b_LT_B_25"));
        const skia::SceneBuild def = skia::sceneRenderer(doc);
        QVERIFY2(def.renderer, qPrintable(def.error));
        const skia::SceneBuild shortName = skia::sceneRenderer(doc, {{QStringLiteral("name"), QStringLiteral("Ion")}});
        const skia::SceneBuild longName = skia::sceneRenderer(
            doc, {{QStringLiteral("name"), QStringLiteral("Ștefania-Alexandra Țăranu-Popescu")}});
        QVERIFY(shortName.renderer && longName.renderer);
        const auto width = [](const skia::SceneBuild &b, const char *id) {
            return skia::sceneOf(*b.renderer).actor(QLatin1String(id))->boxWidth();
        };
        QCOMPARE(width(shortName, "strapMain"), 590.0); // the approved minimum
        QVERIFY(width(longName, "strapMain") > width(def, "strapMain") + 200);
        // The seam is the narrower strap.
        QCOMPARE(width(longName, "seam"), qMin(width(longName, "strapMain"), width(longName, "strapRole")));
    }

    void nativeMatchesReference_data()
    {
        QTest::addColumn<QString>("scene");
        QTest::addColumn<QString>("name");
        QTest::addColumn<QList<int>>("frames");
        QTest::addColumn<QString>("params");
        const QList<int> ports{10, 20, 40, 65, 135};
        QTest::newRow("15A") << scenePath("15_A_Step_Focus") << QStringLiteral("15_A_Step_Focus") << ports << QString();
        QTest::newRow("03b") << scenePath("03b_LT_B_25") << QStringLiteral("03b_LT_B_25") << ports << QString();
        // The Stage03b/13/13C/14/15 ports (identical to their approved scripts on all 150 frames).
        QTest::newRow("03b_LT_A_25") << scenePath("03b_LT_A_25") << QStringLiteral("03b_LT_A_25") << ports << QString();
        QTest::newRow("03b_LT_A_60") << scenePath("03b_LT_A_60") << QStringLiteral("03b_LT_A_60") << ports << QString();
        QTest::newRow("03b_LT_B_60") << scenePath("03b_LT_B_60") << QStringLiteral("03b_LT_B_60") << ports << QString();
        QTest::newRow("03b_LT_C_25_REV") << scenePath("03b_LT_C_25_REV") << QStringLiteral("03b_LT_C_25_REV") << ports << QString();
        QTest::newRow("03b_LT_C_60_REV") << scenePath("03b_LT_C_60_REV") << QStringLiteral("03b_LT_C_60_REV") << ports << QString();
        QTest::newRow("13_A_Shortcut_Keys") << scenePath("13_A_Shortcut_Keys") << QStringLiteral("13_A_Shortcut_Keys") << ports << QString();
        QTest::newRow("13_B_Source_Credit") << scenePath("13_B_Source_Credit") << QStringLiteral("13_B_Source_Credit") << ports << QString();
        QTest::newRow("13_C_Editorial_Checklist") << scenePath("13_C_Editorial_Checklist") << QStringLiteral("13_C_Editorial_Checklist") << ports << QString();
        QTest::newRow("13C_U1_Universal_Horizontal_Copy") << scenePath("13C_U1_Universal_Horizontal_Copy") << QStringLiteral("13C_U1_Universal_Horizontal_Copy") << ports << QString();
        QTest::newRow("13C_U2_Universal_Vertical_Paste") << scenePath("13C_U2_Universal_Vertical_Paste") << QStringLiteral("13C_U2_Universal_Vertical_Paste") << ports << QString();
        QTest::newRow("13C_U3_Mac_Dual_Names_Force_Quit") << scenePath("13C_U3_Mac_Dual_Names_Force_Quit") << QStringLiteral("13C_U3_Mac_Dual_Names_Force_Quit") << ports << QString();
        QTest::newRow("14_A_Menu_Path") << scenePath("14_A_Menu_Path") << QStringLiteral("14_A_Menu_Path") << ports << QString();
        QTest::newRow("14_B_Setting_Change") << scenePath("14_B_Setting_Change") << QStringLiteral("14_B_Setting_Change") << ports << QString();
        QTest::newRow("15_B_Definition_Card") << scenePath("15_B_Definition_Card") << QStringLiteral("15_B_Definition_Card") << ports << QString();
        QTest::newRow("15_C_Pro_Tip") << scenePath("15_C_Pro_Tip") << QStringLiteral("15_C_Pro_Tip") << ports << QString();
        // Smart Keycaps: the approved shortcut titles, two entrance frames and two of the hold. The
        // reference renders them identical to the approved scripts (test_parity.py --keycaps13
        // --keycaps13c), and the U1/U2/U3 holds to the approved source holds.
        const QList<int> keys{25, 40, 75, 110};
        QTest::newRow("13A") << scenePath("13_A_Shortcut_Keys") << QStringLiteral("13_A_Shortcut_Keys") << keys << QString();
        QTest::newRow("13C U1") << scenePath("13C_U1_Universal_Horizontal_Copy")
                                << QStringLiteral("13C_U1_Universal_Horizontal_Copy") << keys << QString();
        QTest::newRow("13C U2") << scenePath("13C_U2_Universal_Vertical_Paste")
                                << QStringLiteral("13C_U2_Universal_Vertical_Paste") << keys << QString();
        QTest::newRow("13C U3") << scenePath("13C_U3_Mac_Dual_Names_Force_Quit")
                                << QStringLiteral("13C_U3_Mac_Dual_Names_Force_Quit") << keys << QString();
        // C10 (frames: render.py render <scene> --frames 10,20,40,65,135 --no-mp4 [--param ...]); 15_B,
        // 14_A and 14_B are the port rows above, now on the C10 param scenes.
        for (const char *n : {"11_AnchorNote_v4", "12_A_Anchor_Bracket", "12_B_Anchor_Measure", "12B_C_Anchor_Index_Manual"})
            QTest::newRow(n) << scenePath(n) << QString::fromLatin1(n) << ports << QString();
        QTest::newRow("12B_C blank") << scenePath("12B_C_Anchor_Index_Manual") << QStringLiteral("12B_C_Anchor_Index_Manual_blank")
                                     << ports << QStringLiteral(R"({"badge1":"","badge2":""})");
        // Golden Run 18 (gold1x; identical to render_stage25.py … render_stage30.py on all 150 frames).
        for (const char *n : {"25A", "25B", "25C", "26A", "26B", "26C", "27A", "27B", "27C", "28A", "28B", "28C", "29A", "29B", "29C", "30A", "30B", "30C"})
            QTest::newRow(n) << scenePath(n) << QString::fromLatin1(n) << ports << QString();
        // The later Golden Run stages (gold1x; identical to their scripts on all 150 frames).
        for (const char *n : {"32A", "32B", "33A", "33B", "34A", "34B", "35A", "35B", "39A", "48A", "49A"})
            QTest::newRow(n) << scenePath(n) << QString::fromLatin1(n) << ports << QString();
        // draft1x: the Stage 54–67 library (the frozen scripts in tools/scene-ref/drafts/).
        for (const char *n : {"54B", "55A", "55B", "56A", "56B", "56D", "57A", "57B", "57C", "57D", "57E", "57F", "58A",
                            "58B", "58C", "58D", "58E", "58F", "59A", "59C", "59D", "59E", "59F", "60A", "60C", "60D",
                            "61A", "61B", "61C", "61D", "61E", "62A", "62B", "62C", "62D", "63A", "63B", "63C", "63D",
                            "64A", "64B", "64C", "64D", "65A", "65B", "65C", "65D", "66A", "66B", "66C", "66D", "67A",
                            "67B", "67C", "67D"})
            QTest::newRow(n) << scenePath(n) << QString::fromLatin1(n) << ports << QString();
        // draft1x: the favourite Filmora styles that stand still (5 s).
        for (const char *n : {"68_Art27", "68_Art30", "68_Art33", "69E_OLD_TEXTURE"})
            QTest::newRow(n) << scenePath(n) << QString::fromLatin1(n) << QList<int>{10, 40, 100} << QString();
        // draft1x: the animated Stage 69 favourites (5 s; frames in, holding, out).
        for (const char *n : {"69A_FILM_FRAME_03", "69B_CLEAN_TITLE_14", "69C_LOWER_THIRDS_22", "69D_BASIC_FONT_24"})
            QTest::newRow(n) << scenePath(n) << QString::fromLatin1(n) << QList<int>{5, 15, 40, 100, 120} << QString();
        // Batch 1 ports, Part5 Stage05 to Stage11 (identical to their scripts on all 150 frames).
        QTest::newRow("05_TutorialStep") << scenePath("05_TutorialStep") << QStringLiteral("05_TutorialStep") << ports << QString();
        QTest::newRow("05_PrecisionCallout") << scenePath("05_PrecisionCallout") << QStringLiteral("05_PrecisionCallout") << ports << QString();
        QTest::newRow("05_PullQuote") << scenePath("05_PullQuote") << QStringLiteral("05_PullQuote") << ports << QString();
        QTest::newRow("06_Metric") << scenePath("06_Metric") << QStringLiteral("06_Metric") << ports << QString();
        QTest::newRow("06_Location") << scenePath("06_Location") << QStringLiteral("06_Location") << ports << QString();
        QTest::newRow("07_Kinetic") << scenePath("07_Kinetic") << QStringLiteral("07_Kinetic") << ports << QString();
        QTest::newRow("07_Insights_v2") << scenePath("07_Insights_v2") << QStringLiteral("07_Insights_v2") << ports << QString();
        QTest::newRow("07_QA_v3") << scenePath("07_QA_v3") << QStringLiteral("07_QA_v3") << ports << QString();
        QTest::newRow("08_Cinematic_Garamond") << scenePath("08_Cinematic_Garamond") << QStringLiteral("08_Cinematic_Garamond") << ports << QString();
        QTest::newRow("08_Cinematic_Inter") << scenePath("08_Cinematic_Inter") << QStringLiteral("08_Cinematic_Inter") << ports << QString();
        QTest::newRow("08_Cinematic_RobotoSlab") << scenePath("08_Cinematic_RobotoSlab") << QStringLiteral("08_Cinematic_RobotoSlab") << ports << QString();
        QTest::newRow("08_Cinematic_Lato") << scenePath("08_Cinematic_Lato") << QStringLiteral("08_Cinematic_Lato") << ports << QString();
        QTest::newRow("10_TypographyMorph") << scenePath("10_TypographyMorph") << QStringLiteral("10_TypographyMorph") << ports << QString();
        QTest::newRow("10_MaskedLightReveal") << scenePath("10_MaskedLightReveal") << QStringLiteral("10_MaskedLightReveal") << ports << QString();
        QTest::newRow("10_PrecisionFocus") << scenePath("10_PrecisionFocus") << QStringLiteral("10_PrecisionFocus") << ports << QString();
        QTest::newRow("11_ArcResolve") << scenePath("11_ArcResolve") << QStringLiteral("11_ArcResolve") << ports << QString();
        QTest::newRow("11_PhraseReflow") << scenePath("11_PhraseReflow") << QStringLiteral("11_PhraseReflow") << ports << QString();
        QTest::newRow("09_WordRelay_A1") << scenePath("09_WordRelay_A1") << QStringLiteral("09_WordRelay_A1") << ports << QString();
        QTest::newRow("09_SplitMotion_C1") << scenePath("09_SplitMotion_C1") << QStringLiteral("09_SplitMotion_C1") << ports << QString();
        QTest::newRow("09_WordShift_B7") << scenePath("09_WordShift_B7") << QStringLiteral("09_WordShift_B7") << ports << QString();
        QTest::newRow("04_Chapter_A_v4") << scenePath("04_Chapter_A_v4") << QStringLiteral("04_Chapter_A_v4") << ports << QString();
        QTest::newRow("04_Chapter_B_v4") << scenePath("04_Chapter_B_v4") << QStringLiteral("04_Chapter_B_v4") << ports << QString();
        QTest::newRow("04_Dark_Chapter_A_v2") << scenePath("04_Dark_Chapter_A_v2") << QStringLiteral("04_Dark_Chapter_A_v2") << ports << QString();
    }

    // Reference frames: tools/scene-ref/render.py render <scene> --frames <frames> --no-mp4.
    // (The gate against the approved MP4s themselves is tools/scene-ref/native_compare.py.)
    void nativeMatchesReference()
    {
        QFETCH(QString, scene);
        QFETCH(QString, name);
        QFETCH(QList<int>, frames);
        QFETCH(QString, params);
        const skia::SceneBuild build = skia::sceneRenderer(readJson(scene), QJsonDocument::fromJson(params.toUtf8()).object());
        QVERIFY2(build.renderer, qPrintable(build.error));
        const scene::Scene &sc = skia::sceneOf(*build.renderer);
        const QHash<QString, QRectF> boxes = skia::sceneRestBoxes(*build.renderer);
        for (int frame : std::as_const(frames)) {
            const QImage ref = QImage(QStringLiteral(DRIFT_TEST_DATA_DIR "/scene-titles/%1/%2.png")
                                          .arg(name)
                                          .arg(frame, 4, 10, QLatin1Char('0')))
                                   .convertToFormat(QImage::Format_ARGB32_Premultiplied);
            QVERIFY(!ref.isNull());
            const double t = scene::designTime(sc, sc.duration, sc.fps, frame / sc.fps);
            const QImage got = skia::renderSceneFrame(build.renderer, t);
            QCOMPARE(got.size(), ref.size());
            const QImage a = overBase(ref), b = overBase(got);
            for (auto it = boxes.begin(); it != boxes.end(); ++it) {
                const scene::Actor *actor = sc.actor(it.key());
                const scene::Channels ch = scene::drawChannels(sc, *actor, t, nullptr);
                if (ch.opacity * ch.wipe * ch.progress <= 0.05)
                    continue;
                const QRect r = it.value().toAlignedRect().adjusted(-20, -20, 20, 20).intersected(a.rect());
                if (r.width() < 11 || r.height() < 11)
                    continue;
                const double s = ssim(luma(a, r), luma(b, r), r.width(), r.height());
                // Position on the actor's own box: the SSIM margin reaches the neighbouring
                // progress segments, whose ends would pull a centroid.
                const double shift = centroidShift(ref, got, it.value().toAlignedRect().adjusted(-4, -4, 4, 4).intersected(a.rect()));
                QVERIFY2(s >= 0.97, qPrintable(QStringLiteral("%1 frame %2 %3: ssim %4").arg(name).arg(frame).arg(it.key()).arg(s)));
                QVERIFY2(shift <= 0.5, qPrintable(QStringLiteral("%1 frame %2 %3: shift %4 px").arg(name).arg(frame).arg(it.key()).arg(shift)));
            }
        }
    }

    // Phase B with the painter's measurement (PIL's getbbox mirrored on Skia's FreeType glyphs)
    // against keycaps.layout with PIL's: widths and rows exact, every position within 0.5 px.
    void keycapsGeometryMatchesReference_data()
    {
        QTest::addColumn<QString>("scene");
        QTest::addColumn<QString>("params");
        QTest::addColumn<QString>("expected");
        keycapRows();
    }

    void keycapsGeometryMatchesReference()
    {
        QFETCH(QString, scene);
        QFETCH(QString, params);
        QFETCH(QString, expected);
        const skia::SceneBuild build =
            skia::sceneRenderer(readJson(scene), QJsonDocument::fromJson(params.toUtf8()).object());
        QVERIFY2(build.renderer, qPrintable(build.error));
        QFile f(QStringLiteral(DRIFT_TEST_DATA_DIR "/scene-titles/geo_%1.txt").arg(expected));
        QVERIFY(f.open(QIODevice::ReadOnly));
        const QStringList want = QString::fromUtf8(f.readAll()).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        const QStringList got = geometry(skia::sceneOf(*build.renderer));
        QCOMPARE(got.size(), want.size());
        for (int i = 0; i < want.size(); ++i) {
            const QString bad = geometryMismatch(got.at(i), want.at(i));
            QVERIFY2(bad.isEmpty(), qPrintable(bad));
        }
    }

    // Editing the shortcut param lays the keys out again: the approved lock holds only for the
    // approved Ctrl+Shift+S, any other shortcut gets the automatic widths (test_keycaps.py).
    void keycapsRelayoutFollowsTheShortcut()
    {
        const QJsonObject doc = readJson(scenePath("13_A_Shortcut_Keys"));
        const auto keys = [](const skia::SceneBuild &b) {
            QList<double> w;
            for (const scene::Actor &a : skia::sceneOf(*b.renderer).actors)
                if (a.role == QLatin1String("keycap"))
                    w << a.groupW;
            return w;
        };
        const skia::SceneBuild def = skia::sceneRenderer(doc);
        QVERIFY2(def.renderer, qPrintable(def.error));
        QCOMPARE(keys(def), (QList<double>{213, 250, 112}));
        const skia::SceneBuild edit = skia::sceneRenderer(doc, {{QStringLiteral("shortcut"), QStringLiteral("Ctrl+C")}});
        QVERIFY2(edit.renderer, qPrintable(edit.error));
        QCOMPARE(keys(edit), (QList<double>{188, 116}));
        const scene::Scene &sc = skia::sceneOf(*edit.renderer);
        QVERIFY(sc.actor(QStringLiteral("keys.a0c0k1")) && !sc.actor(QStringLiteral("keys.a0c0k2")));
        QCOMPARE(sc.actor(QStringLiteral("keys.a0c0k1"))->groupAt.x(), 604.0); // keycaps.layout: 347 + 188 + gap + the "+" sprite
        // The rest of the title does not move.
        QCOMPARE(sc.actor(QStringLiteral("title"))->baseline, skia::sceneOf(*def.renderer).actor(QStringLiteral("title"))->baseline);
    }

    // Command is its own key and has no place on a Windows block: the error names it, at load and
    // in the renderer (what installing a file and the inspector's param edit report).
    void keycapsCommandOnWindowsIsAnError()
    {
        const QString want = QStringLiteral("keycaps: 'command' is a macOS key and has no place on a Windows block");
        const QJsonObject doc = readJson(scenePath("13_A_Shortcut_Keys"));
        QString error;
        QVERIFY(!scene::loadScene(doc, {{QStringLiteral("shortcut"), QStringLiteral("Command+C")}}, &error));
        QCOMPARE(error, want);
        const skia::SceneBuild build = skia::sceneRenderer(doc, {{QStringLiteral("shortcut"), QStringLiteral("Cmd+C")}});
        QVERIFY(!build.renderer);
        QCOMPARE(build.error, want);
        QVERIFY(!scene::loadScene(readJson(scenePath("T09_stacked")),
                                  {{QStringLiteral("shortcutWindows"), QStringLiteral("Command+V")}}, &error));
        QCOMPARE(error, want);
        // A Mac block takes it, and Win is the one a Mac block refuses.
        QVERIFY(scene::loadScene(readJson(scenePath("13C_U3_Mac_Dual_Names_Force_Quit")),
                                 {{QStringLiteral("shortcutMac"), QStringLiteral("Command+C")}}));
        QVERIFY(!scene::loadScene(readJson(scenePath("13C_U3_Mac_Dual_Names_Force_Quit")),
                                  {{QStringLiteral("shortcutMac"), QStringLiteral("Win+C")}}, &error));
        QCOMPARE(error, QStringLiteral("keycaps: 'super' has no macOS legend"));
        QVERIFY(!scene::loadScene(doc, {{QStringLiteral("shortcut"), QStringLiteral("Ctrl++C")}}, &error));
        QCOMPARE(error, QStringLiteral("keycaps: empty key in 'Ctrl++C' (a \"+\" key is written \"plus\")"));
        QVERIFY(!scene::loadScene(doc, {{QStringLiteral("shortcut"), QStringLiteral("hyper")}}, &error));
        QCOMPARE(error, QStringLiteral("keycaps: unknown key 'hyper'; use custom:hyper for a legend of your own"));
    }

    // tools/scene-ref/test_keycaps.py, Dictionary and Grammar, on the native side.
    void keycapsDictionaryAndGrammar()
    {
        QString error;
        QVERIFY2(!scene::keycaps::dictionary(&error).isEmpty(), qPrintable(error));
        const auto key = [](const char *token, const char *platform = "windows", const char *policy = "names",
                            const QJsonObject &legends = {}) {
            return scene::keycaps::resolveKey(QString::fromUtf8(token), QLatin1String(platform), QLatin1String(policy), legends);
        };
        QCOMPARE(key("Option")->primary, QStringLiteral("ALT"));
        QCOMPARE(key("alt", "macos", "glyphPlusNames")->primary, QStringLiteral("\u2325"));
        QCOMPARE(key("alt", "macos", "glyphPlusNames")->secondary, QStringLiteral("Option / Alt"));
        QCOMPARE(key("Alt", "macos", "names")->primary, QStringLiteral("Option"));
        for (const char *alias : {"command", "cmd", "\u2318"}) {
            QCOMPARE(key(alias, "macos", "glyph")->ident, QStringLiteral("command"));
            QVERIFY(!key(alias));
        }
        QVERIFY(!key("win", "macos", "glyph"));
        QCOMPARE(key("mod")->primary, QStringLiteral("CTRL"));
        QCOMPARE(key("mod", "macos", "glyph")->primary, QStringLiteral("\u2318"));
        QCOMPARE(key("Esc", "macos", "glyphPlusNames")->secondary, QString());
        QCOMPARE(key("alt", "macos", "physical", {{QStringLiteral("alt"), QStringLiteral("Alt")}})->primary, QStringLiteral("Alt"));
        QCOMPARE(key("shift", "macos", "physical")->primary, QStringLiteral("SHIFT"));
        QCOMPARE(key("c")->primary, QStringLiteral("C"));
        QCOMPARE(key("f12")->primary, QStringLiteral("F12"));
        QCOMPARE(key("custom:Macro 7")->primary, QStringLiteral("Macro 7"));
        for (const char *bad : {"f25", "hyper", "custom:"})
            QVERIFY(!key(bad));
        QVERIFY(key("space")->wide);

        using Parsed = QList<QList<QStringList>>;
        QCOMPARE(*scene::keycaps::parseShortcut(QStringLiteral("Ctrl+K THEN Ctrl+C")),
                 (Parsed{{{QStringLiteral("Ctrl"), QStringLiteral("K")}, {QStringLiteral("Ctrl"), QStringLiteral("C")}}}));
        QCOMPARE(*scene::keycaps::parseShortcut(QStringLiteral("ctrl+y or Ctrl+Shift+Z")),
                 (Parsed{{{QStringLiteral("ctrl"), QStringLiteral("y")}},
                         {{QStringLiteral("Ctrl"), QStringLiteral("Shift"), QStringLiteral("Z")}}}));
        for (const char *bad : {"", "   ", "Ctrl+", "+C", "Ctrl++C"})
            QVERIFY(!scene::keycaps::parseShortcut(QLatin1String(bad)));
    }

    // The generator's group fields, a wrapped "+" hidden and out of the rest boxes, and the catalog
    // listing every shortcut title now that they load natively.
    void keycapsScenesLoadNatively()
    {
        const scene::Scene u3 = *scene::loadScene(readJson(scenePath("13C_U3_Mac_Dual_Names_Force_Quit")));
        const scene::Scene a13 = *scene::loadScene(readJson(scenePath("13_A_Shortcut_Keys")));
        QCOMPARE(u3.actor(QStringLiteral("keysMac.a0c0k0"))->ss, 3);
        QVERIFY(!u3.actor(QStringLiteral("keysMac.a0c0k0"))->snapTrunc);
        QCOMPARE(a13.actor(QStringLiteral("keys.a0c0k0"))->ss, 2);
        QVERIFY(a13.actor(QStringLiteral("keys.a0c0k0"))->snapTrunc);

        const skia::SceneBuild wrap = skia::sceneRenderer(
            readJson(scenePath("T16_vertical-9x16")),
            {{QStringLiteral("shortcutWindows"), QStringLiteral("Ctrl+Alt+Shift+Win+Esc+Tab+F2+F3")}});
        QVERIFY2(wrap.renderer, qPrintable(wrap.error));
        const QHash<QString, QRectF> boxes = skia::sceneRestBoxes(*wrap.renderer);
        int hidden = 0;
        for (const scene::Actor &a : skia::sceneOf(*wrap.renderer).actors)
            if (a.hidden) {
                ++hidden;
                QVERIFY(!boxes.contains(a.id));
            }
        QVERIFY(hidden > 0);

        scene::SceneCatalog::instance().reload();
        int shortcuts = 0;
        for (const scene::SceneEntry &e : scene::SceneCatalog::instance().entries())
            if (e.origin == QLatin1String("builtin") && e.category == QLatin1String("Keyboard Shortcuts"))
                ++shortcuts;
        // 13_A, 13C U1-U3, T01-T18, and the 7 kept variants (13B A1-A4, 13D U1-U3)
        QCOMPARE(shortcuts, 29);
        for (const char *name : {"13B_A1_Single_Function_Key", "13B_A2_Two_Mixed_Keys", "13B_A3_Four_Mixed_Keys",
                                 "13B_A4_Wide_Spacebar", "13D_U1_Universal_Horizontal_Copy_EN",
                                 "13D_U2_Universal_Vertical_Paste_EN", "13D_U3_Mac_Dual_Names_Force_Quit_EN"})
            QVERIFY2(scene::loadScene(readJson(scenePath(name))).has_value(), name);
    }

    void missingFontIsAnError()
    {
        QJsonObject doc = readJson(scenePath("03b_LT_B_25"));
        QJsonObject fonts = doc.value(QStringLiteral("fonts")).toObject();
        QJsonObject name = fonts.value(QStringLiteral("name")).toObject();
        name.insert(QStringLiteral("sha256"), QStringLiteral("00"));
        fonts.insert(QStringLiteral("name"), name);
        doc.insert(QStringLiteral("fonts"), fonts);
        const skia::SceneBuild build = skia::sceneRenderer(doc);
        QVERIFY(!build.renderer);
        QVERIFY(build.error.contains(QLatin1String("sha256")));
    }
};

QTEST_GUILESS_MAIN(SceneTitleTest)
#include "tst_scenetitle.moc"
