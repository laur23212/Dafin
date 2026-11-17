#include "app/jobs/ai/AiToolsController.h"

#include "core/base/LowSpec.h"
#include "core/base/SafeMode.h"

#include "app/editor/AppController.h"
#include "app/editor/ProjectFileController.h"
#include "app/jobs/JobRegistry.h"
#include "app/jobs/audio/AudioToolsController.h"
#include "app/mcp/McpController.h"
#include "core/edit/CutOps.h"
#include "core/edit/ProtectedRanges.h"
#include "core/edit/TimelineOps.h"
#include "core/project/Project.h"
#include "core/project/TranscriptEdit.h"
#include "engine/ml/speech/SpeechAudio.h"
#include "engine/ml/speech/diarization/SpeakerDiarizer.h"
#include "engine/ml/speech/transcription/CtcAligner.h"
#include "engine/ml/speech/transcription/LocalTranscription.h"
#include "engine/ml/speech/transcription/WhisperTranscriber.h"

#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QMetaObject>
#include <QPointer>
#include <QSettings>
#include <QTimer>
#include <QUndoStack>

#include <algorithm>
#include <memory>
#include <utility>

namespace {

// m:ss, or h:mm:ss from an hour on.
QString timeLabel(drift::TimeUs us)
{
    const qint64 total = std::max<qint64>(0, us / drift::kUsPerSecond);
    const qint64 h = total / 3600;
    const qint64 m = (total / 60) % 60;
    const qint64 s = total % 60;
    if (h > 0)
        return QStringLiteral("%1:%2:%3").arg(h).arg(m, 2, 10, QLatin1Char('0')).arg(s, 2, 10, QLatin1Char('0'));
    return QStringLiteral("%1:%2").arg(m).arg(s, 2, 10, QLatin1Char('0'));
}

bool isMediaClip(const drift::Clip &clip)
{
    return (clip.type == drift::ClipType::Video || clip.type == drift::ClipType::Audio) && !clip.path.isEmpty();
}

// The text a line was listed with, read back from the transcript, to tell a stale line index.
QString speechText(const drift::Transcript &transcript, int first, int last)
{
    QStringList parts;
    for (int i = std::max(0, first); i <= last && i < transcript.words.size(); ++i) {
        const drift::TranscriptWord &w = transcript.words.at(i);
        const QString text = w.text.trimmed();
        if (drift::isSpeechToken(w) && !text.isEmpty())
            parts.append(text);
    }
    return parts.join(QLatin1Char(' '));
}

// Dafin: the take a transcript view shows for `focus`: every media clip on its track that plays the
// same media, in timeline order (a cut splits one clip into several; they still read as one take).
// A clip from another file, or a stale transcript, is not part of it.
QList<drift::Clip> takeOf(const drift::Project &project, const drift::Clip &focus)
{
    QList<drift::Clip> out;
    for (const drift::Track &track : project.tracks()) {
        bool here = false;
        for (const drift::Clip &clip : track.clips)
            here = here || clip.id == focus.id;
        if (!here)
            continue;
        for (const drift::Clip &clip : track.clips) {
            if (clip.id == focus.id
                || (isMediaClip(clip) && !focus.assetId.isEmpty() && clip.assetId == focus.assetId
                    && clip.path == focus.path))
                out.append(clip);
        }
        break;
    }
    if (out.isEmpty())
        out.append(focus);
    std::stable_sort(out.begin(), out.end(), [](const drift::Clip &a, const drift::Clip &b) {
        return a.timelineStart < b.timelineStart;
    });
    return out;
}

// m:ss–m:ss of a source range, for messages.
QString rangeLabel(const drift::TimeRangeUs &r)
{
    return QStringLiteral("%1–%2").arg(timeLabel(r.startUs), timeLabel(r.endUs));
}

constexpr drift::TimeUs kDeclickUs = 30'000;     // cut_words' de-click ramp
constexpr drift::TimeUs kWordPadUs = 50'000;     // cut_words' default air beside kept words
const char *const kFillerKey = "ai/fillerWords/";

QString fillerLangKey(const QString &language)
{
    const QString lang = language.trimmed().toLower().section(QLatin1Char('-'), 0, 0)
                             .section(QLatin1Char('_'), 0, 0);
    return lang.isEmpty() ? QStringLiteral("en") : lang;
}

QList<drift::FillerWord> storedFillers(const QString &language)
{
    const QString lang = fillerLangKey(language);
    const QVariant stored = QSettings().value(QLatin1String(kFillerKey) + lang);
    if (!stored.isValid())
        return drift::defaultFillerWords(lang);
    QList<drift::FillerWord> out;
    for (const QVariant &v : stored.toList()) {
        const QVariantMap m = v.toMap();
        const QString text = m.value(QStringLiteral("text")).toString().simplified();
        if (!text.isEmpty())
            out.append({text, m.value(QStringLiteral("enabled"), true).toBool()});
    }
    return out;
}

} // namespace

AiToolsController::AiToolsController(AppController *app, QObject *parent)
    : QObject(parent)
    , m_app(app)
{
    m_refreshTimer = new QTimer(this);
    m_refreshTimer->setSingleShot(true);
    m_refreshTimer->setInterval(0);
    connect(m_refreshTimer, &QTimer::timeout, this, &AiToolsController::refresh);
    m_hitStopTimer = new QTimer(this);
    m_hitStopTimer->setSingleShot(true);
    connect(m_hitStopTimer, &QTimer::timeout, this, [this] {
        if (m_app->playing())
            m_app->setPlaying(false);
    });

    // Selection, edits, undo and a new project all change which clip is in focus or what its
    // transcript lines map to on the timeline. Bursts collapse into one refresh.
    connect(m_app, &AppController::selectionChanged, this, &AiToolsController::scheduleRefresh);
    connect(m_app, &AppController::tracksChanged, this, &AiToolsController::scheduleRefresh);
    connect(&m_app->m_undoStack, &QUndoStack::indexChanged, this, &AiToolsController::scheduleRefresh);
    connect(m_app, &AppController::projectReset, this, [this] {
        m_lastCaptionedClipId.clear();
        cancelCleanup();
        clearCleanup();
        scheduleRefresh();
    });
}

void AiToolsController::setTranscriptVisible(bool visible)
{
    if (m_transcriptVisible == visible)
        return;
    m_transcriptVisible = visible;
    emit transcriptVisibleChanged();
    if (visible)
        refresh();
}

