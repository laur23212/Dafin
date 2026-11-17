#include "app/jobs/ai/VocalRemoverController.h"

#include "app/editor/AppController.h"
#include "app/editor/appcontroller/AppControllerInternal.h"
#include "core/base/LowSpec.h"
#include "core/base/SafeMode.h"
#include "core/edit/TimelineOps.h"
#include "engine/decode/readers/ClipReaderPool.h"
#include "engine/encode/writers/AudioFileWriter.h"
#include "engine/ml/audio/DemucsSeparator.h"
#include "engine/ml/runtime/OrtRuntime.h"

#include <QFile>
#include <QFileInfo>
#include <QMetaObject>
#include <QTimer>
#include <QUndoStack>
#include <QUuid>
#include <QtConcurrent>

#include <algorithm>
#include <memory>

namespace {

constexpr quint64 kVocalsScanStreamId = 0xA5'11'5C'A4'00'00'00'31ull;

bool carriesAudio(const drift::Clip &clip)
{
    return clip.type == drift::ClipType::Audio
           || (clip.type == drift::ClipType::Video && !clip.suppressEmbeddedAudio);
}

// The clip whose sound `clip` plays: itself, or for a video whose audio was separated, its audio
// partner. (The same rule as Audio tools, Remove silence and Sync audio.)
const drift::Clip *audibleClip(const drift::Project &project, const drift::Clip &clip)
{
    if (carriesAudio(clip))
        return &clip;
    for (const drift::ClipRef &ref : drift::linkedPartners(project, clip)) {
        if (project.tracks().at(ref.trackIndex).type != drift::TrackType::Audio)
            continue;
        const drift::Clip &partner = project.tracks().at(ref.trackIndex).clips.at(ref.clipIndex);
        if (carriesAudio(partner))
            return &partner;
    }
    return nullptr;
}

bool findClip(const drift::Project &project, const QString &id, int *trackOut, int *clipOut)
{
    for (int t = 0; t < project.tracks().size(); ++t) {
        const QList<drift::Clip> &clips = project.tracks().at(t).clips;
        for (int c = 0; c < clips.size(); ++c) {
            if (clips.at(c).id == id) {
                *trackOut = t;
                *clipOut = c;
                return true;
            }
        }
    }
    return false;
}

void nameWith(drift::Clip &clip, const QString &suffix, const QString &fallback)
{
    const QString base = clip.name.isEmpty() ? fallback : clip.name;
    clip.name = base.endsWith(suffix) ? base : base + suffix;
}

} // namespace

VocalRemoverController::VocalRemoverController(AppController *app, QObject *parent)
    : QObject(parent)
    , m_app(app)
{
    m_refreshTimer = new QTimer(this);
    m_refreshTimer->setSingleShot(true);
    m_refreshTimer->setInterval(0);
    connect(m_refreshTimer, &QTimer::timeout, this, &VocalRemoverController::refresh);
    connect(m_app, &AppController::selectionChanged, this, &VocalRemoverController::scheduleRefresh);
    connect(m_app, &AppController::tracksChanged, this, &VocalRemoverController::scheduleRefresh);
    connect(&m_app->m_undoStack, &QUndoStack::indexChanged, this, &VocalRemoverController::scheduleRefresh);
    connect(m_app, &AppController::projectReset, this, &VocalRemoverController::scheduleRefresh);
}

VocalRemoverController::~VocalRemoverController()
{
    m_cancel.storeRelaxed(1);
    if (m_future.isRunning())
        m_future.waitForFinished();
}

void VocalRemoverController::scheduleRefresh()
{
    if (!m_refreshTimer->isActive())
        m_refreshTimer->start();
}

void VocalRemoverController::refresh()
{
    m_refreshTimer->stop();
    bool can = false;
    QString name;
    if (canProcess(m_app->m_selectedTrack, m_app->m_selectedClip)) {
        const drift::Clip &clip = m_app->m_project.tracks().at(m_app->m_selectedTrack).clips.at(m_app->m_selectedClip);
        const drift::Clip *audible = audibleClip(m_app->m_project, clip);
        can = true;
        name = clip.name.isEmpty() ? QFileInfo(audible->path).fileName() : clip.name;
    }
    if (can == m_canSeparate && name == m_clipName)
        return;
    m_canSeparate = can;
    m_clipName = name;
    emit selectionInfoChanged();
}

bool VocalRemoverController::modelReady() const
{
    if (m_testModel)
        return true;
    return drift::DemucsSeparator::modelPresent() && drift::ort::available();
}

