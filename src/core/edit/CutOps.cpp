#include "core/edit/CutOps.h"

#include "core/project/Project.h"
#include "core/edit/TimelineOps.h"
#include "core/project/TranscriptEdit.h"

#include <QHash>
#include <QSet>
#include <QStringList>

#include <algorithm>
#include <limits>

namespace drift {

QList<TimeRangeUs> normalizedRanges(QList<TimeRangeUs> ranges)
{
    std::sort(ranges.begin(), ranges.end(),
              [](const TimeRangeUs &a, const TimeRangeUs &b) { return a.startUs < b.startUs; });
    QList<TimeRangeUs> out;
    for (const TimeRangeUs &r : std::as_const(ranges)) {
        if (r.endUs <= r.startUs)
            continue;
        if (!out.isEmpty() && r.startUs <= out.last().endUs)
            out.last().endUs = qMax(out.last().endUs, r.endUs);
        else
            out.append(r);
    }
    return out;
}

QList<TimeRangeUs> keptTimelineIntervals(const Clip &clip, const QList<TimeRangeUs> &removed,
                                         TimeUs minKeepUs)
{
    QList<TimeRangeUs> clipped;
    for (const TimeRangeUs &r : removed) {
        const TimeRangeUs c{qMax(r.startUs, clip.timelineStart), qMin(r.endUs, clip.timelineEnd())};
        if (c.endUs > c.startUs)
            clipped.append(c);
    }
    QList<TimeRangeUs> kept;
    TimeUs cursor = clip.timelineStart;
    for (const TimeRangeUs &r : normalizedRanges(clipped)) {
        if (r.startUs - cursor >= qMax<TimeUs>(1, minKeepUs))
            kept.append({cursor, r.startUs});
        cursor = qMax(cursor, r.endUs);
    }
    if (clip.timelineEnd() - cursor >= qMax<TimeUs>(1, minKeepUs))
        kept.append({cursor, clip.timelineEnd()});
    return kept;
}

bool sliceClipExact(const Clip &src, TimeUs from, TimeUs to, Clip &out)
{
    from = qMax(from, src.timelineStart);
    to = qMin(to, src.timelineEnd());
    if (to - from < kCutMinEdgeUs)
        return false;
    Clip work = src;
    if (from > work.timelineStart) {
        Clip tail;
        if (!splitClipAtOffsetMin(work, tail, from - work.timelineStart, kCutMinEdgeUs))
            return false;
        work = tail;
    }
    if (work.timelineEnd() > to) {
        Clip discarded;
        if (!splitClipAtOffsetMin(work, discarded, to - work.timelineStart, kCutMinEdgeUs))
            return false;
    }
    out = work;
    return true;
}

namespace {

// Outer edges of the whole group behave like a trim: they keep the clip's fades. An edge the cut
// made gets the de-click ramp instead of a visual fade.
void applyEdgeFades(QList<Clip> &segments, const Clip &src, TimeUs declickUs, bool startCut, bool endCut)
{
    for (int i = 0; i < segments.size(); ++i) {
        Clip &seg = segments[i];
        const bool first = i == 0;
        const bool last = i == segments.size() - 1;
        seg.fadeInUs = first ? qMin(src.fadeInUs, seg.timelineDuration) : 0;
        seg.animIn = first ? src.animIn : ClipAnimation{};
        seg.fadeOutUs = last ? qMin(src.fadeOutUs, seg.timelineDuration) : 0;
        seg.animOut = last ? src.animOut : ClipAnimation{};
        seg.audioFadeInUs = first && !startCut ? src.audioFadeInUs : qMax(declickUs, first ? src.audioFadeInUs : 0);
        seg.audioFadeOutUs = last && !endCut ? src.audioFadeOutUs : qMax(declickUs, last ? src.audioFadeOutUs : 0);
    }
}

} // namespace

QList<Clip> packedSegments(const Clip &src, const QList<TimeRangeUs> &kept, TimeUs declickUs)
{
    QList<Clip> out;
    TimeUs cursor = src.timelineStart;
    for (const TimeRangeUs &range : kept) {
        Clip seg;
        if (!sliceClipExact(src, range.startUs, range.endUs, seg))
            continue;
        seg.timelineStart = cursor;
        cursor += seg.timelineDuration;
        out.append(seg);
    }
    if (out.isEmpty())
        return out;
    const bool startCut = kept.first().startUs > src.timelineStart;
    const bool endCut = kept.last().endUs < src.timelineEnd();
    applyEdgeFades(out, src, declickUs, startCut, endCut);
    return out;
}

QList<Clip> segmentsFromSourceRanges(const Clip &src, QList<TimeRangeUs> sourceRanges,
                                     TimeUs mediaDurationUs, TimeUs padUs, TimeUs declickUs,
                                     QString *error)
{
    if (src.hasSpeedCurve()) {
        if (error)
            *error = QStringLiteral("clip has a speed ramp; flatten it first");
        return {};
    }
    const TimeUs maxUs = mediaDurationUs > 0 ? mediaDurationUs : std::numeric_limits<TimeUs>::max();
    QList<TimeRangeUs> ranges;
    for (TimeRangeUs r : std::as_const(sourceRanges)) {
        r.startUs = qBound<TimeUs>(0, r.startUs - padUs, maxUs);
        r.endUs = qBound<TimeUs>(0, r.endUs + padUs, maxUs);
        if (r.length() < kCutMinEdgeUs)
            continue;
        // Consecutive ranges that touch play as one: no cut, so no de-click either.
        if (!ranges.isEmpty() && r.startUs <= ranges.last().endUs && r.startUs >= ranges.last().startUs)
            ranges.last().endUs = qMax(ranges.last().endUs, r.endUs);
        else
            ranges.append(r);
    }
    if (ranges.isEmpty()) {
        if (error)
            *error = QStringLiteral("no usable ranges");
        return {};
    }

    const double speed = src.effectiveSpeed();
    QList<Clip> out;
    TimeUs cursor = src.timelineStart;
    for (const TimeRangeUs &r : std::as_const(ranges)) {
        Clip seg = src;
        seg.srcIn = r.startUs;
        seg.srcOut = r.endUs;
        seg.timelineStart = cursor;
        seg.timelineDuration = qMax<TimeUs>(1, static_cast<TimeUs>(llround(r.length() / speed)));
        // Where this range's first played frame sat on src's own clock, so its curves come along.
        const TimeUs playStartSource = src.reverse ? r.endUs : r.startUs;
        const TimeUs localOffset = static_cast<TimeUs>(llround(
            (src.reverse ? src.srcOut - playStartSource : playStartSource - src.srcIn) / speed));
        shiftClipKeyframes(seg, -localOffset);
        cursor += seg.timelineDuration;
        out.append(seg);
    }
    const bool startCut = src.reverse ? ranges.first().endUs != src.srcOut : ranges.first().startUs != src.srcIn;
    const bool endCut = src.reverse ? ranges.last().startUs != src.srcIn : ranges.last().endUs != src.srcOut;
    applyEdgeFades(out, src, declickUs, startCut, endCut);
    return out;
}

bool sourceRangeToTimeline(const Clip &clip, TimeRangeUs source, TimeRangeUs &timelineOut)
{
    const TimeUs from = qMax(source.startUs, clip.srcIn);
    const TimeUs to = qMin(source.endUs, clip.srcOut);
    if (to <= from)
        return false;
    const TimeUs a = clip.timelineStart + clip.sourceUsToClipLocalUs(from);
    const TimeUs b = clip.timelineStart + clip.sourceUsToClipLocalUs(to);
    timelineOut = {qMin(a, b), qMax(a, b)};
    return timelineOut.endUs > timelineOut.startUs;
}

QList<TimelineWord> transcriptWordsOnTimeline(const Project &project, TimeUs viewStart, TimeUs viewEnd,
                                              const QString &onlyClipId)
{
    QList<TimelineWord> out;
    QSet<QString> seen;
    for (const Track &track : project.tracks()) {
        if (onlyClipId.isEmpty() && track.muted)
            continue;
        for (const Clip &clip : track.clips) {
            if (!onlyClipId.isEmpty() && clip.id != onlyClipId)
                continue;
            if (clip.assetId.isEmpty() || clip.timelineEnd() <= viewStart || clip.timelineStart >= viewEnd)
                continue;
            if (onlyClipId.isEmpty() && clip.suppressEmbeddedAudio && track.type != TrackType::Audio)
                continue;
            const TranscriptPtr t = project.transcript(clip.assetId);
            if (!t)
                continue;
            // A linked pair is one sound; read it once.
            const QString key = clip.assetId + QLatin1Char('@') + QString::number(clip.timelineStart)
                                + QLatin1Char(':') + QString::number(clip.srcIn);
            if (seen.contains(key))
                continue;
            seen.insert(key);
            for (const int i : t->wordsInRange(clip.srcIn, clip.srcOut)) {
                const TranscriptWord &w = t->words.at(i);
                TimeRangeUs tl;
                if (!sourceRangeToTimeline(clip, {w.startUs, qMax(w.endUs, w.startUs + 1)}, tl))
                    continue;
                if (tl.endUs <= viewStart || tl.startUs >= viewEnd)
                    continue;
                TimelineWord tw{w, i, clip.assetId, clip.id};
                tw.word.startUs = tl.startUs;
                tw.word.endUs = tl.endUs;
                out.append(tw);
            }
        }
    }
    std::stable_sort(out.begin(), out.end(),
                     [](const TimelineWord &a, const TimelineWord &b) { return a.word.startUs < b.word.startUs; });
    return out;
}

QList<TimeRangeUs> sourceCutsForWords(const Transcript &transcript, const Clip &clip,
                                      const QList<int> &wordIndexes, TimeUs padUs)
{
    const TimeUs lo = qMin(clip.srcIn, clip.srcOut);
    const TimeUs hi = qMax(clip.srcIn, clip.srcOut);
    // Speech tokens in order; runs are positions in this list, as in cut_words.
    QList<int> speech;
    QHash<int, int> position;
    for (int i = 0; i < transcript.words.size(); ++i) {
        if (isSpeechToken(transcript.words.at(i))) {
            position.insert(i, int(speech.size()));
            speech.append(i);
        }
    }
    QList<int> positions;
    for (const int i : wordIndexes) {
        const auto it = position.constFind(i);
        if (it == position.constEnd())
            continue;
        const TranscriptWord &w = transcript.words.at(i);
        if (w.endUs <= lo || w.startUs >= hi)
            continue;
        positions.append(*it);
    }
    std::sort(positions.begin(), positions.end());
    positions.erase(std::unique(positions.begin(), positions.end()), positions.end());

    QList<QPair<int, int>> runs;
    for (const int p : std::as_const(positions)) {
        if (!runs.isEmpty() && runs.last().second + 1 == p)
            runs.last().second = p;
        else
            runs.append({p, p});
    }

    QList<TimeRangeUs> out;
    for (const auto &[a, b] : std::as_const(runs)) {
        const TranscriptWord &first = transcript.words.at(speech.at(a));
        const TranscriptWord &last = transcript.words.at(speech.at(b));
        const TimeUs L0 = a > 0 ? qMax(lo, transcript.words.at(speech.at(a - 1)).endUs) : lo;
        const TimeUs L1 = qMax(L0, first.startUs);
        const TimeUs R0 = qMax(L1, qMin(hi, last.endUs));
        const TimeUs R1 = b + 1 < speech.size() ? qMax(R0, qMin(hi, transcript.words.at(speech.at(b + 1)).startUs))
                                                : hi;
        // Take the pauses around the removed words too, leaving `pad` of air beside the words
        // that stay (less when the gap is shorter than that).
        const TimeUs cutS = L0 <= lo ? lo : qMin(L0 + qMin(padUs, (L1 - L0) / 2), L1);
        const TimeUs cutE = R1 >= hi ? hi : qMax(R1 - qMin(padUs, (R1 - R0) / 2), R0);
        if (cutE > cutS)
            out.append({cutS, cutE});
    }
    return normalizedRanges(out);
}

TimeUs timeAfterCut(const QList<TimeRangeUs> &removed, TimeUs t)
{
    TimeUs shift = 0;
    for (const TimeRangeUs &r : normalizedRanges(removed)) {
        if (t <= r.startUs)
            break;
        if (t < r.endUs)
            return r.startUs - shift;
        shift += r.length();
    }
    return t - shift;
}

namespace {

// The parts of [a, b) that `removed` (normalized) does not cover, in time order.
QList<TimeRangeUs> keptPieces(TimeUs a, TimeUs b, const QList<TimeRangeUs> &removed)
{
    QList<TimeRangeUs> out;
    TimeUs cursor = a;
    for (const TimeRangeUs &r : removed) {
        if (r.endUs <= cursor)
            continue;
        if (r.startUs >= b)
            break;
        if (r.startUs > cursor)
            out.append({cursor, r.startUs});
        cursor = qMax(cursor, r.endUs);
    }
    if (b > cursor)
        out.append({cursor, b});
    return out;
}

} // namespace

bool captionsFollowCut(Project &project, const Clip &before, const QList<TimeRangeUs> &removedIn,
                       const Transcript *transcript)
{
    const QList<TimeRangeUs> removed = normalizedRanges(removedIn);
    const QString key = subtitleSourceKey(before);
    if (removed.isEmpty() || key.isEmpty())
        return false;

    bool anyChanged = false;
    for (Track &track : project.tracks()) {
        for (int i = int(track.clips.size()) - 1; i >= 0; --i) {
            const Clip &sub = track.clips.at(i);
            if (sub.type != ClipType::Subtitle)
                continue;
            bool ours = false;
            for (const SubtitleCue &cue : sub.subtitleCues)
                ours = ours || cue.source == key;
            if (!ours)
                continue;

            bool changed = false;
            QList<SubtitleCue> cues;
            for (SubtitleCue cue : sub.subtitleCues) {
                TimeUs a = sub.timelineStart + cue.startUs;
                TimeUs b = sub.timelineStart + cue.endUs;
                if (cue.source != key) {
                    cue.startUs = a;
                    cue.endUs = b;
                    cues.append(cue);
                    continue;
                }
                const QList<TimeRangeUs> pieces = keptPieces(a, b, removed);
                const bool lostTime = pieces.size() != 1 || pieces.first().startUs != a || pieces.first().endUs != b;
                const bool overClip = a >= before.timelineStart && b <= before.timelineEnd();
                if (lostTime && overClip && transcript) {
                    QStringList said;
                    for (const TimeRangeUs &piece : pieces) {
                        TimeUs s0 = before.timelineToSourceUs(piece.startUs);
                        TimeUs s1 = before.timelineToSourceUs(piece.endUs);
                        if (s1 < s0)
                            std::swap(s0, s1);
                        const QString text = transcriptTextInWindow(*transcript, s0, s1);
                        if (!text.isEmpty())
                            said.append(text);
                    }
                    const QString text = said.join(QLatin1Char(' '));
                    if (text != cue.text) {
                        cue.text = text;
                        changed = true;
                    }
                }
                const TimeUs na = timeAfterCut(removed, a);
                const TimeUs nb = timeAfterCut(removed, b);
                if (na != a || nb != b)
                    changed = true;
                cue.startUs = na;
                cue.endUs = nb;
                if (cue.endUs <= cue.startUs || (lostTime && overClip && cue.text.trimmed().isEmpty())) {
                    changed = true;
                    continue;
                }
                cues.append(cue);
            }
            if (!changed)
                continue;
            anyChanged = true;

            if (cues.isEmpty()) {
                track.clips.removeAt(i);
                continue;
            }
            TimeUs start = timeAfterCut(removed, sub.timelineStart);
            TimeUs end = timeAfterCut(removed, sub.timelineEnd());
            for (const SubtitleCue &cue : std::as_const(cues)) {
                start = qMin(start, cue.startUs);
                end = qMax(end, cue.endUs);
            }
            QList<SubtitleCue> rebased;
            for (SubtitleCue cue : std::as_const(cues)) {
                cue.startUs -= start;
                cue.endUs -= start;
                rebased.append(cue);
            }
            sortSubtitleCues(rebased);

            Clip &out = track.clips[i];
            // A clip still carrying its generated name ("Subtitles (12)") keeps counting its cues.
            if (out.name == subtitleClipName(out.subtitleCues))
                out.name = subtitleClipName(rebased);
            out.timelineStart = start;
            out.timelineDuration = qMax<TimeUs>(1, end - start);
            out.srcIn = 0;
            out.srcOut = out.timelineDuration;
            out.subtitleCues = rebased;
        }
    }
    return anyChanged;
}

} // namespace drift