void AiToolsController::scheduleRefresh()
{
    if (!m_refreshTimer->isActive())
        m_refreshTimer->start();
}

void AiToolsController::refresh()
{
    m_refreshTimer->stop();
    const drift::Project &project = m_app->m_project;

    QList<QPair<int, int>> pairs = m_app->m_selection;
    if (pairs.isEmpty() && m_app->m_selectedTrack >= 0 && m_app->m_selectedClip >= 0)
        pairs.append(qMakePair(m_app->m_selectedTrack, m_app->m_selectedClip));
    bool canCaption = false;
    for (const QPair<int, int> &pair : std::as_const(pairs)) {
        if (m_app->isValidClipIndex(pair.first, pair.second)
            && isMediaClip(project.tracks().at(pair.first).clips.at(pair.second))) {
            canCaption = true;
            break;
        }
    }
    if (canCaption != m_canCaption) {
        m_canCaption = canCaption;
        emit canCaptionChanged();
    }

    // Lines are only worked out while the panel shows them.
    if (!m_transcriptVisible)
        return;

    // The clip in focus: the selected video/audio clip, else the one captioned last (selecting the
    // new caption clip keeps its transcript up).
    const drift::Clip *target = nullptr;
    if (m_app->isValidClipIndex(m_app->m_selectedTrack, m_app->m_selectedClip)) {
        const drift::Clip &clip = project.tracks().at(m_app->m_selectedTrack).clips.at(m_app->m_selectedClip);
        if (isMediaClip(clip))
            target = &clip;
    }
    if (!target && !m_lastCaptionedClipId.isEmpty()) {
        const QVariantList at = m_app->locateClip(m_lastCaptionedClipId);
        if (at.size() == 2)
            target = &project.tracks().at(at.at(0).toInt()).clips.at(at.at(1).toInt());
    }

    QVariantMap info;
    // A listed line with its words (timeline order) and the clip that plays them.
    struct Listed
    {
        Line line;
        QVariantMap row;
        QVariantList words;
    };
    QList<Listed> listed;
    QString assetId;
    QString sourceKey;
    if (target) {
        assetId = target->assetId;
        sourceKey = drift::subtitleSourceKey(*target);
        const drift::MediaAsset *asset = assetId.isEmpty() ? nullptr : project.asset(assetId);
        QString name = target->name;
        if (name.isEmpty() && asset)
            name = asset->name;
        if (name.isEmpty())
            name = QFileInfo(target->path).fileName();
        const drift::TranscriptPtr transcript = asset ? project.transcript(assetId) : nullptr;
        const bool usable = transcript && transcript->source.matches(target->path);
        info.insert(QStringLiteral("clipId"), target->id);
        info.insert(QStringLiteral("clipName"), name);
        info.insert(QStringLiteral("hasTranscript"), usable);
        if (usable) {
            info.insert(QStringLiteral("language"), transcript->language);
            info.insert(QStringLiteral("aligned"), transcript->wordTimingsAligned);
            info.insert(QStringLiteral("words"), transcript->speechTokenCount());
            // Dafin: the whole take on this track, so the words stay listed once a cut has split
            // the clip into pieces.
            const QList<drift::Clip> take = takeOf(project, *target);
            for (const drift::Clip &clip : std::as_const(take)) {
                const drift::TimeUs inUs = std::min(clip.srcIn, clip.srcOut);
                const drift::TimeUs outUs = std::max(clip.srcIn, clip.srcOut);
                for (const drift::TranscriptLine &l : drift::transcriptLines(*transcript, inUs, outUs)) {
                    drift::TimeRangeUs on;
                    if (!drift::sourceRangeToTimeline(clip, {l.startUs, l.endUs}, on))
                        continue;
                    QString speaker;
                    if (transcript->diarized && l.speaker >= 0 && l.speaker < transcript->speakers.size())
                        speaker = transcript->speakers.at(l.speaker).label;
                    Listed entry;
                    entry.line = Line{l.firstWord, l.lastWord, l.startUs, l.endUs, on.startUs, l.text};
                    entry.row = QVariantMap{{QStringLiteral("text"), l.text},
                                            {QStringLiteral("start"), drift::usToSeconds(on.startUs)},
                                            {QStringLiteral("end"), drift::usToSeconds(on.endUs)},
                                            {QStringLiteral("time"), timeLabel(on.startUs)},
                                            {QStringLiteral("speaker"), speaker}};
                    QList<std::pair<drift::TimeUs, QVariantMap>> words;
                    for (int i = std::max(0, l.firstWord); i <= l.lastWord && i < transcript->words.size(); ++i) {
                        const drift::TranscriptWord &w = transcript->words.at(i);
                        if (!drift::isSpeechToken(w) || w.text.trimmed().isEmpty())
                            continue;
                        drift::TimeRangeUs wordOn;
                        if (!drift::sourceRangeToTimeline(clip, {w.startUs, std::max(w.endUs, w.startUs + 1)}, wordOn))
                            continue;
                        words.append({wordOn.startUs,
                                      QVariantMap{{QStringLiteral("i"), i},
                                                  {QStringLiteral("clip"), clip.id},
                                                  {QStringLiteral("text"), w.text.trimmed()},
                                                  {QStringLiteral("start"), drift::usToSeconds(wordOn.startUs)},
                                                  {QStringLiteral("end"), drift::usToSeconds(wordOn.endUs)}}});
                    }
                    std::stable_sort(words.begin(), words.end(),
                                     [](const auto &a, const auto &b) { return a.first < b.first; });
                    for (const auto &word : std::as_const(words))
                        entry.words.append(word.second);
                    listed.append(entry);
                }
            }
            // Timeline order: a reversed clip says its source backwards.
            std::stable_sort(listed.begin(), listed.end(), [](const Listed &a, const Listed &b) {
                return a.line.timelineStartUs < b.line.timelineStartUs;
            });
        }
    }

    QList<Line> lines;
    QVariantList rows;
    QVariantList wordRows;
    lines.reserve(listed.size());
    rows.reserve(listed.size());
    for (Listed &entry : listed) {
        const int lineIndex = int(lines.size());
        entry.row.insert(QStringLiteral("firstRow"), int(wordRows.size()));
        entry.row.insert(QStringLiteral("rowCount"), int(entry.words.size()));
        for (QVariant &word : entry.words) {
            QVariantMap map = word.toMap();
            map.insert(QStringLiteral("line"), lineIndex);
            wordRows.append(map);
        }
        lines.append(entry.line);
        rows.append(entry.row);
    }

    const QString clipId = target ? target->id : QString();
    const bool changed = clipId != m_targetClipId || info != m_info || rows != m_lineRows || wordRows != m_wordRows;
    m_targetClipId = clipId;
    m_targetAssetId = assetId;
    m_targetSourceKey = sourceKey;
    m_lines = lines;
    if (changed) {
        m_info = info;
        m_lineRows = rows;
        m_wordRows = wordRows;
        emit transcriptChanged();
    }
}