bool VocalRemoverController::canProcess(int trackIndex, int clipIndex) const
{
    if (!m_app->isValidClipIndex(trackIndex, clipIndex))
        return false;
    const drift::Clip &clip = m_app->m_project.tracks().at(trackIndex).clips.at(clipIndex);
    const drift::Clip *audible = audibleClip(m_app->m_project, clip);
    return audible && !audible->path.isEmpty() && audible->sequenceId.isEmpty()
           && audible->srcOut > audible->srcIn;
}

void VocalRemoverController::setModelForTesting(drift::stems::SegmentModel model, int segmentFrames,
                                                int sampleRate)
{
    m_testModel = std::move(model);
    m_testSegmentFrames = segmentFrames;
    m_testSampleRate = sampleRate;
}

void VocalRemoverController::cancel()
{
    if (!m_running)
        return;
    m_cancel.storeRelaxed(1);
    m_status = tr("Stopping…");
    emit statusChanged();
}

bool VocalRemoverController::releaseModel()
{
    if (m_running)
        return false;
    drift::DemucsSeparator::instance().unload();
    return true;
}

bool VocalRemoverController::begin(const QString &message)
{
    if (m_running) {
        m_app->setLastMessage(tr("The vocal remover is already running"), QStringLiteral("warning"));
        return false;
    }
    // Same reason as the other audio jobs: playback would drive the decode pool from a second
    // thread while this walks it.
    m_app->setPlaying(false);
    m_cancel.storeRelaxed(0);
    m_progress = 0.0;
    emit progressChanged();
    m_status = message;
    emit statusChanged();
    m_running = true;
    emit runningChanged();
    m_app->setLastMessage(message);
    return true;
}

void VocalRemoverController::end()
{
    m_running = false;
    emit runningChanged();
}

void VocalRemoverController::reportProgress(double fraction, const QString &status)
{
    QMetaObject::invokeMethod(
        this,
        [this, fraction, status]() {
            if (!m_running)
                return;
            m_progress = std::clamp(fraction, 0.0, 1.0);
            emit progressChanged();
            if (!status.isEmpty() && status != m_status && m_cancel.loadRelaxed() == 0) {
                m_status = status;
                emit statusChanged();
            }
        },
        Qt::QueuedConnection);
}

void VocalRemoverController::separate(int output)
{
    separateClip(m_app->m_selectedTrack, m_app->m_selectedClip, output);
}

