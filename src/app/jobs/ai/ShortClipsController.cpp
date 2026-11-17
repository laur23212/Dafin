#include "app/jobs/ai/ShortClipsController.h"

#include "app/editor/AppController.h"
#include "app/editor/ProjectFileController.h"
#include "app/editor/ProjectSafety.h"
#include "app/editor/appcontroller/AppControllerInternal.h"
#include "app/jobs/JobRegistry.h"
#include "app/jobs/vision/SceneDetectController.h"
#include "app/jobs/vision/FaceTrackingController.h"
#include "app/mcp/McpController.h"
#include "app/online/addons/AddonManager.h"
#include "app/online/library/SessionFiles.h"
#include "core/base/LowSpec.h"
#include "core/base/SafeMode.h"
#include "core/edit/AutoReframe.h"
#include "core/edit/ProtectedRanges.h"
#include "core/edit/TimelineOps.h"
#include "core/project/Project.h"
#include "core/project/ProtectedRange.h"
#include "core/project/SubtitleCue.h"
#include "core/project/Transcript.h"
#include "core/text/TextStyle.h"
#include "engine/decode/readers/ClipReaderPool.h"
#include "engine/face/tracking/FaceLandmarker.h"
#include "engine/ml/speech/SpeechAudio.h"
#include "engine/ml/speech/transcription/LocalTranscription.h"
#include "engine/ml/speech/transcription/WhisperTranscriber.h"
#include "engine/ml/vision/detection/SceneDetect.h"
#include "engine/project/ProjectBundle.h"
#include "engine/project/ProjectDependencies.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonObject>
#include <QMetaObject>
#include <QMutex>
#include <QPointer>
#include <QSet>
#include <QSettings>
#include <QTimer>
#include <QUuid>

#include <algorithm>
#include <cmath>
#include <memory>
#include <optional>

