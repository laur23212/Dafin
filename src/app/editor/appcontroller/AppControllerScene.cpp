// Dafin: Scene Titles in the editor (docs/scene-titles.md) — the library (Titles › Scenes),
// installing a scene file with a report, placing one on the timeline as a single clip, and its
// params as inspector fields, each change one undo step.

#include "app/editor/appcontroller/AppControllerInternal.h"
#include "core/edit/TimelineOps.h"
#include "core/scene/SceneCatalog.h"
#include "core/scene/SceneTitle.h"
#ifdef DRIFT_WITH_SKIA
#include "engine/render2d/scene/ScenePainter.h"
#endif

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QSaveFile>
#include <QUuid>

#include <cmath>

namespace {

QString localPath(const QUrl &url)
{
    return url.isLocalFile() ? url.toLocalFile() : url.toString();
}

// QML hands numbers over as doubles; an int param must stay an integer to pass the check.
QJsonValue paramJson(const drift::scene::Scene &scene, const QString &id, const QVariant &value)
{
    for (const drift::scene::ParamSpec &p : scene.params) {
        if (p.id != id)
            continue;
        if (p.type == QLatin1String("int") && value.canConvert<double>()) {
            const double d = value.toDouble();
            if (std::floor(d) == d)
                return QJsonValue(qint64(d));
        }
        if (p.type == QLatin1String("number"))
            return QJsonValue(value.toDouble());
    }
    return QJsonValue::fromVariant(value);
}

} // namespace

QVariantList AppController::sceneTitles() const
{
    QVariantList out;
    for (const drift::scene::SceneEntry &e : drift::scene::SceneCatalog::instance().entries()) {
        const drift::scene::Scene probe = *drift::scene::loadScene(e.document);
        out << QVariantMap{{QStringLiteral("id"), e.id},
                           {QStringLiteral("label"), e.label},
                           {QStringLiteral("category"), e.category},
                           {QStringLiteral("origin"), e.origin},
                           {QStringLiteral("duration"), probe.duration},
                           {QStringLiteral("status"), e.document.value(QStringLiteral("source")).toObject()
                                                          .value(QStringLiteral("status")).toString()}};
    }
    return out;
}

QVariantMap AppController::checkSceneTitleFile(const QUrl &fileUrl) const
{
    const QString path = localPath(fileUrl);
    QVariantMap out{{QStringLiteral("path"), path}, {QStringLiteral("ok"), false}};
    QStringList problems;
    QJsonObject doc;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        problems << QStringLiteral("cannot open the file");
    } else {
        QJsonParseError err;
        const QJsonDocument d = QJsonDocument::fromJson(file.readAll(), &err);
        if (!d.isObject())
            problems << QStringLiteral("not JSON: %1 (offset %2)").arg(err.errorString()).arg(err.offset);
        doc = d.object();
    }
    if (problems.isEmpty()) {
        QString error;
        const std::optional<drift::scene::Scene> sc = drift::scene::loadScene(doc, {}, &error);
        if (!sc) {
            problems << error;
        } else {
            out.insert(QStringLiteral("id"), sc->id);
            out.insert(QStringLiteral("label"), sc->label);
#ifdef DRIFT_WITH_SKIA
            // The fonts are part of the check: a scene whose exact files are missing would not draw.
            const drift::skia::SceneBuild build = drift::skia::sceneRenderer(doc);
            if (!build.renderer)
                problems << build.error;
#endif
        }
    }
    out.insert(QStringLiteral("ok"), problems.isEmpty());
    QVariantList issues;
    for (const QString &p : std::as_const(problems))
        issues << QVariantMap{{QStringLiteral("severity"), QStringLiteral("error")}, {QStringLiteral("message"), p}};
    out.insert(QStringLiteral("issues"), issues);
    // The block a user pastes back to the scene's author, word for word.
    out.insert(QStringLiteral("report"),
               QStringLiteral("Dafin scene title check for %1 (format %2 v%3)\n%4")
                   .arg(QFileInfo(path).fileName(), QLatin1String(drift::scene::kFormat))
                   .arg(drift::scene::kVersion)
                   .arg(problems.isEmpty() ? QStringLiteral("no problems found")
                                           : QStringLiteral("error: ") + problems.join(QStringLiteral("\nerror: "))));
    return out;
}

QVariantMap AppController::installSceneTitle(const QUrl &fileUrl)
{
    QVariantMap result = checkSceneTitleFile(fileUrl);
    if (!result.value(QStringLiteral("ok")).toBool())
        return result;
    const QString dir = drift::scene::SceneCatalog::installedDir();
    if (dir.isEmpty() || !QDir().mkpath(dir)) {
        result.insert(QStringLiteral("ok"), false);
        return result;
    }
    QString name = result.value(QStringLiteral("id")).toString();
    for (QChar &c : name)
        if (!c.isLetterOrNumber() && c != QLatin1Char('.') && c != QLatin1Char('-') && c != QLatin1Char('_'))
            c = QLatin1Char('_');
    QFile source(result.value(QStringLiteral("path")).toString());
    QSaveFile out(QDir(dir).filePath(name + QStringLiteral(".json")));
    if (!source.open(QIODevice::ReadOnly) || !out.open(QIODevice::WriteOnly) || out.write(source.readAll()) < 0
        || !out.commit()) {
        result.insert(QStringLiteral("ok"), false);
        setLastMessage(tr("Could not install the scene title"), QStringLiteral("error"));
        return result;
    }
    drift::scene::SceneCatalog::instance().reload();
    emit sceneTitlesChanged();
    setLastMessage(tr("Installed “%1”").arg(result.value(QStringLiteral("label")).toString()), QStringLiteral("success"));
    return result;
}