void AiToolsController::setCaptioning(bool captioning)
{
    if (m_captioning == captioning)
        return;
    m_captioning = captioning;
    emit captioningChanged();
}

void AiToolsController::setCaptionProgress(double progress)
{
    if (qFuzzyCompare(m_captionProgress + 1.0, progress + 1.0))
        return;
    m_captionProgress = progress;
    emit captionProgressChanged();
}

void AiToolsController::setCaptionStatus(const QString &status)
{
    if (m_captionStatus == status)
        return;
    m_captionStatus = status;
    emit captionStatusChanged();
}

bool AiToolsController::createCaptions(const QString &language, int maxWordsPerCue)
{
    return startRun(language, maxWordsPerCue, true);
}

bool AiToolsController::transcribe(const QString &language)
{
    return startRun(language, 0, false);
}

bool AiToolsController::aiAllowed() const
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
    return true;
}

bool AiToolsController::startRun(const QString &language, int maxWordsPerCue, bool makeCaptions)
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
    if (m_captioning) {
        m_app->setLastMessage(m_runMakesCaptions ? tr("Captions are already being made")
                                                 : tr("Speech recognition is already running"),
                              QStringLiteral("warning"));
        return false;
    }
    const drift::Project &project = m_app->m_project;

    QList<QPair<int, int>> pairs = m_app->m_selection;
    if (pairs.isEmpty() && m_app->m_selectedTrack >= 0 && m_app->m_selectedClip >= 0)
        pairs.append(qMakePair(m_app->m_selectedTrack, m_app->m_selectedClip));

    // A linked video and its sound say the same thing over the same span: keep one, the sound, as
    // AppController::generateSubtitlesForClips does. Titles or captions in the selection are left
    // out rather than refused.
    QList<drift::Clip> clips;
    for (const QPair<int, int> &pair : std::as_const(pairs)) {
        if (!m_app->isValidClipIndex(pair.first, pair.second))
            continue;
        const drift::Clip &clip = project.tracks().at(pair.first).clips.at(pair.second);
        if (!isMediaClip(clip))
            continue;
        auto partner = std::find_if(clips.begin(), clips.end(), [&clip](const drift::Clip &taken) {
            return !clip.linkId.isEmpty() && taken.linkId == clip.linkId;
        });
        if (partner == clips.end())
            clips.append(clip);
        else if (clip.type == drift::ClipType::Audio)
            *partner = clip;
    }
    if (clips.isEmpty()) {
        m_app->setLastMessage(tr("Select a video or audio clip with speech on the timeline first"),
                              QStringLiteral("warning"));
        return false;
    }
    std::sort(clips.begin(), clips.end(), [](const drift::Clip &a, const drift::Clip &b) {
        return a.timelineStart < b.timelineStart;
    });
    // Checked now rather than after a long transcription: the caption clip refuses them anyway.
    for (int i = 1; i < clips.size(); ++i) {
        if (clips.at(i).timelineStart < clips.at(i - 1).timelineEnd()) {
            m_app->setLastMessage(tr("These clips overlap in time — caption them separately"),
                                  QStringLiteral("warning"));
            return false;
        }
    }
    const QString lang = language.trimmed().toLower();
    struct Work
    {
        QString assetId;
        QString path;
    };
    QList<Work> work;
    QStringList seen;
    for (const drift::Clip &clip : std::as_const(clips)) {
        const drift::MediaAsset *asset = clip.assetId.isEmpty() ? nullptr : project.asset(clip.assetId);
        const QString name = asset && !asset->name.isEmpty() ? asset->name : QFileInfo(clip.path).fileName();
        if (!asset || !asset->sequenceId.isEmpty()) {
            m_app->setLastMessage(tr("“%1” can't be transcribed: caption the clips inside it instead").arg(name),
                                  QStringLiteral("warning"));
            return false;
        }
        if (asset->hasAudioKnown && !asset->hasAudio) {
            m_app->setLastMessage(tr("“%1” has no sound").arg(name), QStringLiteral("warning"));
            return false;
        }
        if (seen.contains(clip.assetId))
            continue;
        seen.append(clip.assetId);
        const drift::TranscriptPtr stored = project.transcript(clip.assetId);
        const bool fresh = stored && stored->source.matches(asset->path)
                           && (lang.isEmpty() || stored->language.compare(lang, Qt::CaseInsensitive) == 0);
        if (fresh)
            continue;
        if (m_app->m_jobs->hasActive(QStringLiteral("transcribe"), clip.assetId)) {
            m_app->setLastMessage(tr("“%1” is already being transcribed").arg(name), QStringLiteral("warning"));
            return false;
        }
        work.append({clip.assetId, asset->path});
    }

    // Media transcribed already (here, or by an agent) captions without the model.
    if (!work.isEmpty() && !drift::WhisperTranscriber::modelPresent()) {
        m_app->setLastMessage(tr("Download speech recognition first"), QStringLiteral("warning"));
        return false;
    }

    m_runClipIds.clear();
    for (const drift::Clip &clip : std::as_const(clips))
        m_runClipIds.append(clip.id);
    m_runLanguage = lang;
    m_runWordsPerCue = std::max(0, maxWordsPerCue);
    m_runMakesCaptions = makeCaptions;
    m_runError.clear();
    m_runCancelled = false;

    // Everything already transcribed: the caption clip is built straight from the words.
    if (work.isEmpty()) {
        finishCaptions();
        return true;
    }

    m_jobIds.clear();
    m_jobCount = int(work.size());
    m_jobsDone = 0;
    setCaptionProgress(0.0);
    setCaptionStatus(tr("Getting speech recognition ready…"));
    setCaptioning(true);

    drift::LocalTranscribeOptions options;
    options.language = lang;
    options.align = true;
    options.vad = true;

    const QPointer<AiToolsController> self(this);
    for (int index = 0; index < work.size(); ++index) {
        const Work item = work.at(index);
        const drift::SourceFingerprint fingerprint = drift::SourceFingerprint::of(item.path);
        auto holder = std::make_shared<std::shared_ptr<drift::Transcript>>();
        // Called on the job's thread; the panel's bar is updated on ours.
        auto report = [self, index](double fraction, const QString &status) {
            if (!self)
                return;
            QMetaObject::invokeMethod(
                self.data(),
                [self, index, fraction, status] {
                    if (self)
                        self->reportJobProgress(index, fraction, status);
                },
                Qt::QueuedConnection);
        };
        const QString id = m_app->m_jobs->start(
            QStringLiteral("transcribe"), item.assetId, JobRegistry::Lane::Model,
            [path = item.path, options, fingerprint, holder, report](JobContext &ctx) {
                bool cancelled = false;
                const std::vector<float> pcm = drift::readMono16k(
                    path, 0, -1,
                    [&ctx, &report](double f) {
                        ctx.progress(0.05 * f, QStringLiteral("Reading audio…"));
                        report(0.05 * f, AiToolsController::tr("Reading the audio…"));
                        return !ctx.cancelled();
                    },
                    &cancelled);
                if (cancelled || ctx.cancelled())
                    return;
                if (pcm.empty())
                    return ctx.fail(QStringLiteral("no_audio"),
                                    AiToolsController::tr("No sound could be read from this file"));
                const drift::LocalTranscribeResult result = drift::transcribeLocal(
                    pcm, 0, options, [&ctx, &report](double f, const QString &status) {
                        ctx.progress(0.05 + 0.95 * f, status);
                        report(0.05 + 0.95 * f, status);
                        return !ctx.cancelled();
                    });
                if (result.cancelled || ctx.cancelled())
                    return;
                if (!result.transcript)
                    return ctx.fail(QStringLiteral("model_error"), result.error);
                result.transcript->source = fingerprint;
                *holder = result.transcript;
                ctx.succeed({{QStringLiteral("words"), result.transcript->speechTokenCount()},
                             {QStringLiteral("engine"), result.transcript->engine},
                             {QStringLiteral("language"), result.transcript->language},
                             {QStringLiteral("aligned"), result.transcript->wordTimingsAligned}});
            },
            [self, assetId = item.assetId, holder](const QJsonObject &job) {
                if (!self)
                    return QJsonObject{};
                const bool ok = job.value(QStringLiteral("ok")).toBool();
                if (ok && *holder)
                    self->storeTranscript(assetId, *holder);
                const QJsonObject error = job.value(QStringLiteral("error")).toObject();
                self->jobDone(job.value(QStringLiteral("id")).toString(), ok,
                              error.value(QStringLiteral("code")).toString(),
                              error.value(QStringLiteral("message")).toString());
                return QJsonObject{};
            });
        m_jobIds.append(id);
    }
    return true;
}