namespace {

using drift::shorts::Segment;
using namespace drift::appcontroller_detail;

// Their own decode cursors, so these scans never move playback's (see ClipReaderPool).
constexpr quint64 kShortsFaceStreamId = 0xDA'F1'5A'00'00'00'00'01ull;

constexpr double kLevelHop = 0.1;
constexpr double kFaceStep = 0.25;    // face samples per second of a short: 4
constexpr int kFaceFrameEdge = 640;
constexpr int kDefaultChecked = 3;
const QString kLengthKey = QStringLiteral("AI/shorts/length");
const QString kCaptionsKey = QStringLiteral("AI/shorts/captions");
const QString kKeywordsKey = QStringLiteral("AI/shorts/keywords");

QString lengthLabel(double seconds)
{
    const int s = int(std::lround(seconds));
    return QStringLiteral("%1:%2").arg(s / 60).arg(s % 60, 2, 10, QLatin1Char('0'));
}

int evenAtLeast2(double v)
{
    return std::max(2, int(std::lround(v / 2.0)) * 2);
}

// One edition to write.
struct EditionPlan
{
    Segment segment;
    QString name;
    QString path;
};

// What every edition shares, captured on the GUI thread.
struct EditionCommon
{
    drift::MediaAsset asset;
    drift::TranscriptPtr transcript;
    std::optional<drift::TextStyle> captionStyle;
    bool captions = true;
    bool followFaces = false;
    QString sourceProject;
    int rotation = 0;
    int fps = 30;
    int sampleRate = 48000;
    int width = 1080;
    int height = 1920;
    double sourceAspect = 16.0 / 9.0;
    int sourcePixelWidth = 0;
    int mediaW = 0;
    int mediaH = 0;
};

// Where the faces are across [startUs, endUs), sampled every kFaceStep seconds; gaps hold the
// nearest face, and no face at all is a centre crop. Returns false when cancelled.
bool scanFaces(const EditionCommon &c, drift::TimeUs startUs, drift::TimeUs endUs,
               const std::function<bool(double)> &progress, QList<drift::reframe::FaceSample> *out)
{
    const double length = drift::usToSeconds(endUs - startUs);
    if (!c.followFaces) {
        *out = {drift::reframe::centreSample(0.0), drift::reframe::centreSample(length)};
        return true;
    }
    drift::FaceLandmarker &landmarker = drift::FaceLandmarker::instance();
    if (!landmarker.available()) {
        *out = {drift::reframe::centreSample(0.0), drift::reframe::centreSample(length)};
        return true;
    }
    const int count = std::max(2, int(std::floor(length / kFaceStep)) + 1);
    QList<drift::reframe::FaceSample> found;
    found.reserve(count);
    QList<drift::FaceAnchors> previous;
    for (int k = 0; k < count; ++k) {
        const drift::TimeUs at = std::min(endUs - 1, startUs + drift::secondsToUs(k * kFaceStep));
        const QImage frame = ClipReaderPool::instance().readVideoFrame(
            c.asset.path, kShortsFaceStreamId, at, kFaceFrameEdge, kFaceFrameEdge, c.rotation);
        drift::reframe::FaceSample s;
        s.t = -1.0; // none seen here
        if (!frame.isNull()) {
            const QList<drift::FaceAnchors> faces = landmarker.detect(frame, previous.isEmpty() ? nullptr : &previous);
            previous = faces;
            // The most confident face is the one the short follows.
            const drift::FaceAnchors *best = nullptr;
            for (const drift::FaceAnchors &f : faces) {
                if (f.valid && (!best || f.score > best->score))
                    best = &f;
            }
            if (best) {
                s.t = k * kFaceStep;
                s.cx = best->faceCenter.x();
                s.cy = best->faceCenter.y();
                s.rx = std::max(0.05, best->faceRx);
                s.ry = std::max(0.05, best->faceRy);
            }
        }
        found.append(s);
        if (!progress(double(k + 1) / count))
            return false;
    }
    // ±0.75 s of smoothing: the crop glides after a face instead of twitching with it.
    const QList<drift::reframe::FaceSample> smooth =
        drift::reframe::smoothSamples(drift::reframe::fillFaceGaps(found, kFaceStep), 3);
    // At most 120 keys per short (as the agent's auto-reframe caps it): every n-th, and the last.
    const int stride = std::max(1, int((smooth.size() + 119) / 120));
    out->clear();
    for (int i = 0; i < smooth.size(); i += stride)
        out->append(smooth.at(i));
    if (!smooth.isEmpty() && (smooth.size() - 1) % stride != 0)
        out->append(smooth.last());
    return true;
}

// The edition as a project of its own: one 9:16 video clip of the source range, reframed by
// `keys`, optional captions, the transcript, and the edition link back to the source project.
drift::Project buildEdition(const EditionCommon &c, const EditionPlan &plan,
                            const drift::reframe::Result &keys, const QString &contentHash, qint64 contentSize)
{
    drift::Project p;
    p.setName(plan.name);
    p.setFps(c.fps);
    p.setSampleRate(c.sampleRate);
    p.setResolution(c.width, c.height);

    drift::MediaAsset asset = c.asset;
    asset.folderId.clear();
    asset.trimInUs = 0;
    asset.trimOutUs = -1;
    p.addAsset(asset);
    if (c.transcript)
        p.setTranscript(asset.id, c.transcript);

    const drift::TimeUs srcIn = drift::secondsToUs(plan.segment.start);
    const drift::TimeUs srcOut = drift::secondsToUs(plan.segment.end);
    const drift::TimeUs duration = srcOut - srcIn;

    const int videoTrack = drift::ensureTrackForClipType(p, drift::ClipType::Video);
    drift::Clip clip;
    clip.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    clip.type = drift::ClipType::Video;
    clip.assetId = asset.id;
    clip.name = asset.name;
    clip.path = asset.path;
    clip.thumbnailPath = asset.thumbnailPath;
    clip.filmstripPath = asset.filmstripPath;
    clip.srcIn = srcIn;
    clip.srcOut = srcOut;
    clip.timelineStart = 0;
    clip.timelineDuration = duration;
    clip.rotationCorrection = c.rotation;
    fitClipLayoutToCanvas(clip, c.mediaW, c.mediaH, c.width, c.height);
    for (const drift::reframe::Key &k : keys.keys) {
        const drift::TimeUs rel = std::clamp<drift::TimeUs>(drift::secondsToUs(k.t), 0, duration);
        writeClipPropValue(clip, QStringLiteral("x"), rel, k.x, true, true);
        writeClipPropValue(clip, QStringLiteral("y"), rel, k.y, true, true);
        writeClipPropValue(clip, QStringLiteral("width"), rel, k.width, true, true);
        writeClipPropValue(clip, QStringLiteral("height"), rel, k.height, true, true);
    }
    p.tracks()[videoTrack].clips.append(clip);

    if (c.captions && c.transcript) {
        // Short lines for a narrow picture: a few words at a time, at most two lines.
        QList<drift::SubtitleCue> cues;
        for (drift::SubtitleCue cue : drift::cuesFromTranscript(*c.transcript, srcIn, srcOut, 24, 2, 4)) {
            cue.startUs = std::clamp<drift::TimeUs>(cue.startUs - srcIn, 0, duration);
            cue.endUs = std::clamp<drift::TimeUs>(cue.endUs - srcIn, 0, duration);
            cue.source = asset.id;
            if (cue.endUs > cue.startUs)
                cues.append(cue);
        }
        if (!cues.isEmpty()) {
            const int subtitleTrack = drift::ensureTrackForClipType(p, drift::ClipType::Subtitle, true);
            drift::Clip captions;
            captions.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
            captions.type = drift::ClipType::Subtitle;
            captions.timelineStart = 0;
            captions.timelineDuration = duration;
            captions.srcIn = 0;
            captions.srcOut = duration;
            captions.subtitleCues = cues;
            captions.name = drift::subtitleClipName(cues);
            if (c.captionStyle)
                captions.textStyle = *c.captionStyle;
            applyDefaultVisualLayout(captions, c.width, c.height);
            p.tracks()[subtitleTrack].clips.append(captions);
        }
    }

    if (!c.sourceProject.isEmpty() && !contentHash.isEmpty()) {
        drift::VariantLink link;
        link.otherProject = c.sourceProject;
        link.assetId = asset.id;
        link.contentHash = contentHash;
        link.contentSize = contentSize;
        link.srcStartUs = srcIn;
        link.srcEndUs = srcOut;
        p.setVariantLink(link);
    }
    p.ensureTrackIds();
    return p;
}

bool writeEdition(const drift::Project &p, const QString &path, QString *error)
{
    drift::bundle::WriteRequest request;
    request.document = p.toJson();
    dafin::project::stampVersion(request.document);
    request.projectId = p.id();
    request.title = p.name();
    request.author = p.author();
    request.description = p.description();
    request.createdAt = p.createdAt();
    request.modifiedAt = p.modifiedAt();
    request.addons = drift::bundle::collectAddons(p);
    request.media = drift::bundle::collectMedia(p, /*embedSource=*/false);
    // Online downloads live in a folder deleted when Dafin closes: those travel inside the file,
    // as a normal Save does it.
    for (drift::bundle::MediaEntry &entry : request.media) {
        if (dafin::assets::session::isSessionFile(entry.originalPath))
            entry.embedded = true;
    }
    return drift::bundle::write(path, request, {}, error);
}

} // namespace