void VocalRemoverController::separateClip(int trackIndex, int clipIndex, int output)
{
    if (drift::safemode::active()) {
        m_app->setLastMessage(tr("AI tools are off in safe mode. Restart normally to use them."),
                              QStringLiteral("warning"));
        return;
    }
    if (drift::lowspec::active() && !m_testModel) {
        m_app->setLastMessage(tr("Local AI is off in compatibility mode: this computer is below Dafin's "
                                 "recommended specs. Settings › Performance › Low-spec mode turns it back on."),
                              QStringLiteral("warning"));
        return;
    }
    if (output < VocalsOnly || output > Both)
        output = Both;
    if (!canProcess(trackIndex, clipIndex)) {
        m_app->setLastMessage(tr("Select a clip with sound on the timeline first"), QStringLiteral("warning"));
        return;
    }
    if (!modelReady()) {
        m_app->setLastMessage(tr("Download the vocal remover first"), QStringLiteral("warning"));
        return;
    }
    const drift::Clip &target = m_app->m_project.tracks().at(trackIndex).clips.at(clipIndex);
    const drift::Clip audible = *audibleClip(m_app->m_project, target);
    if (!audible.assetId.isEmpty()) {
        if (const drift::MediaAsset *asset = m_app->m_project.asset(audible.assetId);
            asset && asset->hasAudioKnown && !asset->hasAudio) {
            m_app->setLastMessage(tr("This clip has no sound"), QStringLiteral("warning"));
            return;
        }
    }

    const bool wantVocals = output != MusicOnly;
    const bool wantMusic = output != VocalsOnly;
    const QString vocalsPath = wantVocals ? drift::newDenoisePath(QStringLiteral("-vocals")) : QString();
    const QString musicPath = wantMusic ? drift::newDenoisePath(QStringLiteral("-music")) : QString();
    if ((wantVocals && vocalsPath.isEmpty()) || (wantMusic && musicPath.isEmpty())) {
        m_app->setLastMessage(tr("Could not create an output file"), QStringLiteral("error"));
        return;
    }
    if (!begin(tr("Getting the vocal remover ready…")))
        return;

    const QString path = audible.path;
    const QString clipId = audible.id;
    const int ordinal = audible.audioStreamIndex;
    const drift::TimeUs srcIn = audible.srcIn;
    const drift::TimeUs srcOut = audible.srcOut;
    const bool testing = bool(m_testModel);
    const int rate = testing ? m_testSampleRate : drift::DemucsSeparator::sampleRate();
    const int segment = testing ? m_testSegmentFrames : drift::DemucsSeparator::segmentFrames();
    drift::stems::SegmentModel model = m_testModel;
    if (!model) {
        model = [](const float *in, float *out, QString *error) {
            return drift::DemucsSeparator::instance().separateSegment(in, out, error);
        };
    }

    m_future = QtConcurrent::run([this, path, clipId, ordinal, srcIn, srcOut, output, rate, segment, model,
                                  testing, vocalsPath, musicPath]() {
        QString error;
        bool ok = true;
        if (!testing && !drift::DemucsSeparator::instance().available()) {
            ok = false;
            error = drift::DemucsSeparator::instance().lastError();
        }

        drift::AudioFileWriter vocalsWriter;
        drift::AudioFileWriter musicWriter;
        if (ok && !vocalsPath.isEmpty())
            ok = vocalsWriter.open(vocalsPath, rate, drift::stems::kChannels, &error);
        if (ok && !musicPath.isEmpty())
            ok = musicWriter.open(musicPath, rate, drift::stems::kChannels, &error);

        bool cancelled = false;
        if (ok) {
            reportProgress(0.0, tr("Separating the voice from the music…"));
            const int64_t total = (srcOut - srcIn) * rate / drift::kUsPerSecond;
            int64_t read = 0;
            // The clip's source window, in the source's own time base: speed and reverse stay on
            // the clip, so the new files line up with the old one.
            const auto feed = [&](float *dst, int maxFrames) {
                if (m_cancel.loadRelaxed() != 0)
                    return -1;
                const int want = int(std::min<int64_t>(maxFrames, total - read));
                if (want <= 0)
                    return 0;
                const drift::TimeUs at = srcIn + drift::TimeUs(read * drift::kUsPerSecond / rate);
                int got = ClipReaderPool::instance().readAudioInterleaved(path, kVocalsScanStreamId, at, want,
                                                                          rate, dst, ordinal);
                if (got <= 0) {
                    // Past the decodable end: the rest is silence, so the files keep the clip's length.
                    std::fill(dst, dst + size_t(want) * drift::stems::kChannels, 0.0f);
                    got = want;
                }
                read += got;
                return got;
            };
            const auto sink = [&error](drift::AudioFileWriter &writer) {
                return [&writer, &error](const float *data, int frames) {
                    return writer.writeFrames(data, frames, &error);
                };
            };
            const drift::stems::Result result = drift::stems::separateVocals(
                total, drift::stems::Layout{segment, segment / 4}, feed, model,
                vocalsPath.isEmpty() ? drift::stems::Sink() : drift::stems::Sink(sink(vocalsWriter)),
                musicPath.isEmpty() ? drift::stems::Sink() : drift::stems::Sink(sink(musicWriter)),
                [this](double f) {
                    reportProgress(0.01 + 0.98 * f);
                    return m_cancel.loadRelaxed() == 0;
                });
            ok = result.ok;
            cancelled = result.cancelled || m_cancel.loadRelaxed() != 0;
            if (!ok && error.isEmpty())
                error = result.error;
        }
        if (ok && !vocalsPath.isEmpty())
            ok = vocalsWriter.finish(&error);
        if (ok && !musicPath.isEmpty())
            ok = musicWriter.finish(&error);
        if (!ok) {
            vocalsWriter.abort();
            musicWriter.abort();
        }
        // About 1.3 GB while loaded: give it back while editing. Loading again takes a second or two.
        if (!testing)
            drift::DemucsSeparator::instance().unload();

        QMetaObject::invokeMethod(
            this,
            [this, ok, cancelled, error, clipId, srcIn, srcOut, output, vocalsPath, musicPath]() {
                end();
                if (!ok) {
                    failRun(cancelled ? tr("Vocal remover cancelled") : error, cancelled, vocalsPath, musicPath);
                    return;
                }
                finishSeparation(clipId, srcIn, srcOut, output, vocalsPath, musicPath);
            },
            Qt::QueuedConnection);
    });
}