void AiToolsController::cancelCaptions()
{
    if (!m_captioning)
        return;
    setCaptionStatus(tr("Stopping…"));
    for (const QString &id : std::as_const(m_jobIds))
        m_app->m_jobs->cancel(id);
}

void AiToolsController::reportJobProgress(int index, double fraction, const QString &status)
{
    if (!m_captioning)
        return;
    // The model lane runs one job at a time, in the order they were queued.
    const double overall = (double(index) + qBound(0.0, fraction, 1.0)) / double(std::max(1, m_jobCount));
    setCaptionProgress(std::min(0.99, overall));
    if (status.isEmpty())
        return;
    setCaptionStatus(m_jobCount > 1 ? tr("%1 (file %2 of %3)").arg(status).arg(index + 1).arg(m_jobCount)
                                    : status);
}

void AiToolsController::jobDone(const QString &jobId, bool ok, const QString &code, const QString &message)
{
    if (!m_jobIds.removeOne(jobId))
        return;
    ++m_jobsDone;
    if (!ok) {
        if (code == QLatin1String("cancelled"))
            m_runCancelled = true;
        else if (m_runError.isEmpty())
            m_runError = message.isEmpty() ? tr("Speech recognition failed") : message;
        // The caption clip needs every transcript, so the rest would be wasted work.
        for (const QString &id : std::as_const(m_jobIds))
            m_app->m_jobs->cancel(id);
    }
    if (!m_jobIds.isEmpty())
        return;
    if (!m_runError.isEmpty())
        endCaptionRun(false, m_runError);
    else if (m_runCancelled)
        endCaptionRun(false, m_runMakesCaptions ? tr("Captions cancelled") : tr("Transcription cancelled"));
    else
        finishCaptions();
}

void AiToolsController::finishCaptions()
{
    QList<QPair<int, int>> pairs;
    for (const QString &id : std::as_const(m_runClipIds)) {
        const QVariantList at = m_app->locateClip(id);
        if (at.size() == 2)
            pairs.append(qMakePair(at.at(0).toInt(), at.at(1).toInt()));
    }
    if (pairs.isEmpty()) {
        endCaptionRun(false, tr("The clips to caption are no longer on the timeline"));
        return;
    }
    // Text-based editing only wanted the words: they are stored, so the transcript is listed.
    if (!m_runMakesCaptions) {
        m_lastCaptionedClipId = m_runClipIds.first();
        endCaptionRun(true, tr("Transcript ready"));
        return;
    }
    setCaptionStatus(tr("Building the captions…"));
    // Every transcript is stored now, so this takes the word-exact path and returns at once.
    const bool ok = m_app->generateSubtitlesForClips(pairs, m_runLanguage, m_runWordsPerCue);
    if (ok)
        m_lastCaptionedClipId = m_runClipIds.first();
    endCaptionRun(ok, ok ? tr("Captions added") : tr("No speech was found to caption"));
}

void AiToolsController::endCaptionRun(bool ok, const QString &message)
{
    m_jobIds.clear();
    m_jobCount = 0;
    m_jobsDone = 0;
    setCaptionProgress(ok ? 1.0 : 0.0);
    setCaptionStatus(message);
    setCaptioning(false);
    if (!ok && !m_runCancelled && !m_runError.isEmpty())
        m_app->setLastMessage(m_runError, QStringLiteral("error"));
    emit captionsFinished(ok, message);
    scheduleRefresh();
}