ShortClipsController::ShortClipsController(AppController *app, QObject *parent)
    : QObject(parent)
    , m_app(app)
{
    QSettings settings;
    m_targetSeconds = std::clamp(settings.value(kLengthKey, 30).toInt(), 15, 60);
    m_captions = settings.value(kCaptionsKey, true).toBool();
    m_keywords = settings.value(kKeywordsKey).toString();

    m_previewTimer = new QTimer(this);
    m_previewTimer->setInterval(50);
    connect(m_previewTimer, &QTimer::timeout, this, &ShortClipsController::previewTick);

    connect(m_app, &AppController::selectionChanged, this, &ShortClipsController::refreshSource);
    connect(m_app, &AppController::undoStackChanged, this, &ShortClipsController::refreshSource);
    connect(m_app, &AppController::projectReset, this, [this]() {
        cancel();
        stopPreview();
        m_result = Result{};
        m_created.clear();
        emit segmentsChanged();
        emit createdChanged();
        refreshSource();
    });
    refreshSource();
}

// ---- source -------------------------------------------------------------------------------

void ShortClipsController::refreshSource()
{
    QString clipId, name, hint;
    const int t = m_app->m_selectedTrack;
    const int c = m_app->m_selectedClip;
    if (m_app->isValidClipIndex(t, c)) {
        const drift::Clip &clip = m_app->m_project.tracks().at(t).clips.at(c);
        const drift::MediaAsset *asset = clip.assetId.isEmpty() ? nullptr : m_app->m_project.asset(clip.assetId);
        if (clip.type != drift::ClipType::Video)
            hint = tr("Select a video clip: a short needs a picture.");
        else if (!asset || !asset->sequenceId.isEmpty() || asset->path.isEmpty())
            hint = tr("This clip can't be used: select one of the clips inside it.");
        else if (clip.srcOut - clip.srcIn < drift::secondsToUs(drift::shorts::kMinShortSeconds))
            hint = tr("This clip is shorter than 15 seconds.");
        else {
            clipId = clip.id;
            name = asset->name.isEmpty() ? QFileInfo(clip.path).fileName() : asset->name;
        }
    } else {
        hint = tr("Select a video clip on the timeline first.");
    }
    if (clipId == m_clipId && name == m_sourceName && hint == m_sourceHint)
        return;
    m_clipId = clipId;
    m_sourceName = name;
    m_sourceHint = hint;
    emit sourceChanged();
}

bool ShortClipsController::locateResultClip(int *trackIndex, int *clipIndex) const
{
    if (m_result.clipId.isEmpty())
        return false;
    const QVariantList at = m_app->locateClip(m_result.clipId);
    if (at.size() != 2)
        return false;
    *trackIndex = at.at(0).toInt();
    *clipIndex = at.at(1).toInt();
    return m_app->isValidClipIndex(*trackIndex, *clipIndex);
}

bool ShortClipsController::guardAiAllowed()
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
    if (busy()) {
        m_app->setLastMessage(tr("Smart short clips is already working"), QStringLiteral("warning"));
        return false;
    }
    return true;
}

// ---- settings and the list ------------------------------------------------------------------

void ShortClipsController::setTargetSeconds(int seconds)
{
    seconds = std::clamp(seconds, 15, 60);
    if (seconds == m_targetSeconds)
        return;
    m_targetSeconds = seconds;
    QSettings().setValue(kLengthKey, seconds);
    emit settingsChanged();
}

void ShortClipsController::setCaptions(bool on)
{
    if (on == m_captions)
        return;
    m_captions = on;
    QSettings().setValue(kCaptionsKey, on);
    emit settingsChanged();
}

