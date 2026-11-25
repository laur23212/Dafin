#include "app/SceneTool.h"

#include "core/scene/SceneTitle.h"
#include "engine/render2d/scene/ScenePainter.h"

#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <cmath>
#include <cstdio>

namespace drift {

namespace {

int usage()
{
    std::fputs("usage: dafin --render-scene <file.scene.json> --out <dir> [--frames 0,10,20|all] [--fps N] "
               "[--duration S] [--param name=value]... [--font-dir <dir>]... [--matte none|base|reference] "
               "[--size WxH]\n",
               stderr);
    return 2;
}

} // namespace

int runSceneTool(int argc, char *argv[])
{
    // Fonts and PNG writing need a Gui application but no display.
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM"))
        qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);
    const QStringList args = app.arguments();

    QString path, out, frames, matte = QStringLiteral("none"), sizeText;
    double fps = 0.0, duration = 0.0;
    QJsonObject params;
    QStringList fontDirs;
    for (int i = 1; i < args.size(); ++i) {
        const QString &a = args.at(i);
        const auto next = [&]() { return i + 1 < args.size() ? args.at(++i) : QString(); };
        if (a == QLatin1String("--render-scene"))
            path = next();
        else if (a == QLatin1String("--out"))
            out = next();
        else if (a == QLatin1String("--frames"))
            frames = next();
        else if (a == QLatin1String("--fps"))
            fps = next().toDouble();
        else if (a == QLatin1String("--duration"))
            duration = next().toDouble();
        else if (a == QLatin1String("--matte"))
            matte = next();
        else if (a == QLatin1String("--size"))
            sizeText = next();
        else if (a == QLatin1String("--font-dir"))
            fontDirs << next();
        else if (a == QLatin1String("--param")) {
            const QString kv = next();
            const int eq = kv.indexOf(QLatin1Char('='));
            if (eq <= 0)
                return usage();
            const QByteArray value = kv.mid(eq + 1).toUtf8();
            QJsonParseError err;
            const QJsonDocument d = QJsonDocument::fromJson("[" + value + "]", &err);
            params.insert(kv.left(eq), err.error == QJsonParseError::NoError && d.array().size() == 1
                                           ? d.array().at(0)
                                           : QJsonValue(QString::fromUtf8(value)));
        }
    }
    if (path.isEmpty() || out.isEmpty()
        || (matte != QLatin1String("none") && matte != QLatin1String("base") && matte != QLatin1String("reference")))
        return usage();

    QFile file(path);
    QJsonParseError parseError;
    const QJsonDocument doc = file.open(QIODevice::ReadOnly) ? QJsonDocument::fromJson(file.readAll(), &parseError)
                                                             : QJsonDocument();
    if (!doc.isObject()) {
        std::fprintf(stderr, "scene error: %s is not a JSON object\n", qPrintable(path));
        return 1;
    }
    const skia::SceneBuild build = skia::sceneRenderer(doc.object(), params, fontDirs);
    if (!build.renderer) {
        std::fprintf(stderr, "scene error: %s\n", qPrintable(build.error));
        return 1;
    }
    const scene::Scene &sc = skia::sceneOf(*build.renderer);
    if (fps <= 0.0)
        fps = sc.fps;
    if (duration <= 0.0)
        duration = sc.duration;
    QSize size(sc.width, sc.height);
    if (!sizeText.isEmpty()) {
        const QStringList wh = sizeText.split(QLatin1Char('x'));
        if (wh.size() != 2 || wh[0].toInt() <= 0 || wh[1].toInt() <= 0)
            return usage();
        size = QSize(wh[0].toInt(), wh[1].toInt());
    }
    const int count = int(std::lround(duration * fps));
    QList<int> list;
    if (frames.isEmpty() || frames == QLatin1String("all")) {
        for (int i = 0; i < count; ++i)
            list << i;
    } else {
        for (const QString &f : frames.split(QLatin1Char(','), Qt::SkipEmptyParts))
            list << f.trimmed().toInt();
    }

    const QJsonObject bg = sc.raw.value(QStringLiteral("preview")).toObject().value(QStringLiteral("background")).toObject();
    const QString bgKind = bg.value(QStringLiteral("kind")).toString(QStringLiteral("base"));
    // "reference" draws the scene's own matte when it is a picture we make (BASE or a flat colour).
    const bool baseMatte = matte == QLatin1String("base")
                        || (matte == QLatin1String("reference") && (bgKind == QLatin1String("base") || bgKind == QLatin1String("solid")));
    QString note;
    if (matte == QLatin1String("reference") && !baseMatte)
        note = QStringLiteral("the %1 background is composited by tools/scene-ref/native_compare.py").arg(bgKind);

    QDir dir(out);
    if (!dir.mkpath(QStringLiteral("alpha")) || (baseMatte && !dir.mkpath(QStringLiteral("matte")))) {
        std::fprintf(stderr, "could not create %s\n", qPrintable(out));
        return 1;
    }
    const QImage base = !baseMatte ? QImage()
                      : matte == QLatin1String("reference") ? skia::scenePreviewMatte(sc, size)
                                                            : skia::sceneBaseMatte(size, sc.mixColor);
    for (int i : std::as_const(list)) {
        const double t = scene::designTime(sc, duration, fps, i / fps);
        const QImage frame = skia::renderSceneFrame(build.renderer, t, size);
        const QString name = QStringLiteral("%1.png").arg(i, 4, 10, QLatin1Char('0'));
        if (!frame.convertToFormat(QImage::Format_ARGB32).save(dir.filePath(QStringLiteral("alpha/") + name))) {
            std::fprintf(stderr, "could not write frame %d\n", i);
            return 1;
        }
        if (baseMatte)
            skia::compositeOverMatte(frame, base).convertToFormat(QImage::Format_RGB888).save(dir.filePath(QStringLiteral("matte/") + name));
    }
    QJsonObject summary{{QStringLiteral("scene"), sc.id},
                        {QStringLiteral("frames"), count},
                        {QStringLiteral("rendered"), int(list.size())},
                        {QStringLiteral("fps"), fps},
                        {QStringLiteral("renderer"), QStringLiteral("native")},
                        {QStringLiteral("params"), QJsonObject::fromVariantMap(sc.values)}};
    if (!note.isEmpty())
        summary.insert(QStringLiteral("note"), note);
    std::printf("%s\n", QJsonDocument(summary).toJson(QJsonDocument::Compact).constData());
    return 0;
}

} // namespace drift