void AiToolsController::storeTranscript(const QString &assetId, std::shared_ptr<drift::Transcript> transcript)
{
    drift::Project &project = m_app->m_project;
    const drift::MediaAsset *asset = project.asset(assetId);
    if (!asset || !transcript)
        return;
    // The file changed while it was being heard: these words belong to the old one.
    if (!transcript->source.isEmpty() && !transcript->source.matches(asset->path))
        return;
    project.setTranscript(assetId, std::move(transcript));
    if (m_app->m_mcpController)
        m_app->m_mcpController->bumpEditRevision();
    m_app->projectFile()->setDirty(true);
    scheduleRefresh();
}

bool AiToolsController::setLineText(int line, const QString &text)
{
    if (line < 0 || line >= m_lines.size() || m_targetAssetId.isEmpty())
        return false;
    drift::Project &project = m_app->m_project;
    const drift::TranscriptPtr current = project.transcript(m_targetAssetId);
    const Line ref = m_lines.at(line);
    // The words moved under this line since it was listed (an agent transcribed again, say).
    if (!current || speechText(*current, ref.firstWord, ref.lastWord) != ref.text) {
        refresh();
        return false;
    }

    auto edited = std::make_shared<drift::Transcript>(*current);
    const bool changed = drift::replaceTranscriptWords(*edited, ref.firstWord, ref.lastWord, text);
    if (changed) {
        project.setTranscript(m_targetAssetId, edited);
        if (m_app->m_mcpController)
            m_app->m_mcpController->bumpEditRevision();
        m_app->projectFile()->setDirty(true);
    }
    // Always, even when the words were already right: captions an undo put back catch up.
    const bool captions = syncCaptions(m_targetSourceKey, changed ? *edited : *current,
                                       ref.sourceStartUs, ref.sourceEndUs);
    refresh();
    return changed || captions;
}

bool AiToolsController::syncCaptions(const QString &sourceKey, const drift::Transcript &transcript,
                                     drift::TimeUs startUs, drift::TimeUs endUs)
{
    if (sourceKey.isEmpty())
        return false;
    drift::Project &project = m_app->m_project;
    const drift::Project before = project;

    // Every clip playing this media: a caption's words are read through the one it sits over.
    QList<drift::Clip> media;
    for (const drift::Track &track : project.tracks()) {
        for (const drift::Clip &clip : track.clips) {
            if (isMediaClip(clip) && drift::subtitleSourceKey(clip) == sourceKey)
                media.append(clip);
        }
    }
    if (media.isEmpty())
        return false;

    bool anyChanged = false;
    for (drift::Track &track : project.tracks()) {
        for (drift::Clip &sub : track.clips) {
            if (sub.type != drift::ClipType::Subtitle)
                continue;
            QList<drift::SubtitleCue> cues = sub.subtitleCues;
            bool changed = false;
            for (int k = int(cues.size()) - 1; k >= 0; --k) {
                const drift::SubtitleCue &cue = cues.at(k);
                if (cue.source != sourceKey)
                    continue;
                const drift::TimeUs a = sub.timelineStart + cue.startUs;
                const drift::TimeUs b = sub.timelineStart + cue.endUs;
                const drift::Clip *over = nullptr;
                drift::TimeUs best = 0;
                for (const drift::Clip &clip : std::as_const(media)) {
                    const drift::TimeUs overlap = std::min(b, clip.timelineEnd()) - std::max(a, clip.timelineStart);
                    if (overlap > best) {
                        best = overlap;
                        over = &clip;
                    }
                }
                if (!over)
                    continue;
                drift::TimeUs s0 = over->timelineToSourceUs(std::max(a, over->timelineStart));
                drift::TimeUs s1 = over->timelineToSourceUs(std::min(b, over->timelineEnd()));
                if (s1 < s0)
                    std::swap(s0, s1);
                if (s1 <= startUs || s0 >= endUs)
                    continue;
                const QString text = drift::transcriptTextInWindow(transcript, s0, s1);
                if (text == cue.text)
                    continue;
                if (text.isEmpty())
                    cues.removeAt(k);
                else
                    cues[k].text = text;
                changed = true;
            }
            if (!changed)
                continue;
            // A clip still carrying its generated name ("Subtitles (12)") keeps counting its cues.
            if (sub.name == drift::subtitleClipName(sub.subtitleCues))
                sub.name = drift::subtitleClipName(cues);
            sub.subtitleCues = cues;
            anyChanged = true;
        }
    }
    if (!anyChanged)
        return false;
    m_app->pushProjectEdit(before, tr("Caption corrected"));
    m_app->finishEdit(tr("Caption corrected"));
    return true;
}

void AiToolsController::seekToLine(int line)
{
    if (line < 0 || line >= m_lines.size())
        return;
    m_app->setPlayheadSeconds(drift::usToSeconds(m_lines.at(line).timelineStartUs));
}

bool AiToolsController::releaseSpeechModels()
{
    if (m_captioning || m_app->subtitleGenerating())
        return false;
    for (const QJsonValue &value : m_app->m_jobs->jobs()) {
        const QJsonObject job = value.toObject();
        const QString kind = job.value(QStringLiteral("kind")).toString();
        if (job.value(QStringLiteral("active")).toBool()
            && (kind == QLatin1String("transcribe") || kind == QLatin1String("diarize")))
            return false;
    }
    drift::WhisperTranscriber::instance().unload();
    drift::CtcAligner::instance().unload();
    drift::SpeakerDiarizer::instance().unload();
    return true;
}

// ---- Text-based editing ----------------------------------------------------------------------

QList<drift::Clip> AiToolsController::focusTake() const
{
    const QVariantList at = m_targetClipId.isEmpty() ? QVariantList{} : m_app->locateClip(m_targetClipId);
    if (at.size() != 2)
        return {};
    return takeOf(m_app->m_project, m_app->m_project.tracks().at(at.at(0).toInt()).clips.at(at.at(1).toInt()));
}

