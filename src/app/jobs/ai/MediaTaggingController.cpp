#include "app/jobs/ai/MediaTaggingController.h"

#include "app/bin/AssetLibrary.h"
#include "app/editor/AppController.h"
#include "app/jobs/JobRegistry.h"
#include "app/online/addons/AddonManager.h"
#include "core/base/LowSpec.h"
#include "core/base/SafeMode.h"
#include "core/project/Project.h"
#include "core/project/media/MediaTags.h"
#include "engine/audio/analysis/AudioEvents.h"
#include "engine/decode/probe/StillImage.h"
#include "engine/decode/readers/ClipReaderPool.h"
#include "engine/ml/audio/AstClassifier.h"
#include "engine/ml/audio/AudioTagging.h"
#include "engine/ml/audio/ClapTagger.h"
#include "engine/ml/speech/SpeechAudio.h"
#include "engine/ml/vision/detection/ObjectDetector.h"

#include <QFileInfo>
#include <QHash>
#include <QJsonObject>
#include <QMetaObject>
#include <QMutex>
#include <QPointer>
#include <QSettings>
#include <QTimer>

#include <algorithm>
#include <cmath>
#include <memory>

namespace {

// Its own decode cursor, so the frames it reads never move playback's (see ClipReaderPool).
constexpr quint64 kTagFrameStreamId = 0xDA'F1'7A'60'00'00'00'01ull;
constexpr int kVideoSamples = 5;
constexpr int kFrameEdge = 640;
// The audio tagger hears at most this much of a file, from its middle.
constexpr double kAudioSeconds = 60.0;
const QString kAutoTagKey = QStringLiteral("AI/autoTagImports");

struct Work
{
    QString assetId;
    QString path;
    drift::MediaKind kind = drift::MediaKind::Other;
    drift::TimeUs durationUs = 0;
    bool hasAudio = false;
    int rotation = 0;
};

struct Found
{
    QString assetId;
    QString path;
    QStringList tags;
};

// The clearest objects in a handful of frames.
QStringList visualTags(const Work &w, const std::function<bool(double)> &progress)
{
    drift::ObjectDetector &detector = drift::ObjectDetector::instance();
    if (!drift::ObjectDetector::modelPresent() || !detector.available())
        return {};
    QList<QList<drift::LabelHit>> samples;
    const auto look = [&](const QImage &frame) {
        QList<drift::LabelHit> hits;
        if (frame.isNull())
            return;
        for (const drift::Detection &d : detector.detect(frame))
            hits.append({d.label, d.score});
        samples.append(hits);
    };
    if (w.kind == drift::MediaKind::Image) {
        look(drift::decodeStillImage(w.path, kFrameEdge, kFrameEdge));
        progress(1.0);
    } else {
        for (int i = 0; i < kVideoSamples; ++i) {
            const drift::TimeUs at = drift::TimeUs((double(i) + 0.5) / kVideoSamples * double(w.durationUs));
            look(ClipReaderPool::instance().readVideoFrame(w.path, kTagFrameStreamId, at, kFrameEdge, kFrameEdge,
                                                           w.rotation));
            if (!progress(double(i + 1) / kVideoSamples))
                return {};
        }
    }
    if (samples.isEmpty())
        return {};
    drift::TagPickOptions options;
    if (samples.size() == 1)
        options.strongScore = 0.55; // a photo is looked at once, but never at a blurry mid-pan frame
    return drift::pickTags(samples, options);
}

// What the sound is: music genre and mood, instruments, ambience, voice, speech or music.
QStringList audioTags(const Work &w, const std::function<bool(double)> &progress)
{
    if (!drift::ClapTagger::modelPresent() || !drift::ClapTagger::instance().available())
        return {};
    const double duration = drift::usToSeconds(w.durationUs);
    double from = 0.0;
    double to = duration > 0 ? duration : kAudioSeconds;
    if (to - from > kAudioSeconds) {
        from = duration / 2.0 - kAudioSeconds / 2.0;
        to = from + kAudioSeconds;
    }
    bool cancelled = false;
    std::vector<float> mono = drift::readMono(
        w.path, drift::secondsToUs(from), drift::secondsToUs(to), drift::ClapTagger::kSampleRate,
        [&progress](double f) { return progress(0.3 * f); }, &cancelled);
    if (cancelled || mono.size() < size_t(drift::ClapTagger::kSampleRate))
        return {};
    // readMono folds to 0.5 * (L + R); the models were calibrated on ffmpeg's 0.707 * (L + R).
    for (float &v : mono)
        v *= 1.41421356f;

    const drift::AudioEventsResult events =
        drift::AudioEvents::analyze(mono.data(), qint64(mono.size()), drift::ClapTagger::kSampleRate, 0.0);
    drift::AudioTagOptions options;
    options.maxTagged = 6;
    options.sections = true;
    options.astEvents = drift::AstClassifier::modelPresent() ? 1 : 0;
    options.astSections = drift::AstClassifier::modelPresent() ? 2 : 0;
    options.progress = [&progress](double f) { return progress(0.3 + 0.7 * f); };
    const drift::AudioTags tags =
        drift::tagAudio(mono.data(), qint64(mono.size()), 0.0, events.events, options);
    if (!tags.clap || tags.cancelled)
        return {};

    // Each section is one sample. CLAP's calibrated z becomes a 0..1 score where "plausible"
    // (z 2.5) is the floor and "strong" (z 3) counts on its own.
    QList<QList<drift::LabelHit>> samples;
    for (const drift::ClapSection &section : tags.sections) {
        QList<drift::LabelHit> hits;
        for (int c = 0; c < drift::kClapSectionCategories; ++c) {
            for (const drift::ClapTag &t : section.tags[c])
                hits.append({drift::shortTagName(t.text), std::clamp((double(t.z) - 1.8) / 2.0, 0.0, 1.0)});
        }
        if (section.hasAst) {
            if (section.ast.speech > 0.5f)
                hits.append({QStringLiteral("speech"), double(section.ast.speech)});
            if (section.ast.music > 0.5f)
                hits.append({QStringLiteral("music"), double(section.ast.music)});
        }
        samples.append(hits);
    }
    drift::TagPickOptions pick;
    pick.weakScore = 0.35;
    pick.strongScore = 0.6;
    pick.maxTags = 5;
    return drift::pickTags(samples, pick);
}

} // namespace

