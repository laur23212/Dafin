// Dafin: Scene Titles (docs/scene-titles.md). The loader against tools/scene-ref/scene_core.py
// value for value, timing and errors, and the native renderer against the reference renderer's
// frames of the two approved ports at the spec's gate (actor SSIM >= 0.97, position <= 0.5 px).
// Headless: QImage and Skia's CPU raster only, so it runs on the Linux CI without a GPU.

#include "core/project/Project.h"
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
    }

    void coreMatchesReference()
    {
        QFETCH(QString, scene);
        QFETCH(QString, params);
        QFETCH(QString, expected);
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
        QTest::newRow("15A") << scenePath("15_A_Step_Focus") << QStringLiteral("15_A_Step_Focus");
        QTest::newRow("03b") << scenePath("03b_LT_B_25") << QStringLiteral("03b_LT_B_25");
        QTest::newRow("03b_LT_A_25") << scenePath("03b_LT_A_25") << QStringLiteral("03b_LT_A_25");
        QTest::newRow("03b_LT_A_60") << scenePath("03b_LT_A_60") << QStringLiteral("03b_LT_A_60");
        QTest::newRow("03b_LT_B_60") << scenePath("03b_LT_B_60") << QStringLiteral("03b_LT_B_60");
        QTest::newRow("03b_LT_C_25_REV") << scenePath("03b_LT_C_25_REV") << QStringLiteral("03b_LT_C_25_REV");
        QTest::newRow("03b_LT_C_60_REV") << scenePath("03b_LT_C_60_REV") << QStringLiteral("03b_LT_C_60_REV");
        QTest::newRow("05_TutorialStep") << scenePath("05_TutorialStep") << QStringLiteral("05_TutorialStep");
        QTest::newRow("05_PrecisionCallout") << scenePath("05_PrecisionCallout") << QStringLiteral("05_PrecisionCallout");
        QTest::newRow("05_PullQuote") << scenePath("05_PullQuote") << QStringLiteral("05_PullQuote");
        QTest::newRow("06_Metric") << scenePath("06_Metric") << QStringLiteral("06_Metric");
        QTest::newRow("06_Location") << scenePath("06_Location") << QStringLiteral("06_Location");
        QTest::newRow("07_Kinetic") << scenePath("07_Kinetic") << QStringLiteral("07_Kinetic");
        QTest::newRow("07_Insights_v2") << scenePath("07_Insights_v2") << QStringLiteral("07_Insights_v2");
        QTest::newRow("07_QA_v3") << scenePath("07_QA_v3") << QStringLiteral("07_QA_v3");
        QTest::newRow("08_Cinematic_Garamond") << scenePath("08_Cinematic_Garamond") << QStringLiteral("08_Cinematic_Garamond");
        QTest::newRow("08_Cinematic_Inter") << scenePath("08_Cinematic_Inter") << QStringLiteral("08_Cinematic_Inter");
        QTest::newRow("08_Cinematic_RobotoSlab") << scenePath("08_Cinematic_RobotoSlab") << QStringLiteral("08_Cinematic_RobotoSlab");
        QTest::newRow("08_Cinematic_Lato") << scenePath("08_Cinematic_Lato") << QStringLiteral("08_Cinematic_Lato");
        QTest::newRow("11_ArcResolve") << scenePath("11_ArcResolve") << QStringLiteral("11_ArcResolve");
        QTest::newRow("11_PhraseReflow") << scenePath("11_PhraseReflow") << QStringLiteral("11_PhraseReflow");
        QTest::newRow("11_AnchorNote_v4") << scenePath("11_AnchorNote_v4") << QStringLiteral("11_AnchorNote_v4");
        QTest::newRow("12_A_Anchor_Bracket") << scenePath("12_A_Anchor_Bracket") << QStringLiteral("12_A_Anchor_Bracket");
        QTest::newRow("12_B_Anchor_Measure") << scenePath("12_B_Anchor_Measure") << QStringLiteral("12_B_Anchor_Measure");
        QTest::newRow("12B_C_Anchor_Index_Manual") << scenePath("12B_C_Anchor_Index_Manual") << QStringLiteral("12B_C_Anchor_Index_Manual");
        QTest::newRow("09_WordRelay_A1") << scenePath("09_WordRelay_A1") << QStringLiteral("09_WordRelay_A1");
        QTest::newRow("09_SplitMotion_C1") << scenePath("09_SplitMotion_C1") << QStringLiteral("09_SplitMotion_C1");
        QTest::newRow("09_WordShift_B7") << scenePath("09_WordShift_B7") << QStringLiteral("09_WordShift_B7");
        QTest::newRow("04_Chapter_A_v4") << scenePath("04_Chapter_A_v4") << QStringLiteral("04_Chapter_A_v4");
        QTest::newRow("04_Chapter_B_v4") << scenePath("04_Chapter_B_v4") << QStringLiteral("04_Chapter_B_v4");
        QTest::newRow("04_Dark_Chapter_A_v2") << scenePath("04_Dark_Chapter_A_v2") << QStringLiteral("04_Dark_Chapter_A_v2");
        QTest::newRow("13_A_Shortcut_Keys") << scenePath("13_A_Shortcut_Keys") << QStringLiteral("13_A_Shortcut_Keys");
        QTest::newRow("13_B_Source_Credit") << scenePath("13_B_Source_Credit") << QStringLiteral("13_B_Source_Credit");
        QTest::newRow("13_C_Editorial_Checklist") << scenePath("13_C_Editorial_Checklist") << QStringLiteral("13_C_Editorial_Checklist");
        QTest::newRow("13C_U1_Universal_Horizontal_Copy") << scenePath("13C_U1_Universal_Horizontal_Copy") << QStringLiteral("13C_U1_Universal_Horizontal_Copy");
        QTest::newRow("13C_U2_Universal_Vertical_Paste") << scenePath("13C_U2_Universal_Vertical_Paste") << QStringLiteral("13C_U2_Universal_Vertical_Paste");
        QTest::newRow("13C_U3_Mac_Dual_Names_Force_Quit") << scenePath("13C_U3_Mac_Dual_Names_Force_Quit") << QStringLiteral("13C_U3_Mac_Dual_Names_Force_Quit");
        QTest::newRow("14_A_Menu_Path") << scenePath("14_A_Menu_Path") << QStringLiteral("14_A_Menu_Path");
        QTest::newRow("14_B_Setting_Change") << scenePath("14_B_Setting_Change") << QStringLiteral("14_B_Setting_Change");
        QTest::newRow("15_B_Definition_Card") << scenePath("15_B_Definition_Card") << QStringLiteral("15_B_Definition_Card");
        QTest::newRow("15_C_Pro_Tip") << scenePath("15_C_Pro_Tip") << QStringLiteral("15_C_Pro_Tip");
    }

    // Reference frames: tools/scene-ref/render.py render <scene> --frames 10,20,40,65,135 --no-mp4.
    // (The gate against the approved MP4s themselves is tools/scene-ref/native_compare.py.)
    void nativeMatchesReference()
    {
        QFETCH(QString, scene);
        QFETCH(QString, name);
        const skia::SceneBuild build = skia::sceneRenderer(readJson(scene));
        QVERIFY2(build.renderer, qPrintable(build.error));
        const scene::Scene &sc = skia::sceneOf(*build.renderer);
        const QHash<QString, QRectF> boxes = skia::sceneRestBoxes(*build.renderer);
        for (int frame : {10, 20, 40, 65, 135}) {
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