bool AiToolsController::applySourceCuts(const QHash<QString, QList<drift::TimeRangeUs>> &cuts,
                                        const QString &editName, drift::TimeUs *removedUs)
{
    drift::Project &project = m_app->m_project;
    struct Job
    {
        drift::Clip clip;
        QList<drift::TimeRangeUs> source;
    };
    QList<Job> jobs;
    for (auto it = cuts.cbegin(); it != cuts.cend(); ++it) {
        const QVariantList at = m_app->locateClip(it.key());
        const QList<drift::TimeRangeUs> source = drift::normalizedRanges(it.value());
        if (at.size() != 2 || source.isEmpty())
            continue;
        jobs.append({project.tracks().at(at.at(0).toInt()).clips.at(at.at(1).toInt()), source});
    }
    if (jobs.isEmpty()) {
        m_app->setLastMessage(tr("Nothing to cut"), QStringLiteral("warning"));
        return false;
    }

    // Protected footage refuses the whole edit before anything changes.
    QStringList refused;
    for (const Job &job : std::as_const(jobs)) {
        for (const drift::ProtectedRange &r :
             drift::protectedOverlaps(project.protectedRanges(), job.clip.assetId, job.source)) {
            const QString label = rangeLabel({r.srcStartUs, r.srcEndUs});
            refused.append(r.reason.isEmpty() ? label : QStringLiteral("%1 (%2)").arg(label, r.reason));
        }
    }
    if (!refused.isEmpty()) {
        refused.removeDuplicates();
        m_app->setLastMessage(tr("That would cut protected footage: %1. Keep those words, or remove the "
                                 "protection first.").arg(refused.join(QStringLiteral(", "))),
                              QStringLiteral("warning"));
        return false;
    }

    // Back to front: a cut only moves what lies after it, so earlier clips keep their place.
    std::sort(jobs.begin(), jobs.end(),
              [](const Job &a, const Job &b) { return a.clip.timelineStart > b.clip.timelineStart; });
    const drift::Project before = project;
    const drift::TimeUs minKeepUs = std::max<drift::TimeUs>(drift::kCutMinEdgeUs, 2 * kDeclickUs);
    drift::TimeUs total = 0;
    bool changed = false;
    for (const Job &job : std::as_const(jobs)) {
        const QVariantList at = m_app->locateClip(job.clip.id);
        if (at.size() != 2)
            continue;
        const drift::Clip clip = project.tracks().at(at.at(0).toInt()).clips.at(at.at(1).toInt());
        QList<drift::TimeRangeUs> removedTimeline;
        for (const drift::TimeRangeUs &r : job.source) {
            drift::TimeRangeUs tl;
            if (drift::sourceRangeToTimeline(clip, r, tl))
                removedTimeline.append(tl);
        }
        if (removedTimeline.isEmpty())
            continue;
        const QList<drift::TimeRangeUs> kept = drift::keptTimelineIntervals(clip, removedTimeline, minKeepUs);
        // What actually goes: the clip less what is kept (a sliver too short to keep goes too).
        QList<drift::TimeRangeUs> removed;
        drift::TimeUs cursor = clip.timelineStart;
        for (const drift::TimeRangeUs &k : kept) {
            if (k.startUs > cursor)
                removed.append({cursor, k.startUs});
            cursor = k.endUs;
        }
        if (cursor < clip.timelineEnd())
            removed.append({cursor, clip.timelineEnd()});
        if (removed.isEmpty())
            continue;
        QString error;
        if (!m_app->replaceClipGroupWithSegments(
                at.at(0).toInt(), at.at(1).toInt(),
                [&kept](const drift::Clip &member) { return drift::packedSegments(member, kept, kDeclickUs); },
                true, nullptr, &error)) {
            project = before;
            m_app->setLastMessage(tr("Could not make the cut"), QStringLiteral("error"));
            return false;
        }
        const drift::TranscriptPtr transcript = clip.assetId.isEmpty() ? nullptr : project.transcript(clip.assetId);
        drift::captionsFollowCut(project, clip, removed, transcript.get());
        for (const drift::TimeRangeUs &r : std::as_const(removed))
            total += r.length();
        changed = true;
    }
    if (!changed) {
        m_app->setLastMessage(tr("Nothing to cut"), QStringLiteral("warning"));
        return false;
    }
    // The same guard after the edit, for footage the planned ranges did not show (a linked clip
    // whose own trim drifted, a sliver dropped beside a protected range).
    if (!drift::protectedLoss(before.protectedRanges(), before, project).isEmpty()) {
        project = before;
        m_app->setLastMessage(tr("That would cut protected footage. Keep those words, or remove the "
                                 "protection first."),
                              QStringLiteral("warning"));
        return false;
    }
    m_app->pushProjectEdit(before, editName);
    m_app->finishEdit(editName);
    if (removedUs)
        *removedUs = total;
    return true;
}

bool AiToolsController::cutWords(const QVariantList &words)
{
    if (!aiAllowed())
        return false;
    refresh();
    if (m_targetAssetId.isEmpty())
        return false;
    const drift::TranscriptPtr transcript = m_app->m_project.transcript(m_targetAssetId);
    if (!transcript)
        return false;
    QHash<QString, QList<int>> byClip;
    int count = 0;
    for (const QVariant &v : words) {
        const QVariantMap m = v.toMap();
        const QString clipId = m.value(QStringLiteral("clip")).toString();
        const int i = m.value(QStringLiteral("i"), -1).toInt();
        if (clipId.isEmpty() || i < 0 || i >= transcript->words.size())
            continue;
        byClip[clipId].append(i);
        ++count;
    }
    QHash<QString, QList<drift::TimeRangeUs>> cuts;
    for (const drift::Clip &clip : focusTake()) {
        const auto it = byClip.constFind(clip.id);
        if (it == byClip.constEnd())
            continue;
        const QList<drift::TimeRangeUs> ranges = drift::sourceCutsForWords(*transcript, clip, *it, kWordPadUs);
        if (!ranges.isEmpty())
            cuts.insert(clip.id, ranges);
    }
    if (cuts.isEmpty()) {
        m_app->setLastMessage(tr("Select words in the transcript first"), QStringLiteral("warning"));
        refresh();
        return false;
    }
    drift::TimeUs removed = 0;
    if (!applySourceCuts(cuts, tr("Cut words"), &removed))
        return false;
    m_app->setLastMessage(tr("Cut %n word(s), %1 s", nullptr, count).arg(drift::usToSeconds(removed), 0, 'f', 1),
                          QStringLiteral("success"));
    refresh();
    return true;
}

void AiToolsController::seekToWord(int row)
{
    if (row < 0 || row >= m_wordRows.size())
        return;
    m_app->setPlayheadSeconds(m_wordRows.at(row).toMap().value(QStringLiteral("start")).toDouble());
}

