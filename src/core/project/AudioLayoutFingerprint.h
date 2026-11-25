#pragma once

#include "core/project/Project.h"

#include <QByteArray>
#include <QCryptographicHash>
#include <QString>

namespace drift {

// Everything the AudioMixer reads, and nothing else — so a transform keyframe edit does not
// invalidate a beat grid while moving, trimming, retiming or muting the music does. A cached beat
// grid, onset list or waveform is only reusable while this digest is unchanged.
inline QByteArray audioLayoutFingerprint(const Project &project)
{
    QCryptographicHash hash(QCryptographicHash::Sha1);
    for (const Track &track : project.tracks()) {
        if (track.type != TrackType::Audio && track.type != TrackType::Video)
            continue;
        hash.addData(track.muted ? "m" : "-");
        for (const Clip &clip : track.clips) {
            const QString row = QStringLiteral("%1|%2|%3|%4|%5|%6|%7|%8|%9|%10")
                                    .arg(clip.assetId)
                                    .arg(clip.timelineStart)
                                    .arg(clip.timelineDuration)
                                    .arg(clip.srcIn)
                                    .arg(clip.srcOut)
                                    .arg(clip.speed)
                                    .arg(clip.reverse ? 1 : 0)
                                    .arg(clip.suppressEmbeddedAudio ? 1 : 0)
                                    .arg(clip.audioEffects.size())
                                    .arg(clip.pan);
            hash.addData(row.toUtf8());
            // The mixer reads this file and this stream of it; a relink or a language switch moves
            // the audio under an unchanged asset id. Added on its own: a path may hold "%1".
            hash.addData("|");
            hash.addData(clip.path.toUtf8());
            hash.addData("|");
            hash.addData(QByteArray::number(clip.audioStreamIndex));
            // Tangents shape the volume ramp, so they belong in the digest alongside the values.
            for (const auto &kv : clip.volume.keyframes().asKeyValueRange()) {
                hash.addData(QStringLiteral("v%1:%2:%3:%4:%5:%6:%7")
                                 .arg(kv.first)
                                 .arg(kv.second.value)
                                 .arg(kv.second.inDx)
                                 .arg(kv.second.inDy)
                                 .arg(kv.second.outDx)
                                 .arg(kv.second.outDy)
                                 .arg(kv.second.hold ? 1 : 0)
                                 .toUtf8());
            }
            // The shape of a ramp decides where every beat inside the clip lands, and two ramps can
            // take the same time over the same source (0.5x to 2x and 2x to 0.5x), so the clip's
            // length cannot stand in for it.
            if (clip.hasSpeedCurve()) {
                hash.addData("c");
                for (const SpeedPoint &point : clip.speedCurve.points()) {
                    hash.addData(QStringLiteral("s%1:%2:%3:%4:%5:%6:%7")
                                     .arg(point.pos)
                                     .arg(point.speed)
                                     .arg(point.inDx)
                                     .arg(point.inDy)
                                     .arg(point.outDx)
                                     .arg(point.outDy)
                                     .arg(point.corner ? 1 : 0)
                                     .toUtf8());
                }
            }
        }
    }
    return hash.result();
}

} // namespace drift
