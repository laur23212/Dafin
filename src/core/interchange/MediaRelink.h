#pragma once

#include "core/interchange/CancelToken.h"

#include <QByteArray>
#include <QHash>
#include <QList>
#include <QSize>
#include <QString>
#include <QStringList>

#include <cstdint>
#include <functional>

// Dafin (C20 / D-005): finding the media file of an imported project that moved.
//
// The order is: the path as the project stores it, the path relative to the project file, then a
// file of the same name in the project's folder tree. The last step used to take whichever file
// the folder walk met first. It now looks at every file of that name and decides on what the file
// is, not where the walk found it:
//
//   * a file that cannot be what the clip needs is dropped: shorter than the part of the source the
//     clip uses, or a size / partial hash that is not the one the project recorded (when it
//     recorded one);
//   * one file left: taken, and said to be verified only when something was actually checked;
//   * several left that are byte-for-byte the same (size and a hash of the first and last 64 KiB):
//     the same footage in two places, not an ambiguity; the one whose folder looks most like the
//     stored path is taken, then the path in alphabetical order, so the pick never depends on the
//     order the disk lists files in;
//   * several different ones left: ambiguous. Nothing is chosen. The caller asks the person (the
//     candidates come ranked, with their facts) and calls again with the answer.
namespace drift::interchange {

// What a media probe knows about a file. `known` is false when it could not be read.
struct MediaFacts
{
    bool known = false;
    int64_t durationUs = -1; // -1 for pictures and when unknown
    QSize size;              // picture size, when it has one
};

// Supplied by the caller (the app wraps MediaProbe; tests give a table). Called on the import's
// worker thread; may be slow.
using MediaProbeFn = std::function<MediaFacts(const QString &path)>;

// What a clip asks of its file, and what the project recorded about it, if anything.
struct MediaNeed
{
    int64_t requiredDurationUs = 0; // the end of the part of the source the clip uses; 0 = no check
    qint64 expectedSize = -1;       // bytes, when the project recorded it
    QByteArray expectedPartialHash; // partialHashOf() of the original, when recorded
};

struct MediaCandidate
{
    QString path;
    qint64 size = -1;
    int64_t durationUs = -1;
    QByteArray partialHash;
    QStringList verifiedBy; // "size", "duration", "hash": what was checked and matched
    int sharedFolders = 0;  // trailing folders in common with the stored path (ranking only)
};

enum class RelinkHow {
    NotFound,
    AtStoredPath,
    RelativeToProject,
    ByNameUnique,
    ByNameIdenticalCopies,
    Chosen,    // the person picked it
    Ambiguous, // several different files fit: nothing chosen
};

struct RelinkOutcome
{
    RelinkHow how = RelinkHow::NotFound;
    QString path;                     // the file; for NotFound / Ambiguous the stored path, normalised
    QStringList verifiedBy;           // what was checked on the chosen file
    QList<MediaCandidate> candidates; // Ambiguous: the files to choose from, best first
    int rejected = 0;                 // same-name files dropped because they cannot be the clip's media
    bool declined = false;            // the person chose to leave the clip unlinked
    bool cancelled = false;
    bool searchIncomplete = false;    // the folder was too big to look through completely

    bool found() const { return how != RelinkHow::NotFound && how != RelinkHow::Ambiguous; }
    bool verified() const { return !verifiedBy.isEmpty(); }
};

// The key a person's answer is stored under: the stored path with forward slashes.
QString relinkKey(const QString &storedPath);

// SHA-256 over the size and the first and last 64 KiB of the file; empty when unreadable.
QByteArray partialHashOf(const QString &path);

class MediaRelinker
{
public:
    // `choices`: relinkKey() → the file the person chose (empty string = leave it unlinked).
    explicit MediaRelinker(const QString &projectFile, MediaProbeFn probe = {}, CancelPtr cancel = {},
                           QHash<QString, QString> choices = {});

    RelinkOutcome find(const QString &stored, const QString &relative, const MediaNeed &need);

private:
    void indexFolder();
    MediaFacts factsOf(const QString &path);
    QByteArray hashOf(const QString &path);

    QString m_dir;
    MediaProbeFn m_probe;
    CancelPtr m_cancel;
    QHash<QString, QString> m_choices;
    bool m_indexed = false;
    bool m_truncated = false;
    QHash<QString, QStringList> m_byName; // lower-case file name → paths, sorted
    QHash<QString, MediaFacts> m_facts;
    QHash<QString, QByteArray> m_hashes;
};

} // namespace drift::interchange