// ---- Silence & filler words --------------------------------------------------------------------

QVariantList AiToolsController::fillerWords(const QString &language) const
{
    QVariantList out;
    for (const drift::FillerWord &f : storedFillers(language))
        out.append(QVariantMap{{QStringLiteral("text"), f.text}, {QStringLiteral("enabled"), f.enabled}});
    return out;
}

void AiToolsController::setFillerWords(const QString &language, const QVariantList &rows)
{
    QVariantList clean;
    QStringList seen;
    for (const QVariant &v : rows) {
        const QVariantMap m = v.toMap();
        const QString text = m.value(QStringLiteral("text")).toString().simplified();
        if (text.isEmpty() || seen.contains(text.toLower()))
            continue;
        seen.append(text.toLower());
        clean.append(QVariantMap{{QStringLiteral("text"), text},
                                 {QStringLiteral("enabled"), m.value(QStringLiteral("enabled"), true).toBool()}});
    }
    QSettings().setValue(QLatin1String(kFillerKey) + fillerLangKey(language), clean);
}

void AiToolsController::resetFillerWords(const QString &language)
{
    QSettings().remove(QLatin1String(kFillerKey) + fillerLangKey(language));
}

QString AiToolsController::fillerLanguage() const
{
    const QString lang = fillerLangKey(m_info.value(QStringLiteral("language")).toString());
    return lang.isEmpty() ? QStringLiteral("en") : lang;
}

void AiToolsController::setCleanupBusy(bool busy, const QString &status)
{
    if (m_cleanupBusy == busy && m_cleanupStatus == status)
        return;
    m_cleanupBusy = busy;
    m_cleanupStatus = status;
    emit cleanupBusyChanged();
}

void AiToolsController::clearCleanup()
{
    if (m_cleanupHits.isEmpty() && !m_cleanupFound && m_cleanupClipId.isEmpty())
        return;
    m_cleanupHits.clear();
    m_cleanupTake.clear();
    m_cleanupClipId.clear();
    m_cleanupFound = false;
    publishCleanup();
}

void AiToolsController::cancelCleanup()
{
    m_silenceQueue.clear();
    if (m_silenceConnection)
        disconnect(m_silenceConnection);
    if (m_cleanupBusy) {
        if (AudioToolsController *tools = m_app->audioTools())
            tools->cancel();
        m_silenceClipId.clear();
        setCleanupBusy(false, tr("Cancelled"));
    }
}

bool AiToolsController::findCleanup(bool silences, bool fillers, double thresholdDb, double minSeconds,
                                    double paddingSeconds, const QString &language)
{
    if (!aiAllowed() || m_cleanupBusy)
        return false;
    if (!silences && !fillers) {
        m_app->setLastMessage(tr("Tick silences, filler words or both"), QStringLiteral("warning"));
        return false;
    }
    refresh();
    const QList<drift::Clip> take = focusTake();
    if (take.isEmpty()) {
        m_app->setLastMessage(tr("Select a video or audio clip on the timeline first"), QStringLiteral("warning"));
        return false;
    }
    clearCleanup();
    m_cleanupClipId = m_targetClipId;
    for (const drift::Clip &clip : take)
        m_cleanupTake.append({clip.id, clip.timelineStart, clip.timelineDuration});

    if (fillers) {
        const drift::TranscriptPtr transcript =
            m_targetAssetId.isEmpty() ? nullptr : m_app->m_project.transcript(m_targetAssetId);
        if (!transcript || !m_info.value(QStringLiteral("hasTranscript")).toBool()) {
            if (!silences) {
                m_app->setLastMessage(tr("Filler words come from the transcript: transcribe this clip first"),
                                      QStringLiteral("warning"));
                m_cleanupClipId.clear();
                m_cleanupTake.clear();
                return false;
            }
        } else {
            const QList<drift::FillerWord> list = storedFillers(language);
            for (const drift::Clip &clip : take) {
                const drift::TimeUs inUs = std::min(clip.srcIn, clip.srcOut);
                const drift::TimeUs outUs = std::max(clip.srcIn, clip.srcOut);
                for (const drift::FillerHit &f : drift::findFillerWords(*transcript, inUs, outUs, list)) {
                    QList<int> words;
                    for (int i = f.firstWord; i <= f.lastWord; ++i)
                        words.append(i);
                    // The same cut as deleting those words in Text-based editing.
                    const QList<drift::TimeRangeUs> cut = drift::sourceCutsForWords(*transcript, clip, words, kWordPadUs);
                    drift::TimeRangeUs on;
                    if (cut.isEmpty() || !drift::sourceRangeToTimeline(clip, {f.startUs, std::max(f.endUs, f.startUs + 1)}, on))
                        continue;
                    m_cleanupHits.append({QStringLiteral("filler"), clip.id,
                                          {cut.first().startUs, cut.last().endUs}, on, f.text});
                }
            }
        }
    }

    m_silenceThreshold = thresholdDb;
    m_silenceMin = minSeconds;
    m_silencePadding = paddingSeconds;
    m_silenceQueue.clear();
    if (silences) {
        for (const drift::Clip &clip : take)
            m_silenceQueue.append(clip.id);
    }
    if (m_silenceQueue.isEmpty()) {
        m_cleanupFound = true;
        publishCleanup();
        return true;
    }
    setCleanupBusy(true, tr("Finding silence…"));
    findNextSilence();
    return true;
}

void AiToolsController::findNextSilence()
{
    AudioToolsController *tools = m_app->audioTools();
    while (!m_silenceQueue.isEmpty()) {
        const QString clipId = m_silenceQueue.takeFirst();
        const QVariantList at = m_app->locateClip(clipId);
        if (!tools || at.size() != 2 || !tools->canProcess(at.at(0).toInt(), at.at(1).toInt()))
            continue;
        if (tools->running()) {
            m_silenceQueue.clear();
            m_app->setLastMessage(tr("Another audio tool is still working; try again when it is done"),
                                  QStringLiteral("warning"));
            break;
        }
        m_silenceClipId = clipId;
        m_silenceConnection = connect(tools, &AudioToolsController::finished, this, &AiToolsController::silenceFound,
                                      Qt::SingleShotConnection);
        // Silence remover's own detection: the clip as it plays, every other clip muted.
        tools->findSilence(at.at(0).toInt(), at.at(1).toInt(), m_silenceThreshold, m_silenceMin, m_silencePadding);
        if (tools->running())
            return;
        // It refused at once (no silencedetect filter, say) and has said why.
        disconnect(m_silenceConnection);
        m_silenceClipId.clear();
        m_silenceQueue.clear();
        break;
    }
    m_silenceClipId.clear();
    setCleanupBusy(false);
    m_cleanupFound = true;
    publishCleanup();
}