bool AppController::uninstallSceneTitle(const QString &sceneId)
{
    const std::optional<drift::scene::SceneEntry> e = drift::scene::SceneCatalog::instance().find(sceneId);
    if (!e || e->origin != QLatin1String("installed") || !QFile::remove(e->path))
        return false;
    drift::scene::SceneCatalog::instance().reload();
    emit sceneTitlesChanged();
    return true;
}

QVariantMap AppController::addSceneTitleClip(const QString &sceneId, double atSeconds, int trackIndex)
{
    const std::optional<drift::scene::SceneEntry> entry = drift::scene::SceneCatalog::instance().find(sceneId);
    if (!entry)
        return {{QStringLiteral("ok"), false}, {QStringLiteral("error"), QStringLiteral("unknown scene title")}};
    QString error;
    const std::optional<drift::scene::Scene> sc = drift::scene::loadScene(entry->document, {}, &error);
    if (!sc)
        return {{QStringLiteral("ok"), false}, {QStringLiteral("error"), error}};

    const drift::Project before = m_project;
    const drift::TimeUs duration = qMax(drift::secondsToUs(sc->duration), drift::minClipDurationUs(m_project.fps()));
    const drift::TimeUs startUs = atSeconds < 0.0 ? m_playheadUs : drift::secondsToUs(atSeconds);
    int target = trackIndex;
    if (target < 0 || target >= m_project.tracks().size()
        || !m_project.tracks().at(target).allowsClipType(drift::ClipType::Scene)) {
        target = drift::ensureFreeTrackForClipType(m_project, drift::ClipType::Scene, startUs, duration, true);
    }
    if (target < 0)
        return {{QStringLiteral("ok"), false}, {QStringLiteral("error"), QStringLiteral("no graphic track")}};
    drift::Track &track = m_project.tracks()[target];
    const drift::TimeUs start = drift::resolveClipStart(m_project, track, -1, startUs, duration, m_snapEnabled, m_playheadUs);

    drift::Clip clip;
    clip.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    clip.type = drift::ClipType::Scene;
    clip.scene.document = entry->document;
    clip.name = sc->label;
    clip.timelineStart = start;
    clip.timelineDuration = duration;
    clip.srcIn = 0;
    clip.srcOut = duration;
    track.clips.append(clip);
    const int newIndex = int(track.clips.size()) - 1;
    pushProjectEdit(before, tr("Scene title added"));
    finishEdit(tr("Scene title added"));
    selectClip(target, newIndex);
#ifdef DRIFT_WITH_SKIA
    const drift::skia::SceneBuild build = drift::skia::sceneRenderer(clip.scene.document);
    if (!build.renderer)
        setLastMessage(tr("“%1” cannot be drawn: %2").arg(sc->label, build.error), QStringLiteral("error"));
#endif
    return {{QStringLiteral("ok"), true}, {QStringLiteral("id"), clip.id},
            {QStringLiteral("track"), target}, {QStringLiteral("index"), newIndex}};
}

QVariantList AppController::sceneParams(int trackIndex, int clipIndex) const
{
    if (!isValidClipIndex(trackIndex, clipIndex))
        return {};
    const drift::Clip &clip = m_project.tracks().at(trackIndex).clips.at(clipIndex);
    if (clip.type != drift::ClipType::Scene)
        return {};
    const std::optional<drift::scene::Scene> sc = drift::scene::loadScene(clip.scene.document, clip.scene.params);
    return sc ? drift::scene::paramRows(*sc) : QVariantList();
}

QVariantMap AppController::setSceneParam(int trackIndex, int clipIndex, const QString &paramId, const QVariant &value)
{
    if (!isValidClipIndex(trackIndex, clipIndex)
        || m_project.tracks().at(trackIndex).clips.at(clipIndex).type != drift::ClipType::Scene)
        return {{QStringLiteral("ok"), false}, {QStringLiteral("error"), QStringLiteral("not a scene title clip")}};
    const drift::Clip &current = m_project.tracks().at(trackIndex).clips.at(clipIndex);
    const std::optional<drift::scene::Scene> base = drift::scene::loadScene(current.scene.document, current.scene.params);
    if (!base)
        return {{QStringLiteral("ok"), false}, {QStringLiteral("error"), QStringLiteral("scene does not load")}};
    QJsonObject params = current.scene.params;
    params.insert(paramId, paramJson(*base, paramId, value));
    QString error;
    const std::optional<drift::scene::Scene> next = drift::scene::loadScene(current.scene.document, params, &error);
    if (!next)
        return {{QStringLiteral("ok"), false}, {QStringLiteral("error"), error}};
    // A clamped param keeps the value it was clamped to, so the field shows what is drawn.
    params.insert(paramId, QJsonValue::fromVariant(next->values.value(paramId)));
    if (params == current.scene.params)
        return {{QStringLiteral("ok"), true}};
    const drift::Project before = m_project;
    m_project.tracks()[trackIndex].clips[clipIndex].scene.params = params;
    pushProjectEdit(before, tr("Scene title changed"));
    finishEdit(tr("Scene title changed"));
    return {{QStringLiteral("ok"), true}};
}

bool AppController::resetSceneParams(int trackIndex, int clipIndex)
{
    if (!isValidClipIndex(trackIndex, clipIndex)
        || m_project.tracks().at(trackIndex).clips.at(clipIndex).type != drift::ClipType::Scene
        || m_project.tracks().at(trackIndex).clips.at(clipIndex).scene.params.isEmpty())
        return false;
    const drift::Project before = m_project;
    m_project.tracks()[trackIndex].clips[clipIndex].scene.params = {};
    pushProjectEdit(before, tr("Scene title reset"));
    finishEdit(tr("Scene title reset"));
    return true;
}