void ShortClipsController::setKeywords(const QString &keywords)
{
    if (keywords == m_keywords)
        return;
    m_keywords = keywords;
    QSettings().setValue(kKeywordsKey, keywords);
    emit settingsChanged();
}

QVariantList ShortClipsController::segments() const
{
    QVariantList out;
    for (int i = 0; i < m_result.segments.size(); ++i) {
        const Segment &s = m_result.segments.at(i);
        QStringList why;
        if (s.hook >= 0.5)
            why << tr("strong opening");
        if (s.energy >= 0.7)
            why << tr("lively speech");
        if (s.keywordHits > 0)
            why << tr("%n keyword(s)", "", s.keywordHits);
        if (s.sceneCuts > 0)
            why << tr("%n shot change(s)", "", s.sceneCuts);
        out.append(QVariantMap{
            {QStringLiteral("index"), i},
            {QStringLiteral("score"), int(std::lround(s.score))},
            {QStringLiteral("start"), s.start},
            {QStringLiteral("end"), s.end},
            {QStringLiteral("length"), s.length()},
            {QStringLiteral("lengthLabel"), lengthLabel(s.length())},
            {QStringLiteral("startLabel"), lengthLabel(s.start)},
            {QStringLiteral("firstWords"), s.text.isEmpty() ? tr("Shots %1–%2").arg(lengthLabel(s.start), lengthLabel(s.end))
                                                            : drift::shorts::firstWords(s.text, 10)},
            {QStringLiteral("text"), s.text},
            {QStringLiteral("checked"), i < m_result.checked.size() && m_result.checked.at(i)},
            {QStringLiteral("why"), why.join(QStringLiteral(", "))},
        });
    }
    return out;
}

int ShortClipsController::checkedCount() const
{
    return int(std::count(m_result.checked.cbegin(), m_result.checked.cend(), true));
}

void ShortClipsController::setChecked(int index, bool checked)
{
    if (index < 0 || index >= m_result.checked.size() || m_result.checked.at(index) == checked)
        return;
    m_result.checked[index] = checked;
    emit segmentsChanged();
}

void ShortClipsController::setAllChecked(bool checked)
{
    bool changed = false;
    for (bool &c : m_result.checked) {
        changed = changed || c != checked;
        c = checked;
    }
    if (changed)
        emit segmentsChanged();
}

// ---- preview ----------------------------------------------------------------------------------

void ShortClipsController::preview(int index)
{
    if (index == m_previewing) {
        stopPreview();
        return;
    }
    if (index < 0 || index >= m_result.segments.size())
        return;
    int t = -1, c = -1;
    if (!locateResultClip(&t, &c)) {
        m_app->setLastMessage(tr("The clip these highlights came from is no longer on the timeline"),
                              QStringLiteral("warning"));
        return;
    }
    const drift::Clip &clip = m_app->m_project.tracks().at(t).clips.at(c);
    const Segment &s = m_result.segments.at(index);
    // Source seconds to timeline seconds through the clip's speed and direction.
    const double speed = clip.effectiveSpeed();
    const double in = drift::usToSeconds(clip.srcIn);
    const double out = drift::usToSeconds(clip.srcOut);
    const double start = drift::usToSeconds(clip.timelineStart);
    double a = clip.reverse ? start + (out - s.end) / speed : start + (s.start - in) / speed;
    double b = clip.reverse ? start + (out - s.start) / speed : start + (s.end - in) / speed;
    a = std::clamp(a, start, drift::usToSeconds(clip.timelineEnd()));
    b = std::clamp(b, start, drift::usToSeconds(clip.timelineEnd()));
    m_previewStartSeconds = a;
    m_previewEndSeconds = b;
    m_app->setPlaying(false);
    m_app->setPlayheadSeconds(a);
    m_app->setPlaying(true);
    m_previewing = index;
    emit previewingChanged();
    m_previewTimer->start();
}

void ShortClipsController::stopPreview()
{
    if (m_previewing < 0)
        return;
    m_previewTimer->stop();
    m_previewing = -1;
    emit previewingChanged();
    if (m_app->playing())
        m_app->setPlaying(false);
}

void ShortClipsController::previewTick()
{
    if (m_previewing < 0) {
        m_previewTimer->stop();
        return;
    }
    const double at = m_app->playheadSeconds();
    // Stopped by the user, or moved elsewhere: the preview is over, without touching playback.
    if (!m_app->playing() || at < m_previewStartSeconds - 0.5 || at > m_previewEndSeconds + 1.0) {
        m_previewTimer->stop();
        m_previewing = -1;
        emit previewingChanged();
        return;
    }
    if (at >= m_previewEndSeconds)
        stopPreview();
}

// ---- analyze ----------------------------------------------------------------------------------

