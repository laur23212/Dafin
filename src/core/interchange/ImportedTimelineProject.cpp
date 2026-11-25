#include "core/interchange/ImportedTimelineProject.h"
#include "core/interchange/MediaRelink.h"

#include "core/project/Track.h"
#include "core/project/clip/Clip.h"
#include "core/project/media/MediaAsset.h"

#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QHash>
#include <QSet>
#include <QUuid>

#include <cmath>

namespace drift::interchange {

namespace {

QString tr(const char *text, const char *disambiguation = nullptr, int n = -1)
{
    return QCoreApplication::translate("ProjectImport", text, disambiguation, n);
}

QString newId()
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

bool isAudioSuffix(const QString &path)
{
    static const QStringList audio{QStringLiteral("mp3"), QStringLiteral("wav"), QStringLiteral("aac"),
                                   QStringLiteral("m4a"), QStringLiteral("flac"), QStringLiteral("ogg"),
                                   QStringLiteral("oga"), QStringLiteral("opus"), QStringLiteral("wma"),
                                   QStringLiteral("aif"), QStringLiteral("aiff"), QStringLiteral("caf"),
                                   QStringLiteral("ac3"), QStringLiteral("amr")};
    return audio.contains(QFileInfo(path).suffix().toLower());
}

} // namespace

ProjectBuildResult toProject(const ImportedTimeline &timeline, const QString &projectFile,
                             const BuildOptions &options)
{
    ProjectBuildResult result;
    Project &project = result.project;
    project.setId(newId());
    project.setName(timeline.name.isEmpty() ? QFileInfo(projectFile).completeBaseName() : timeline.name);
    project.setResolution(qMax(16, timeline.width), qMax(16, timeline.height));
    project.setFps(projectFpsFor(timeline.fps));
    if (timeline.keptFpsNum > 0 && timeline.keptFpsDen > 1)
        project.setExactFrameRate(timeline.keptFpsNum, timeline.keptFpsDen);
    if (timeline.sampleRate >= 8000)
        project.setSampleRate(timeline.sampleRate);
    project.tracks().clear();

    MediaRelinker finder(projectFile, options.probe, options.cancel, options.relinkChoices);
    QHash<QString, MediaNeed> needs;
    QHash<QString, RelinkOutcome> outcomes;
    const auto needKey = [](const ImportedClip &c) { return relinkKey(c.path) + QLatin1Char('\x1f') + c.relativePath; };
    // What each stored file has to be: long enough for every clip that uses it, and the identity
    // the project recorded, if it recorded one.
    for (const ImportedTrack &in : timeline.tracks) {
        for (const ImportedClip &c : in.clips) {
            MediaNeed &need = needs[needKey(c)];
            if (!c.still) {
                const int64_t span = c.srcSpanUs > 0 ? c.srcSpanUs
                                                      : int64_t(std::llround(double(c.durationUs) * c.speed));
                need.requiredDurationUs = qMax(need.requiredDurationUs, c.srcInUs + span);
            }
            if (c.expectedSize >= 0 && need.expectedSize < 0)
                need.expectedSize = c.expectedSize;
            if (!c.expectedPartialHash.isEmpty() && need.expectedPartialHash.isEmpty())
                need.expectedPartialHash = c.expectedPartialHash;
        }
    }
    QHash<QString, QString> assetForPath;
    QSet<QString> missing;
    QSet<QString> asked;

    const auto sourceIdOf = [](const ImportedClip &c) {
        return c.sourceId.isEmpty() ? QStringLiteral("clip:%1").arg(c.name) : c.sourceId;
    };
    const auto assetFor = [&](const ImportedClip &c, bool audioTrack, QString *path) {
        const QString key = needKey(c);
        if (!outcomes.contains(key)) {
            outcomes.insert(key, finder.find(c.path, c.relativePath, needs.value(key)));
            const RelinkOutcome &first = outcomes[key];
            // Said once per file, whatever the number of clips that use it.
            CompatEntry entry;
            entry.sourceId = sourceIdOf(c);
            entry.where = c.name.isEmpty() ? QFileInfo(first.path).fileName() : c.name;
            entry.sourceTimeUs = c.startUs >= 0 ? c.startUs : -1;
            entry.construct = QStringLiteral("media_file");
            switch (first.how) {
            case RelinkHow::AtStoredPath:
            case RelinkHow::RelativeToProject:
                break;
            case RelinkHow::ByNameUnique:
            case RelinkHow::ByNameIdenticalCopies:
            case RelinkHow::Chosen:
                entry.construct = QStringLiteral("media_relinked");
                if (first.how == RelinkHow::Chosen || first.verified()) {
                    entry.status = CompatStatus::Preserved;
                    entry.applied = first.how == RelinkHow::Chosen
                        ? tr("Linked to %1, the file you chose.").arg(QDir::toNativeSeparators(first.path))
                        : tr("Linked to %1 (checked: %2).").arg(QDir::toNativeSeparators(first.path),
                                                                first.verifiedBy.join(QStringLiteral(", ")));
                } else {
                    entry.status = CompatStatus::Approximated;
                    entry.reason = tr("The media file is not where the project says; one file of the same name was found.");
                    entry.applied = tr("Linked to %1. Nothing could confirm it is the same file.")
                                        .arg(QDir::toNativeSeparators(first.path));
                    entry.hint = tr("Check that the clip shows the right footage.");
                }
                result.report.add(entry);
                break;
            case RelinkHow::Ambiguous:
            case RelinkHow::NotFound: {
                entry.status = CompatStatus::MissingDependency;
                if (first.how == RelinkHow::Ambiguous) {
                    entry.reason = tr("%n different files of this name fit and nothing tells them apart.", nullptr,
                                      int(first.candidates.size()));
                    entry.applied = tr("The clip was kept and shows as missing until you choose the file.");
                    entry.hint = tr("Choose the right file, or relink the clip yourself.");
                } else if (first.declined) {
                    entry.reason = tr("You chose to leave this clip unlinked.");
                    entry.applied = tr("The clip was kept and shows as missing.");
                    entry.hint = tr("Relink the clip when you have the file.");
                } else {
                    entry.reason = tr("The media file is not on this computer, nor beside the project file.");
                    if (first.rejected > 0)
                        entry.reason = tr("%n file(s) of this name were found beside the project, but none can be this clip's media (too short, or not the file the project used).",
                                          nullptr, first.rejected);
                    if (first.searchIncomplete)
                        entry.reason += QLatin1Char(' ') + tr("The folder is too big to search completely.");
                    entry.applied = tr("The clip was kept and shows as missing.");
                    entry.hint = tr("Relink the clip, or put the file back where the project expects it.");
                }
                result.report.add(entry);
                break;
            }
            }
            if (first.cancelled)
                result.cancelled = true;
            if (first.how == RelinkHow::Ambiguous && !asked.contains(relinkKey(c.path))) {
                asked.insert(relinkKey(c.path));
                ImportQuestion q;
                q.key = relinkKey(c.path);
                q.clipName = c.name.isEmpty() ? QFileInfo(first.path).fileName() : c.name;
                q.candidates = first.candidates;
                result.questions.append(q);
            }
        }
        const RelinkOutcome &outcome = outcomes[key];
        *path = outcome.path;
        if (!outcome.found())
            missing.insert(QDir::toNativeSeparators(*path));
        const auto it = assetForPath.constFind(*path);
        if (it != assetForPath.constEnd())
            return *it;
        MediaAsset asset;
        asset.id = newId();
        asset.path = *path;
        asset.name = QFileInfo(*path).fileName();
        // The file's own kind: the sound of a video file on an audio track is still a video asset.
        Q_UNUSED(audioTrack);
        asset.kind = c.still ? MediaKind::Image : isAudioSuffix(*path) ? MediaKind::Audio : MediaKind::Video;
        project.assets().insert(asset.id, asset);
        project.assetOrder().append(asset.id);
        assetForPath.insert(*path, asset.id);
        return asset.id;
    };

    const auto makeClip = [&](const ImportedClip &c, bool audioTrack) {
        Clip clip;
        clip.id = newId();
        QString path;
        clip.assetId = assetFor(c, audioTrack, &path);
        clip.path = path;
        clip.name = c.name.isEmpty() ? QFileInfo(path).fileName() : c.name;
        clip.type = audioTrack ? ClipType::Audio : (c.still ? ClipType::Image : ClipType::Video);
        clip.timelineStart = qMax<int64_t>(0, c.startUs);
        clip.timelineDuration = qMax<int64_t>(1000, c.durationUs);
        clip.speed = c.speed > 0 ? c.speed : 1.0;
        clip.reverse = c.reverse;
        if (c.still) {
            clip.srcIn = 0;
            clip.srcOut = clip.timelineDuration;
            clip.speed = 1.0;
        } else if (!c.speedPoints.isEmpty() && c.srcSpanUs > 0) {
            // A proven ramp: the source range is what the file says, the ramp decides how long it
            // takes, and the clip keeps the length the project gave it on the timeline.
            clip.speed = 1.0;
            clip.srcIn = qMax<int64_t>(0, c.srcInUs);
            clip.srcOut = clip.srcIn + c.srcSpanUs;
            clip.speedCurve.setPoints(c.speedPoints);
        } else {
            clip.srcIn = qMax<int64_t>(0, c.srcInUs);
            clip.srcOut = clip.srcIn + clip.sourceSpanUs();
        }
        if (clip.type == ClipType::Video)
            clip.suppressEmbeddedAudio = !c.keepsOwnAudio;
        ++result.clipCount;
        return clip;
    };

    for (const ImportedTrack &in : timeline.tracks) {
        if (result.cancelled || isCancelled(options.cancel)) {
            result.cancelled = true;
            return result;
        }
        if (in.audio) {
            Track track;
            track.type = TrackType::Audio;
            track.name = in.name;
            track.muted = in.muted;
            track.locked = in.locked;
            for (const ImportedClip &c : in.clips)
                track.clips.append(makeClip(c, true));
            project.tracks().append(track);
            continue;
        }
        Track pictures;
        pictures.type = TrackType::Shape;
        Track video;
        video.type = TrackType::Video;
        video.name = in.name;
        for (Track *t : {&pictures, &video}) {
            t->hidden = in.muted;
            t->locked = in.locked;
        }
        for (const ImportedClip &c : in.clips)
            (c.still ? pictures : video).clips.append(makeClip(c, false));
        if (!pictures.clips.isEmpty())
            project.tracks().append(pictures);
        if (!video.clips.isEmpty() || pictures.clips.isEmpty())
            project.tracks().append(video);
    }

    // Link each video clip with the audio clip that came with it: the same file at the same
    // place, within a frame.
    const int64_t frame = int64_t(1'000'000 / qMax(1, project.fps()));
    QSet<QString> linked;
    for (Track &vt : project.tracks()) {
        if (vt.type != TrackType::Video)
            continue;
        for (Clip &v : vt.clips) {
            for (Track &at : project.tracks()) {
                if (at.type != TrackType::Audio || !v.linkId.isEmpty())
                    continue;
                for (Clip &a : at.clips) {
                    if (linked.contains(a.id) || a.path != v.path || qAbs(a.timelineStart - v.timelineStart) > frame
                        || qAbs(a.timelineEnd() - v.timelineEnd()) > frame)
                        continue;
                    const QString link = newId();
                    v.linkId = link;
                    a.linkId = link;
                    v.suppressEmbeddedAudio = true;
                    linked.insert(a.id);
                    break;
                }
            }
        }
    }

    if (project.tracks().isEmpty())
        project.resetToDefaultTimeline();
    project.ensureTrackIds();
    result.missingMedia = QStringList(missing.cbegin(), missing.cend());
    result.missingMedia.sort(Qt::CaseInsensitive);
    return result;
}

QString summary(const ImportedTimeline &timeline, const ProjectBuildResult &result, const QString &appName)
{
    QStringList parts;
    parts.append(tr("%1 project imported: %n clip(s).", nullptr, result.clipCount).arg(appName));
    if (timeline.skippedClips > 0)
        parts.append(tr("Left out %n item(s) Dafin can't bring over (%1).", nullptr, timeline.skippedClips)
                         .arg(timeline.skippedKinds.join(QStringLiteral(", "))));
    if (!result.missingMedia.isEmpty())
        parts.append(tr("%n media file(s) were not found and show as missing.", nullptr,
                        int(result.missingMedia.size())));
    // What the reader says it did not bring across faithfully (the Filmora reader fills this in).
    int effectClips = 0;
    int otherTimelines = 0;
    int emptyClips = 0;
    int unrecognized = 0;
    QStringList rateNotes;
    for (const CompatEntry &e : timeline.report.entries) {
        if (e.construct == QLatin1String("attached_effect"))
            ++effectClips;
        else if (e.construct == QLatin1String("other_timeline"))
            ++otherTimelines;
        else if (e.construct == QLatin1String("empty_clip"))
            ++emptyClips;
        else if (e.status == CompatStatus::Unknown && e.construct != QLatin1String("frame_rate"))
            ++unrecognized;
        else if (e.construct == QLatin1String("frame_rate") && e.status != CompatStatus::Preserved)
            rateNotes.append(e.applied);
    }
    if (effectClips > 0)
        parts.append(tr("%n clip(s) came without the effects attached to them in the original.", nullptr, effectClips));
    if (otherTimelines > 0)
        parts.append(tr("%n other timeline(s) in the project were not imported.", nullptr, otherTimelines));
    if (emptyClips > 0)
        parts.append(tr("%n clip(s) with no length or invalid times were left out.", nullptr, emptyClips));
    if (unrecognized > 0)
        parts.append(tr("%n item(s) were in a layout Dafin does not recognise.", nullptr, unrecognized));
    parts.append(rateNotes);
    if (timeline.keptFpsNum > 0)
        parts.append(tr("Frame rate kept at %1/%2 for export; the editing grid is %3 fps.")
                         .arg(timeline.keptFpsNum).arg(timeline.keptFpsDen).arg(result.project.fps()));
    parts.append(tr("Effects, transitions, titles and keyframes stay behind."));
    for (const QString &note : timeline.notes)
        parts.append(note);
    return parts.join(QLatin1Char(' '));
}

} // namespace drift::interchange