void AiToolsController::silenceFound(bool ok, const QString &message)
{
    Q_UNUSED(message)
    AudioToolsController *tools = m_app->audioTools();
    const QString clipId = m_silenceClipId;
    m_silenceClipId.clear();
    if (!ok || !tools) {
        m_silenceQueue.clear();
        if (tools)
            tools->clearSilencePreview();
        setCleanupBusy(false, message);
        m_cleanupFound = !m_cleanupHits.isEmpty();
        publishCleanup();
        return;
    }
    // The preview is in the analysed clip's own span (a video's separated sound, say).
    const QVariantList audibleAt = m_app->locateClip(tools->silencePreviewClipId());
    if (audibleAt.size() == 2) {
        const drift::Clip audible =
            m_app->m_project.tracks().at(audibleAt.at(0).toInt()).clips.at(audibleAt.at(1).toInt());
        for (const QVariant &v : tools->silencePreview()) {
            const QVariantMap r = v.toMap();
            const drift::TimeUs a = audible.timelineStart + drift::secondsToUs(r.value(QStringLiteral("start")).toDouble());
            const drift::TimeUs b = audible.timelineStart + drift::secondsToUs(r.value(QStringLiteral("end")).toDouble());
            drift::TimeUs s0 = audible.timelineToSourceUs(a);
            drift::TimeUs s1 = audible.timelineToSourceUs(b);
            if (s1 < s0)
                std::swap(s0, s1);
            if (b > a && s1 > s0)
                m_cleanupHits.append({QStringLiteral("silence"), clipId, {s0, s1}, {a, b}, QString()});
        }
    }
    // Its list lives here now; the Properties panel's own preview is not left behind.
    tools->clearSilencePreview();
    findNextSilence();
}

void AiToolsController::publishCleanup()
{
    std::stable_sort(m_cleanupHits.begin(), m_cleanupHits.end(), [](const CleanupHit &a, const CleanupHit &b) {
        return a.timeline.startUs < b.timeline.startUs;
    });
    QVariantList rows;
    for (const CleanupHit &h : std::as_const(m_cleanupHits)) {
        const bool silence = h.kind == QLatin1String("silence");
        rows.append(QVariantMap{{QStringLiteral("kind"), h.kind},
                                {QStringLiteral("text"), silence ? tr("Pause") : h.text},
                                {QStringLiteral("time"), timeLabel(h.timeline.startUs)},
                                {QStringLiteral("start"), drift::usToSeconds(h.timeline.startUs)},
                                {QStringLiteral("end"), drift::usToSeconds(h.timeline.endUs)},
                                {QStringLiteral("seconds"), drift::usToSeconds(h.source.length())}});
    }
    m_cleanupRows = rows;
    emit cleanupChanged();
}

double AiToolsController::cleanupSeconds(const QVariantList &rows) const
{
    QHash<QString, QList<drift::TimeRangeUs>> byClip;
    for (const QVariant &v : rows) {
        const int row = v.toInt();
        if (row >= 0 && row < m_cleanupHits.size())
            byClip[m_cleanupHits.at(row).clipId].append(m_cleanupHits.at(row).source);
    }
    drift::TimeUs total = 0;
    for (auto it = byClip.cbegin(); it != byClip.cend(); ++it) {
        for (const drift::TimeRangeUs &r : drift::normalizedRanges(it.value()))
            total += r.length();
    }
    return drift::usToSeconds(total);
}

void AiToolsController::playCleanupHit(int row)
{
    if (row < 0 || row >= m_cleanupHits.size())
        return;
    const drift::TimeRangeUs on = m_cleanupHits.at(row).timeline;
    constexpr drift::TimeUs kAround = 400'000;
    m_app->setPlayheadSeconds(drift::usToSeconds(std::max<drift::TimeUs>(0, on.startUs - kAround)));
    m_app->setPlaying(true);
    m_hitStopTimer->start(int((on.length() + 2 * kAround) / 1000));
}

bool AiToolsController::removeCleanupHits(const QVariantList &rows)
{
    if (!aiAllowed() || m_cleanupBusy || m_cleanupHits.isEmpty())
        return false;
    // The hits point at the take as it was found; a move or trim since makes them point at the
    // wrong sound, so it has to be found again.
    for (const TakeClip &t : std::as_const(m_cleanupTake)) {
        const QVariantList at = m_app->locateClip(t.id);
        const drift::Clip *clip = at.size() == 2
            ? &m_app->m_project.tracks().at(at.at(0).toInt()).clips.at(at.at(1).toInt()) : nullptr;
        if (!clip || clip->timelineStart != t.startUs || clip->timelineDuration != t.durationUs) {
            clearCleanup();
            m_app->setLastMessage(tr("The clip changed since this was found; press Find again"),
                                  QStringLiteral("warning"));
            return false;
        }
    }
    QHash<QString, QList<drift::TimeRangeUs>> cuts;
    int fillers = 0;
    int pauses = 0;
    for (const QVariant &v : rows) {
        const int row = v.toInt();
        if (row < 0 || row >= m_cleanupHits.size())
            continue;
        const CleanupHit &h = m_cleanupHits.at(row);
        cuts[h.clipId].append(h.source);
        (h.kind == QLatin1String("silence") ? pauses : fillers) += 1;
    }
    if (cuts.isEmpty()) {
        m_app->setLastMessage(tr("Tick what to remove first"), QStringLiteral("warning"));
        return false;
    }
    drift::TimeUs removed = 0;
    if (!applySourceCuts(cuts, tr("Remove silence and filler words"), &removed))
        return false;
    clearCleanup();
    m_app->setLastMessage(tr("Removed %1 pause(s) and %2 filler word(s), %3 s")
                              .arg(pauses).arg(fillers).arg(drift::usToSeconds(removed), 0, 'f', 1),
                          QStringLiteral("success"));
    refresh();
    return true;
}