bool ShortClipsController::analyze(const QString &language)
{
    if (!guardAiAllowed())
        return false;
    refreshSource();
    if (m_clipId.isEmpty()) {
        m_app->setLastMessage(m_sourceHint, QStringLiteral("warning"));
        return false;
    }
    const QVariantList at = m_app->locateClip(m_clipId);
    if (at.size() != 2)
        return false;
    const drift::Clip clip = m_app->m_project.tracks().at(at.at(0).toInt()).clips.at(at.at(1).toInt());
    const drift::MediaAsset *asset = m_app->m_project.asset(clip.assetId);
    if (!asset)
        return false;

    const drift::TranscriptPtr stored = m_app->m_project.transcript(clip.assetId);
    const bool fresh = stored && stored->source.matches(asset->path);
    const bool mayHaveSound = !asset->hasAudioKnown || asset->hasAudio;
    const bool runtime = m_app->m_addonManager && m_app->m_addonManager->runtimeAvailable();
    const bool transcribe = !fresh && mayHaveSound && runtime && drift::WhisperTranscriber::modelPresent();
    if (transcribe && m_app->m_jobs->hasActive(QStringLiteral("transcribe"), clip.assetId)) {
        m_app->setLastMessage(tr("“%1” is already being transcribed; try again when it is done").arg(m_sourceName),
                              QStringLiteral("warning"));
        return false;
    }

    // The shot pass reads frames through SceneDetect's own decode cursor, which the Scenes tab uses.
    if (m_app->m_sceneDetect && m_app->m_sceneDetect->detecting()) {
        m_app->setLastMessage(tr("Wait for scene detection to finish first"), QStringLiteral("warning"));
        return false;
    }

    stopPreview();
    // Frames and sound come off the shared decoder pool, which playback must not walk meanwhile.
    m_app->setPlaying(false);

    const QString path = asset->path;
    const QString assetId = clip.assetId;
    const drift::TimeUs srcIn = clip.srcIn;
    const drift::TimeUs srcOut = clip.srcOut;
    const int rotation = clip.rotationCorrection;
    const QString lang = language.trimmed().toLower();
    QStringList keywords = drift::shorts::defaultKeywords();
    for (const QString &k : drift::shorts::parseKeywords(m_keywords)) {
        if (!keywords.contains(k))
            keywords.append(k);
    }
    const double target = m_targetSeconds;
    const drift::TranscriptPtr existing = fresh ? stored : nullptr;

    struct Shared
    {
        QMutex mutex;
        std::shared_ptr<drift::Transcript> made;
        QList<Segment> segments;
        bool fromSpeech = false;
        QString note;
    };
    auto shared = std::make_shared<Shared>();
    const QPointer<ShortClipsController> self(this);
    auto report = [self](double fraction, const QString &status) {
        if (!self)
            return;
        QMetaObject::invokeMethod(
            self.data(),
            [self, fraction, status]() {
                if (!self || !self->analyzing())
                    return;
                self->setProgress(std::min(0.99, fraction));
                if (!status.isEmpty())
                    self->setStatus(status);
            },
            Qt::QueuedConnection);
    };

    setProgress(0.0);
    setStatus(transcribe ? tr("Getting speech recognition ready…") : tr("Measuring the sound…"));
    const QString id = m_app->m_jobs->start(
        QStringLiteral("smart_shorts"), assetId, JobRegistry::Lane::Model,
        [=](JobContext &ctx) {
            drift::TranscriptPtr transcript = existing;
            std::vector<float> pcm16;
            // 1. The words, when the media has none yet (whole file, as Auto captions keeps it).
            const double speechShare = transcribe ? 0.6 : 0.0;
            if (transcribe) {
                bool cancelled = false;
                pcm16 = drift::readMono16k(
                    path, 0, -1,
                    [&](double f) {
                        report(0.05 * f, ShortClipsController::tr("Reading the audio…"));
                        return !ctx.cancelled();
                    },
                    &cancelled);
                if (cancelled || ctx.cancelled())
                    return;
                if (!pcm16.empty()) {
                    drift::LocalTranscribeOptions options;
                    options.language = lang;
                    options.align = true;
                    options.vad = true;
                    const drift::LocalTranscribeResult result =
                        drift::transcribeLocal(pcm16, 0, options, [&](double f, const QString &status) {
                            ctx.progress(0.05 + 0.55 * f, status);
                            report(0.05 + 0.55 * f, status);
                            return !ctx.cancelled();
                        });
                    if (result.cancelled || ctx.cancelled())
                        return;
                    if (result.transcript) {
                        result.transcript->source = drift::SourceFingerprint::of(path);
                        transcript = result.transcript;
                        QMutexLocker lock(&shared->mutex);
                        shared->made = result.transcript;
                    } else {
                        QMutexLocker lock(&shared->mutex);
                        shared->note = result.error;
                    }
                }
            }

            // 2. Loudness, every 0.1 s of the clip's range.
            report(speechShare, ShortClipsController::tr("Measuring the sound…"));
            QVector<float> levels;
            if (!pcm16.empty()) {
                const size_t from = std::min(pcm16.size(), drift::speechUsToSamples(srcIn));
                const size_t to = std::min(pcm16.size(), drift::speechUsToSamples(srcOut));
                if (to > from)
                    levels = drift::shorts::levelsDbFromMono(pcm16.data() + from, qint64(to - from),
                                                             drift::kSpeechSampleRate, kLevelHop);
                std::vector<float>().swap(pcm16);
            } else if (mayHaveSound) {
                // In two-minute pieces at 8 kHz, so an hour of sound never sits in memory at once.
                constexpr int kRate = 8000;
                const drift::TimeUs piece = drift::secondsToUs(120.0);
                for (drift::TimeUs at = srcIn; at < srcOut; at += piece) {
                    bool cancelled = false;
                    const std::vector<float> mono =
                        drift::readMono(path, at, std::min(srcOut, at + piece), kRate, {}, &cancelled);
                    if (cancelled || ctx.cancelled())
                        return;
                    if (mono.empty())
                        break;
                    levels += drift::shorts::levelsDbFromMono(mono.data(), qint64(mono.size()), kRate, kLevelHop);
                    const double f = double(at - srcIn) / double(std::max<drift::TimeUs>(1, srcOut - srcIn));
                    report(speechShare + 0.1 * f, {});
                }
            }
            if (ctx.cancelled())
                return;

            // 3. The shots (cached by SceneDetect, so a second analysis is quick).
            drift::SceneDetectRequest request;
            request.path = path;
            request.rotationCorrection = rotation;
            request.sourceIn = srcIn;
            request.sourceOut = srcOut;
            request.options.analyzeAudio = false;
            request.options.detectObjects = false;
            drift::SceneAnalysis scenes;
            if (!drift::loadCachedAnalysis(request, &scenes)) {
                const double base = speechShare + 0.1;
                QString error;
                scenes = drift::detectScenes(
                    request,
                    [&](double f, const QString &) {
                        ctx.progress(base + (0.97 - base) * f);
                        report(base + (0.97 - base) * f, ShortClipsController::tr("Finding the shots…"));
                        return !ctx.cancelled();
                    },
                    &error);
                if (ctx.cancelled())
                    return;
                if (!scenes.scenes.isEmpty())
                    drift::storeCachedAnalysis(request, scenes);
            }
            QList<double> cuts;
            for (const drift::SceneCut &cut : std::as_const(scenes.cuts))
                cuts.append(drift::usToSeconds(cut.sourceUs));

            // 4. The plan: whole sentences when enough is said, whole shots otherwise.
            const double in = drift::usToSeconds(srcIn);
            const double out = drift::usToSeconds(srcOut);
            QList<drift::shorts::Unit> units;
            if (transcript)
                units = drift::shorts::sentencesFromTranscript(*transcript, srcIn, srcOut);
            int words = 0;
            for (const drift::shorts::Unit &u : std::as_const(units))
                words += u.words;
            const bool fromSpeech = words >= 20;
            drift::shorts::Options options;
            options.targetSeconds = target;
            options.rangeStart = in;
            options.rangeEnd = out;
            if (!fromSpeech) {
                units = drift::shorts::shotsFromCuts(cuts, in, out);
                options.leadIn = 0.0;
                options.tail = 0.0;
            }
            drift::shorts::Signals input;
            input.levelsDb = levels;
            input.hop = kLevelHop;
            input.levelsStart = in;
            input.sceneCuts = cuts;
            input.keywords = keywords;
            const QList<Segment> segments = drift::shorts::planShorts(units, input, options);
            {
                QMutexLocker lock(&shared->mutex);
                shared->segments = segments;
                shared->fromSpeech = fromSpeech;
            }
            ctx.succeed({{QStringLiteral("segments"), int(segments.size())}, {QStringLiteral("speech"), fromSpeech}});
        },
        [self, shared, assetId, path, clipId = clip.id, name = m_sourceName](const QJsonObject &job) {
            if (!self)
                return QJsonObject{};
            ShortClipsController *me = self.data();
            AppController *app = me->m_app;
            QMutexLocker lock(&shared->mutex);
            // A transcript made here is kept with the project, as Auto captions keeps its own.
            if (shared->made) {
                const drift::MediaAsset *asset = app->m_project.asset(assetId);
                if (asset && asset->path == path && shared->made->source.matches(asset->path)) {
                    app->m_project.setTranscript(assetId, shared->made);
                    if (app->m_mcpController)
                        app->m_mcpController->bumpEditRevision();
                    app->projectFile()->setDirty(true);
                }
            }
            const bool ok = job.value(QStringLiteral("ok")).toBool();
            const QString code = job.value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toString();
            QString message;
            if (ok) {
                me->stopPreview();
                me->m_result = Result{};
                me->m_result.clipId = clipId;
                me->m_result.assetId = assetId;
                me->m_result.path = path;
                me->m_result.name = name;
                me->m_result.fromSpeech = shared->fromSpeech;
                me->m_result.segments = shared->segments;
                me->m_result.checked = QVector<bool>(int(shared->segments.size()), false);
                for (int i = 0; i < std::min(kDefaultChecked, int(shared->segments.size())); ++i)
                    me->m_result.checked[i] = true;
                emit me->segmentsChanged();
                if (shared->segments.isEmpty())
                    message = tr("No highlights of this length were found. Try another length.");
                else if (!shared->fromSpeech)
                    message = shared->note.isEmpty()
                                  ? tr("Found %n highlight(s) by shots: there was too little speech for sentences.", "",
                                       int(shared->segments.size()))
                                  : tr("Found %n highlight(s) by shots (speech recognition failed: %1)", "",
                                       int(shared->segments.size())).arg(shared->note);
                else
                    message = tr("Found %n highlight(s)", "", int(shared->segments.size()));
            } else if (code == QLatin1String("cancelled")) {
                message = tr("Cancelled");
            } else {
                message = job.value(QStringLiteral("error")).toObject().value(QStringLiteral("message")).toString();
            }
            lock.unlock();
            me->setJob({}, false);
            me->setProgress(ok ? 1.0 : 0.0);
            me->setStatus(message);
            if (!ok && code != QLatin1String("cancelled"))
                app->setLastMessage(message, QStringLiteral("error"));
            emit me->finished(ok, message);
            return QJsonObject{};
        });
    setJob(id, false);
    return true;
}