MediaTaggingController::MediaTaggingController(AppController *app, QObject *parent)
    : QObject(parent)
    , m_app(app)
{
    m_autoTagImports = QSettings().value(kAutoTagKey, false).toBool();
    m_importTimer = new QTimer(this);
    m_importTimer->setSingleShot(true);
    m_importTimer->setInterval(1500);
    connect(m_importTimer, &QTimer::timeout, this, [this]() {
        const QStringList ids = m_arrived;
        m_arrived.clear();
        if (m_autoTagImports)
            tagAssets(ids, false);
    });
    if (m_app->m_assetLibrary) {
        connect(m_app->m_assetLibrary, &AssetLibrary::assetsImported, this,
                &MediaTaggingController::onAssetsImported);
        connect(m_app->m_assetLibrary, &AssetLibrary::countChanged, this, &MediaTaggingController::mediaChanged);
    }
    connect(m_app, &AppController::projectReset, this, [this]() {
        m_queued.clear();
        m_arrived.clear();
        cancel();
        emit mediaChanged();
    });
    // Tags land, are undone and redone through the undo stack; the lists follow it.
    connect(m_app, &AppController::undoStackChanged, this, &MediaTaggingController::mediaChanged);
    if (m_app->m_addonManager)
        connect(m_app->m_addonManager, &AddonManager::kindChanged, this, [this](const QString &kind) {
            if (kind == QLatin1String("audio-tag-model") || kind == QLatin1String("audio-class-model")
                || kind == QLatin1String("object-model") || kind == QLatin1String("onnxruntime"))
                refreshModels();
        });
    refreshModels();
}

