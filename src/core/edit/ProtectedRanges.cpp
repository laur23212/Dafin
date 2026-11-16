#include "core/edit/ProtectedRanges.h"

#include "core/project/Project.h"

#include <QCryptographicHash>
#include <QFile>

#include <algorithm>

namespace drift {

namespace {

QList<TimeRangeUs> merged(QList<TimeRangeUs> ranges)
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

// Parts of `span` inside the merged `cover`.
QList<TimeRangeUs> intersect(const TimeRangeUs &span, const QList<TimeRangeUs> &cover)
{
    QList<TimeRangeUs> out;
    for (const TimeRangeUs &c : cover) {
        const TimeUs a = qMax(span.startUs, c.startUs);
        const TimeUs b = qMin(span.endUs, c.endUs);
        if (b > a)
            out.append({a, b});
    }
    return out;
}

// Parts of `span` outside the merged `cover`.
QList<TimeRangeUs> subtract(const TimeRangeUs &span, const QList<TimeRangeUs> &cover)
{
    QList<TimeRangeUs> out;
    TimeUs cursor = span.startUs;
    for (const TimeRangeUs &c : cover) {
        if (c.endUs <= cursor)
            continue;
        if (c.startUs >= span.endUs)
            break;
        if (c.startUs > cursor)
            out.append({cursor, qMin(c.startUs, span.endUs)});
        cursor = qMax(cursor, c.endUs);
        if (cursor >= span.endUs)
            break;
    }
    if (cursor < span.endUs)
        out.append({cursor, span.endUs});
    return out;
}

} // namespace

QList<TimeRangeUs> sourceCoverage(const Project &project, const QString &assetId)
{
    QList<TimeRangeUs> spans;
    for (const Track &track : project.tracks()) {
        for (const Clip &clip : track.clips) {
            if (clip.assetId == assetId && clip.srcOut > clip.srcIn)
                spans.append({clip.srcIn, clip.srcOut});
        }
    }
    return merged(spans);
}

QList<ProtectedLoss> protectedLoss(const QList<ProtectedRange> &ranges, const Project &before,
                                   const Project &after)
{
    QList<ProtectedLoss> out;
    QHash<QString, QList<TimeRangeUs>> coverBefore;
    QHash<QString, QList<TimeRangeUs>> coverAfter;
    for (const ProtectedRange &range : ranges) {
        if (!range.isValid())
            continue;
        if (!coverBefore.contains(range.assetId)) {
            coverBefore.insert(range.assetId, sourceCoverage(before, range.assetId));
            coverAfter.insert(range.assetId, sourceCoverage(after, range.assetId));
        }
        const TimeRangeUs span{range.srcStartUs, range.srcEndUs};
        QList<TimeRangeUs> lost;
        for (const TimeRangeUs &played : intersect(span, coverBefore.value(range.assetId)))
            lost += subtract(played, coverAfter.value(range.assetId));
        // A frame or two of de-click ramp at a cut edge is not a lost warning; anything longer is.
        lost.erase(std::remove_if(lost.begin(), lost.end(),
                                  [](const TimeRangeUs &r) { return r.length() < kCutMinEdgeUs; }),
                   lost.end());
        if (!lost.isEmpty())
            out.append({range, lost});
    }
    return out;
}

QList<ProtectedRange> protectedOverlaps(const QList<ProtectedRange> &ranges, const QString &assetId,
                                        const QList<TimeRangeUs> &removedSource)
{
    QList<ProtectedRange> out;
    for (const ProtectedRange &range : ranges) {
        if (!range.isValid() || range.assetId != assetId)
            continue;
        for (const TimeRangeUs &r : removedSource) {
            if (qMin(r.endUs, range.srcEndUs) - qMax(r.startUs, range.srcStartUs) >= kCutMinEdgeUs) {
                out.append(range);
                break;
            }
        }
    }
    return out;
}

QString sampledContentHash(const QString &path, qint64 *sizeOut)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    const qint64 size = file.size();
    if (sizeOut)
        *sizeOut = size;
    constexpr qint64 kChunk = 1 << 20;
    QCryptographicHash hash(QCryptographicHash::Sha256);
    hash.addData(QByteArray::number(size));
    const qint64 offsets[] = {0, qMax<qint64>(0, size / 2 - kChunk / 2), qMax<qint64>(0, size - kChunk)};
    for (const qint64 offset : offsets) {
        if (!file.seek(offset))
            return {};
        hash.addData(file.read(kChunk));
    }
    return QString::fromLatin1(hash.result().toHex());
}

} // namespace drift
