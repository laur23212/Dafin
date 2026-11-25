// Dafin: Scene Titles (docs/scene-titles.md). The loader against tools/scene-ref/scene_core.py
// value for value, timing and errors, and the native renderer against the reference renderer's
// frames of the approved ports at the spec's gate (actor SSIM >= 0.97, position <= 0.5 px).
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
        const QList<int> ports{10, 20, 40, 65, 135};
        QTest::newRow("15A") << scenePath("15_A_Step_Focus") << QStringLiteral("15_A_Step_Focus") << ports;
        QTest::newRow("03b") << scenePath("03b_LT_B_25") << QStringLiteral("03b_LT_B_25") << ports;
        // Smart Keycaps: the approved shortcut titles, two entrance frames and two of the hold. The
        // reference renders them identical to the approved scripts (test_parity.py --keycaps13
        // --keycaps13c), and the U1/U2/U3 holds to the approved source holds.
        const QList<int> keys{25, 40, 75, 110};
        QTest::newRow("13A") << scenePath("13_A_Shortcut_Keys") << QStringLiteral("13_A_Shortcut_Keys") << keys;
        QTest::newRow("13C U1") << scenePath("13C_U1_Universal_Horizontal_Copy")
                                << QStringLiteral("13C_U1_Universal_Horizontal_Copy") << keys;
        QTest::newRow("13C U2") << scenePath("13C_U2_Universal_Vertical_Paste")
                                << QStringLiteral("13C_U2_Universal_Vertical_Paste") << keys;
        QTest::newRow("13C U3") << scenePath("13C_U3_Mac_Dual_Names_Force_Quit")
                                << QStringLiteral("13C_U3_Mac_Dual_Names_Force_Quit") << keys;
    }

    // Reference frames: tools/scene-ref/render.py render <scene> --frames <frames> --no-mp4.
    // (The gate against the approved MP4s themselves is tools/scene-ref/native_compare.py.)
    void nativeMatchesReference()
    {
        QFETCH(QString, scene);
        QFETCH(QString, name);
        QFETCH(QList<int>, frames);
        const skia::SceneBuild build = skia::sceneRenderer(readJson(scene));
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
        QCOMPARE(shortcuts, 22);
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