void MediaTaggingController::refreshModels()
{
    const bool runtime = m_app->m_addonManager && m_app->m_addonManager->runtimeAvailable();
    const bool hear = runtime && drift::ClapTagger::modelPresent();
    const bool see = runtime && drift::ObjectDetector::modelPresent();
    if (hear == m_canHear && see == m_canSee)
        return;
    m_canHear = hear;
    m_canSee = see;
    emit modelsChanged();
}

bool MediaTaggingController::canHear() const
{
    return m_canHear;
}

bool MediaTaggingController::canSee() const
{
    return m_canSee;
}

void MediaTaggingController::setAutoTagImports(bool on)
{
    if (m_autoTagImports == on)
        return;
    m_autoTagImports = on;
    QSettings().setValue(kAutoTagKey, on);
    emit autoTagImportsChanged();
}

void MediaTaggingController::onAssetsImported(const QStringList &assetIds)
{
    if (!m_autoTagImports || drift::lowspec::active() || drift::safemode::active())
        return;
    for (const QString &id : assetIds) {
        if (!m_arrived.contains(id))
            m_arrived.append(id);
    }
    m_importTimer->start();
}

bool MediaTaggingController::taggable(const QString &assetId) const
{
    const drift::MediaAsset *asset = m_app->m_project.asset(assetId);
    if (!asset || asset->path.isEmpty() || !asset->sequenceId.isEmpty())
        return false;
    if (m_app->m_assetLibrary && m_app->m_assetLibrary->isMissing(assetId))
        return false;
    switch (asset->kind) {
    case drift::MediaKind::Video:
    case drift::MediaKind::Image:
        return m_canSee || (m_canHear && asset->kind == drift::MediaKind::Video);
    case drift::MediaKind::Audio:
        return m_canHear;
    default:
        return false;
    }
}

int MediaTaggingController::untaggedCount() const
{
    int n = 0;
    for (const QString &id : m_app->m_project.assetOrder()) {
        const drift::MediaAsset *asset = m_app->m_project.asset(id);
        if (asset && !asset->tagged && taggable(id))
            ++n;
    }
    return n;
}

QVariantList MediaTaggingController::taggedMedia() const
{
    QVariantList out;
    for (const QString &id : m_app->m_project.assetOrder()) {
        const drift::MediaAsset *asset = m_app->m_project.asset(id);
        if (!asset || asset->tags.isEmpty())
            continue;
        out.append(QVariantMap{{QStringLiteral("assetId"), asset->id},
                               {QStringLiteral("name"), asset->name},
                               {QStringLiteral("kind"), drift::mediaKindToString(asset->kind)},
                               {QStringLiteral("tags"), asset->tags}});
    }
    return out;
}

bool MediaTaggingController::tagUntagged()
{
    QStringList ids;
    for (const QString &id : m_app->m_project.assetOrder()) {
        const drift::MediaAsset *asset = m_app->m_project.asset(id);
        if (asset && !asset->tagged)
            ids.append(id);
    }
    return tagAssets(ids, false);
}

bool MediaTaggingController::tagAll()
{
    return tagAssets(m_app->m_project.assetOrder(), true);
}

bool MediaTaggingController::tagAssets(const QStringList &assetIds, bool again)
{
    if (drift::safemode::active()) {
        m_app->setLastMessage(tr("AI tools are off in safe mode. Restart normally to use them."),
                              QStringLiteral("warning"));
        return false;
    }
    if (drift::lowspec::active()) {
        m_app->setLastMessage(tr("Local AI is off in compatibility mode: this computer is below Dafin's "
                                 "recommended specs. Settings › Performance › Low-spec mode turns it back on."),
                              QStringLiteral("warning"));
        return false;
    }
    refreshModels();
    if (!m_canHear && !m_canSee) {
        m_app->setLastMessage(tr("Download the tagging models first (AI › Media auto-tagging)"),
                              QStringLiteral("warning"));
        return false;
    }
    QStringList ids;
    for (const QString &id : assetIds) {
        const drift::MediaAsset *asset = m_app->m_project.asset(id);
        if (!asset || ids.contains(id) || !taggable(id))
            continue;
        if (asset->tagged && !again)
            continue;
        ids.append(id);
    }
    if (ids.isEmpty())
        return false;
    if (running()) {
        for (const QString &id : std::as_const(ids)) {
            if (!m_queued.contains(id))
                m_queued.append(id);
        }
        return true;
    }
    return start(ids);
}

