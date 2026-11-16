#pragma once

#include "core/edit/CutOps.h"
#include "core/project/ProtectedRange.h"

#include <QList>
#include <QString>

namespace drift {

class Project;

// Dafin: the checks behind protected ranges. Pure functions over projects so they are testable
// without the editor and shared by every cutting op.

// Merged, sorted source ranges of `assetId` that some clip on the project's current timeline
// plays (each clip's [srcIn, srcOut], whatever its speed or direction).
QList<TimeRangeUs> sourceCoverage(const Project &project, const QString &assetId);

struct ProtectedLoss
{
    ProtectedRange range;     // the protected range that is affected
    QList<TimeRangeUs> lost;  // the parts of it that played before the edit and no longer play
};

// Protected footage that `before` played and `after` no longer plays. Footage that was already
// off the timeline before the edit is not counted: protection stops cuts, it does not demand
// that a range be placed.
QList<ProtectedLoss> protectedLoss(const QList<ProtectedRange> &ranges, const Project &before,
                                   const Project &after);

// Protected ranges of `assetId` that overlap any of `removedSource` (source microseconds). Used
// for a planned cut before anything is edited, so dry runs report the same refusal.
QList<ProtectedRange> protectedOverlaps(const QList<ProtectedRange> &ranges, const QString &assetId,
                                        const QList<TimeRangeUs> &removedSource);

// SHA-256 (hex) of the file size plus its first, middle and last MiB. Identifies a file by
// content, not by name or path, without reading a whole multi-GB recording; two different
// recordings of the same length differ in those samples. Empty when the file can't be read.
QString sampledContentHash(const QString &path, qint64 *sizeOut = nullptr);

} // namespace drift