void VocalRemoverController::failRun(const QString &message, bool cancelled, const QString &vocalsPath,
                                     const QString &musicPath)
{
    if (!vocalsPath.isEmpty())
        QFile::remove(vocalsPath);
    if (!musicPath.isEmpty())
        QFile::remove(musicPath);
    const QString text = message.isEmpty() ? tr("The vocal remover failed") : message;
    m_progress = 0.0;
    emit progressChanged();
    m_status = text;
    emit statusChanged();
    m_app->setLastMessage(text, cancelled ? QStringLiteral("info") : QStringLiteral("error"));
    emit finished(false, text);
}

void VocalRemoverController::finishSeparation(const QString &clipId, drift::TimeUs srcIn, drift::TimeUs srcOut,
                                              int output, const QString &vocalsPath, const QString &musicPath)
{
    drift::Project &project = m_app->m_project;
    int track = -1, index = -1;
    if (!findClip(project, clipId, &track, &index) || project.tracks().at(track).clips.at(index).srcIn != srcIn
        || project.tracks().at(track).clips.at(index).srcOut != srcOut) {
        // Deleted or trimmed while the job ran: the files no longer match the clip.
        failRun(tr("The clip changed while it was processed"), false, vocalsPath, musicPath);
        return;
    }

    const drift::Project before = project;
    // A video still playing its own sound gets that sound separated onto an audio track first;
    // the new files then replace the separated clip's media, and the picture is left untouched.
    if (project.tracks().at(track).clips.at(index).type == drift::ClipType::Video) {
        if (!drift::appcontroller_detail::detachEmbeddedAudioFromVideo(project, m_app->m_assetLibrary, track,
                                                                     index)) {
            project = before;
            failRun(tr("Could not separate the video's audio"), false, vocalsPath, musicPath);
            return;
        }
        // Separating inserts a track, which can shift indices: look the video up again by id.
        const drift::Clip *companion = nullptr;
        if (findClip(project, clipId, &track, &index))
            companion = audibleClip(project, project.tracks().at(track).clips.at(index));
        if (!companion || companion->type != drift::ClipType::Audio
            || !findClip(project, companion->id, &track, &index)) {
            project = before;
            failRun(tr("Could not separate the video's audio"), false, vocalsPath, musicPath);
            return;
        }
    }

    // Everything about the clip stays (timing, link, volume dots, fades, effects) except the
    // media: srcIn/srcOut rebase because the files cover exactly the old source window.
    const auto repoint = [srcIn, srcOut](drift::Clip &clip, const QString &path) {
        clip.path = path;
        clip.assetId.clear();
        clip.audioStreamIndex = 0;
        clip.thumbnailPath.clear();
        clip.filmstripPath.clear();
        clip.srcIn = 0;
        clip.srcOut = srcOut - srcIn;
    };

    QString done;
    int selectTrack = track;
    int selectIndex = index;
    drift::Clip &clip = project.tracks()[track].clips[index];
    if (output == VocalsOnly) {
        repoint(clip, vocalsPath);
        nameWith(clip, tr(" (vocals)"), tr("Clip"));
        done = tr("Vocals separated");
    } else if (output == MusicOnly) {
        repoint(clip, musicPath);
        nameWith(clip, tr(" (music)"), tr("Clip"));
        done = tr("Music separated");
    } else {
        // The clip keeps its place as the voice; the music is a copy of it on a new track under
        // it, in the same link group, so the two move, trim and delete together and stay in sync.
        if (clip.linkId.isEmpty())
            clip.linkId = QUuid::createUuid().toString(QUuid::WithoutBraces);
        drift::Clip music = clip;
        repoint(clip, vocalsPath);
        nameWith(clip, tr(" (vocals)"), tr("Clip"));
        music.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        repoint(music, musicPath);
        nameWith(music, tr(" (music)"), tr("Clip"));
        const int musicTrack = drift::insertTrackAboveForClipType(project, track + 1, drift::ClipType::Audio);
        project.tracks()[musicTrack].clips.append(music);
        done = tr("Vocals and music separated");
    }

    m_app->pushProjectEdit(before, tr("Vocal remover"));
    m_app->finishEdit(done);
    m_app->selectClip(selectTrack, selectIndex);
    m_progress = 1.0;
    emit progressChanged();
    m_status = done;
    emit statusChanged();
    m_app->setLastMessage(done, QStringLiteral("success"));
    emit finished(true, done);
}

#include "moc_VocalRemoverController.cpp"