void ShortClipsController::cancel()
{
    if (m_jobId.isEmpty())
        return;
    setStatus(tr("Stopping…"));
    m_app->m_jobs->cancel(m_jobId);
}

// ---- create -----------------------------------------------------------------------------------

bool ShortClipsController::create()
{
    if (!guardAiAllowed())
        return false;
    if (checkedCount() == 0) {
        m_app->setLastMessage(tr("Tick the highlights to turn into shorts first"), QStringLiteral("warning"));
        return false;
    }
    const QString projectPath = m_app->projectFile()->currentProjectPath();
    const QFileInfo projectInfo(projectPath);
    if (projectPath.isEmpty() || projectPath.startsWith(QLatin1String("content:")) || !projectInfo.dir().exists()) {
        m_app->setLastMessage(tr("Save the project first: each short is saved next to it as a project of its own, "
                                 "linked to this one."),
                              QStringLiteral("warning"));
        return false;
    }
    const drift::MediaAsset *asset = m_app->m_project.asset(m_result.assetId);
    if (!asset || asset->path != m_result.path) {
        m_app->setLastMessage(tr("The media these highlights came from has changed; analyze it again"),
                              QStringLiteral("warning"));
        return false;
    }
    const bool runtime = m_app->m_addonManager && m_app->m_addonManager->runtimeAvailable();
    bool followFaces = runtime && drift::FaceLandmarker::modelPresent();
    if (followFaces && m_app->m_faces && m_app->m_faces->detecting()) {
        m_app->setLastMessage(tr("Wait for face detection to finish first"), QStringLiteral("warning"));
        return false;
    }

    EditionCommon common;
    common.asset = *asset;
    common.transcript = m_app->m_project.transcript(asset->id);
    if (common.transcript && !common.transcript->source.matches(asset->path))
        common.transcript.reset();
    common.captionStyle = drift::textStyleForPresetId(QStringLiteral("subtitle"));
    common.captions = m_captions;
    common.followFaces = followFaces;
    common.sourceProject = projectInfo.absoluteFilePath();
    int t = -1, c = -1;
    common.rotation = locateResultClip(&t, &c) ? m_app->m_project.tracks().at(t).clips.at(c).rotationCorrection
                                               : drift::rotationCorrectionOf(*asset);
    common.fps = m_app->m_project.fps();
    common.sampleRate = m_app->m_project.sampleRate() > 0 ? m_app->m_project.sampleRate() : 48000;
    // 9:16 at the project's short side: a 1920 × 1080 project gives 1080 × 1920 shorts.
    const int shortSide = std::max(2, std::min(m_app->m_project.width(), m_app->m_project.height()));
    common.width = evenAtLeast2(shortSide);
    common.height = evenAtLeast2(common.width * 16.0 / 9.0);
    // The probe keeps the file's own size; the picture is turned by its rotation tag plus the clip's
    // correction (applyAssetLayout swaps the sides the same way).
    const bool turned = ((asset->rotationDegrees + common.rotation) % 360 + 360) % 180 == 90;
    common.mediaW = turned ? asset->height : asset->width;
    common.mediaH = turned ? asset->width : asset->height;
    common.sourceAspect = common.mediaW > 0 && common.mediaH > 0 ? double(common.mediaW) / common.mediaH : 16.0 / 9.0;
    common.sourcePixelWidth = common.mediaW;

    // "<project> - Short N.drift", never over a file that is already there.
    const QString base = projectInfo.completeBaseName();
    const QString title = m_app->m_project.name().isEmpty() ? base : m_app->m_project.name();
    QList<EditionPlan> plans;
    QSet<QString> taken;
    int n = 1;
    for (int i = 0; i < m_result.segments.size(); ++i) {
        if (!m_result.checked.value(i))
            continue;
        QString path;
        for (;; ++n) {
            path = projectInfo.dir().filePath(QStringLiteral("%1 - Short %2.drift").arg(base).arg(n));
            if (!QFileInfo::exists(path) && !taken.contains(path))
                break;
        }
        taken.insert(path);
        plans.append({m_result.segments.at(i), tr("%1 – Short %2").arg(title).arg(n), path});
        ++n;
    }

    stopPreview();
    if (followFaces)
        m_app->setPlaying(false);

    struct Shared
    {
        QMutex mutex;
        QVariantList written;
    };
    auto shared = std::make_shared<Shared>();
    const QPointer<ShortClipsController> self(this);
    const int total = int(plans.size());
    auto report = [self](double fraction, const QString &status) {
        if (!self)
            return;
        QMetaObject::invokeMethod(
            self.data(),
            [self, fraction, status]() {
                if (!self || !self->creating())
                    return;
                self->setProgress(std::min(0.99, fraction));
                if (!status.isEmpty())
                    self->setStatus(status);
            },
            Qt::QueuedConnection);
    };

    setProgress(0.0);
    setStatus(tr("Creating %n short(s)…", "", total));
    const QString id = m_app->m_jobs->start(
        QStringLiteral("smart_shorts_create"), m_result.assetId, JobRegistry::Lane::Model,
        [common, plans, shared, report, total](JobContext &ctx) {
            qint64 size = 0;
            const QString hash = drift::sampledContentHash(common.asset.path, &size);
            for (int i = 0; i < plans.size(); ++i) {
                if (ctx.cancelled())
                    return;
                const EditionPlan &plan = plans.at(i);
                const QString status = common.followFaces
                                           ? ShortClipsController::tr("Following the faces in short %1 of %2…").arg(i + 1).arg(total)
                                           : ShortClipsController::tr("Writing short %1 of %2…").arg(i + 1).arg(total);
                report(double(i) / total, status);
                QList<drift::reframe::FaceSample> samples;
                const bool done = scanFaces(
                    common, drift::secondsToUs(plan.segment.start), drift::secondsToUs(plan.segment.end),
                    [&](double f) {
                        ctx.progress((i + 0.95 * f) / total);
                        report((i + 0.95 * f) / total, {});
                        return !ctx.cancelled();
                    },
                    &samples);
                if (!done || ctx.cancelled())
                    return;
                const drift::reframe::Result keys = drift::reframe::reframeKeys(
                    samples, common.width, common.height, common.sourceAspect, 9.0 / 16.0, common.sourcePixelWidth);
                const drift::Project edition = buildEdition(common, plan, keys, hash, size);
                QString error;
                if (!writeEdition(edition, plan.path, &error)) {
                    ctx.fail(QStringLiteral("write_failed"),
                             ShortClipsController::tr("Could not write “%1”: %2")
                                 .arg(QFileInfo(plan.path).fileName(), error));
                    return;
                }
                QMutexLocker lock(&shared->mutex);
                shared->written.append(QVariantMap{{QStringLiteral("name"), plan.name},
                                                   {QStringLiteral("path"), plan.path},
                                                   {QStringLiteral("length"), lengthLabel(plan.segment.length())}});
            }
            ctx.succeed({{QStringLiteral("written"), total}});
        },
        [self, shared](const QJsonObject &job) {
            if (!self)
                return QJsonObject{};
            ShortClipsController *me = self.data();
            const bool ok = job.value(QStringLiteral("ok")).toBool();
            const QJsonObject error = job.value(QStringLiteral("error")).toObject();
            QVariantList written;
            {
                QMutexLocker lock(&shared->mutex);
                written = shared->written;
            }
            // Whatever was written stays, a cancelled or failed run included.
            me->m_created = written;
            emit me->createdChanged();
            QString message;
            if (ok)
                message = tr("Created %n short(s) next to the project", "", int(written.size()));
            else if (error.value(QStringLiteral("code")).toString() == QLatin1String("cancelled"))
                message = written.isEmpty() ? tr("Cancelled") : tr("Cancelled after %n short(s)", "", int(written.size()));
            else
                message = error.value(QStringLiteral("message")).toString();
            me->setJob({}, true);
            me->setProgress(ok ? 1.0 : 0.0);
            me->setStatus(message);
            me->m_app->setLastMessage(message, ok ? QStringLiteral("success") : QStringLiteral("warning"));
            emit me->finished(ok, message);
            return QJsonObject{{QStringLiteral("paths"), QJsonValue::fromVariant(written)}};
        });
    setJob(id, true);
    return true;
}

// ---- state ------------------------------------------------------------------------------------

void ShortClipsController::setJob(const QString &id, bool create)
{
    m_jobId = id;
    m_jobIsCreate = create;
    emit busyChanged();
}

void ShortClipsController::setProgress(double progress)
{
    if (qFuzzyCompare(m_progress + 1.0, progress + 1.0))
        return;
    m_progress = progress;
    emit progressChanged();
}

void ShortClipsController::setStatus(const QString &status)
{
    if (status == m_status)
        return;
    m_status = status;
    emit statusChanged();
}

#include "moc_ShortClipsController.cpp"