bool MediaTaggingController::start(const QStringList &assetIds)
{
    QList<Work> work;
    for (const QString &id : assetIds) {
        const drift::MediaAsset *asset = m_app->m_project.asset(id);
        if (!asset)
            continue;
        Work w;
        w.assetId = id;
        w.path = asset->path;
        w.kind = asset->kind;
        w.durationUs = asset->durationUs;
        // Not probed yet counts as "maybe": the audio read simply comes back empty when there is none.
        w.hasAudio = asset->kind == drift::MediaKind::Audio || !asset->hasAudioKnown || asset->hasAudio;
        w.rotation = drift::rotationCorrectionOf(*asset);
        work.append(w);
    }
    if (work.isEmpty())
        return false;

    // Frames come off the shared decoder pool, which playback must not be walking at the same time.
    if (std::any_of(work.cbegin(), work.cend(), [](const Work &w) { return w.kind == drift::MediaKind::Video; }))
        m_app->setPlaying(false);

    const bool hear = m_canHear;
    const bool see = m_canSee;
    setProgress(0.0);
    setStatus(tr("Getting the models ready…"));

    struct Shared
    {
        QMutex mutex;
        QList<Found> found;
    };
    auto shared = std::make_shared<Shared>();
    const QPointer<MediaTaggingController> self(this);
    const int total = int(work.size());
    auto report = [self, total](int index, double fraction, const QString &name) {
        QMetaObject::invokeMethod(
            self.data(),
            [self, index, fraction, name, total]() {
                if (!self || !self->running())
                    return;
                self->setProgress(std::min(0.99, (double(index) + std::clamp(fraction, 0.0, 1.0)) / total));
                self->setStatus(tr("Tagging “%1” (%2 of %3)…").arg(name).arg(index + 1).arg(total));
            },
            Qt::QueuedConnection);
    };

    m_jobId = m_app->m_jobs->start(
        QStringLiteral("auto_tag"), QStringLiteral("media"), JobRegistry::Lane::Model,
        [work, hear, see, shared, report](JobContext &ctx) {
            for (int i = 0; i < work.size(); ++i) {
                if (ctx.cancelled())
                    return;
                const Work &w = work.at(i);
                const QString name = QFileInfo(w.path).fileName();
                report(i, 0.0, name);
                const bool picture = w.kind == drift::MediaKind::Video || w.kind == drift::MediaKind::Image;
                const bool sound = w.hasAudio && w.kind != drift::MediaKind::Image;
                const double split = picture && see && sound && hear ? 0.5 : 1.0;
                QStringList seen, heard;
                if (picture && see) {
                    seen = visualTags(w, [&](double f) {
                        ctx.progress((i + split * f) / work.size());
                        report(i, split * f, name);
                        return !ctx.cancelled();
                    });
                }
                if (ctx.cancelled())
                    return;
                if (sound && hear) {
                    const double base = picture && see ? split : 0.0;
                    heard = audioTags(w, [&](double f) {
                        ctx.progress((i + base + (1.0 - base) * f) / work.size());
                        report(i, base + (1.0 - base) * f, name);
                        return !ctx.cancelled();
                    });
                }
                if (ctx.cancelled())
                    return;
                QMutexLocker lock(&shared->mutex);
                shared->found.append({w.assetId, w.path, drift::mergeTags(seen, heard)});
            }
            ctx.succeed({{QStringLiteral("tagged"), int(work.size())}});
        },
        [self, shared](const QJsonObject &job) {
            if (!self)
                return QJsonObject{};
            MediaTaggingController *me = self.data();
            const bool ok = job.value(QStringLiteral("ok")).toBool();
            const QString code = job.value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toString();
            QList<Found> found;
            {
                QMutexLocker lock(&shared->mutex);
                found = shared->found;
            }
            // What finished lands even when the run was cancelled part-way: one undo step.
            drift::Project &project = me->m_app->m_project;
            const drift::Project before = project;
            int changed = 0;
            int withTags = 0;
            for (const Found &f : std::as_const(found)) {
                drift::MediaAsset *asset = project.asset(f.assetId);
                // Removed, or pointed at another file, while it was being looked at.
                if (!asset || asset->path != f.path)
                    continue;
                withTags += f.tags.isEmpty() ? 0 : 1;
                if (asset->tagged && asset->tags == f.tags)
                    continue;
                asset->tags = f.tags;
                asset->tagged = true;
                ++changed;
            }
            if (changed > 0) {
                me->m_app->pushProjectEdit(before, tr("Auto-tag media"));
                me->m_app->finishEdit(tr("Auto-tag media"));
            }
            me->m_jobId.clear();
            emit me->runningChanged();
            emit me->mediaChanged();
            const bool cancelled = code == QLatin1String("cancelled") || (!ok && code.isEmpty());
            QString message;
            if (ok)
                message = withTags > 0 ? tr("Tagged %n media item(s)", "", withTags) : tr("Nothing recognisable was found");
            else if (cancelled)
                message = tr("Tagging cancelled; kept the tags of %n item(s) already done", "", withTags);
            else
                message = job.value(QStringLiteral("error")).toObject().value(QStringLiteral("message")).toString();
            me->setProgress(ok ? 1.0 : 0.0);
            me->setStatus(message);
            emit me->finished(ok, message);
            // Asked for while this ran.
            if (!me->m_queued.isEmpty() && !cancelled) {
                const QStringList next = me->m_queued;
                me->m_queued.clear();
                QMetaObject::invokeMethod(me, [self, next]() {
                    if (self)
                        self->tagAssets(next, true);
                }, Qt::QueuedConnection);
            } else {
                me->m_queued.clear();
            }
            return QJsonObject{{QStringLiteral("changed"), changed}};
        });
    emit runningChanged();
    return true;
}

void MediaTaggingController::cancel()
{
    m_queued.clear();
    if (!m_jobId.isEmpty()) {
        setStatus(tr("Stopping…"));
        m_app->m_jobs->cancel(m_jobId);
    }
}

bool MediaTaggingController::removeTag(const QString &assetId, const QString &tag)
{
    drift::MediaAsset *asset = m_app->m_project.asset(assetId);
    if (!asset)
        return false;
    const QStringList kept = drift::withoutTag(asset->tags, tag);
    if (kept == asset->tags)
        return false;
    const drift::Project before = m_app->m_project;
    m_app->m_project.asset(assetId)->tags = kept;
    m_app->pushProjectEdit(before, tr("Remove tag"));
    m_app->finishEdit(tr("Remove tag"));
    emit mediaChanged();
    return true;
}

bool MediaTaggingController::clearTags(const QStringList &assetIds)
{
    QStringList ids;
    for (const QString &id : assetIds) {
        const drift::MediaAsset *asset = m_app->m_project.asset(id);
        if (asset && !asset->tags.isEmpty())
            ids.append(id);
    }
    if (ids.isEmpty())
        return false;
    const drift::Project before = m_app->m_project;
    for (const QString &id : std::as_const(ids))
        m_app->m_project.asset(id)->tags.clear(); // stays `tagged`: tagging untagged media skips it
    m_app->pushProjectEdit(before, tr("Remove tags"));
    m_app->finishEdit(tr("Remove tags"));
    emit mediaChanged();
    return true;
}

void MediaTaggingController::setProgress(double progress)
{
    if (qFuzzyCompare(m_progress + 1.0, progress + 1.0))
        return;
    m_progress = progress;
    emit progressChanged();
}

void MediaTaggingController::setStatus(const QString &status)
{
    if (m_status == status)
        return;
    m_status = status;
    emit statusChanged();
}

#include "moc_MediaTaggingController.cpp"
