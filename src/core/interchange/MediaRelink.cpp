#include "core/interchange/MediaRelink.h"

#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>

#include <algorithm>

namespace drift::interchange {

namespace {

constexpr int kMaxIndexed = 20000;
constexpr qint64 kHashSpan = 64 * 1024;

QString normalised(const QString &path)
{
    QString out = path;
    out.replace(QLatin1Char('\\'), QLatin1Char('/'));
    return out;
}

// Folders the two paths share, counted from the file outwards (the file name itself excluded).
int sharedTrailingFolders(const QString &stored, const QString &candidate)
{
    const QStringList a = normalised(stored).split(QLatin1Char('/'), Qt::SkipEmptyParts);
    const QStringList b = normalised(candidate).split(QLatin1Char('/'), Qt::SkipEmptyParts);
    int shared = 0;
    for (int i = int(a.size()) - 2, j = int(b.size()) - 2; i >= 0 && j >= 0; --i, --j, ++shared) {
        if (a.at(i).compare(b.at(j), Qt::CaseInsensitive) != 0)
            break;
    }
    return shared;
}

void rank(QList<MediaCandidate> *list)
{
    std::stable_sort(list->begin(), list->end(), [](const MediaCandidate &x, const MediaCandidate &y) {
        if (x.verifiedBy.size() != y.verifiedBy.size())
            return x.verifiedBy.size() > y.verifiedBy.size();
        if (x.sharedFolders != y.sharedFolders)
            return x.sharedFolders > y.sharedFolders;
        return x.path < y.path;
    });
}

} // namespace

QString relinkKey(const QString &storedPath)
{
    return normalised(storedPath);
}

QByteArray partialHashOf(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    const qint64 size = file.size();
    QCryptographicHash hash(QCryptographicHash::Sha256);
    hash.addData(QByteArray::number(size));
    const QByteArray head = file.read(kHashSpan);
    if (head.size() != qMin(size, kHashSpan))
        return {};
    hash.addData(head);
    if (size > kHashSpan) {
        if (!file.seek(qMax(kHashSpan, size - kHashSpan)))
            return {};
        const QByteArray tail = file.readAll();
        hash.addData(tail);
    }
    return hash.result().toHex();
}

MediaRelinker::MediaRelinker(const QString &projectFile, MediaProbeFn probe, CancelPtr cancel,
                             QHash<QString, QString> choices)
    : m_dir(QFileInfo(projectFile).absolutePath())
    , m_probe(std::move(probe))
    , m_cancel(std::move(cancel))
    , m_choices(std::move(choices))
{
}

void MediaRelinker::indexFolder()
{
    if (m_indexed || m_dir.isEmpty())
        return;
    // Plain files only, and no link is followed: a symlink is a second name for a file already
    // listed (or for one somewhere else), never a different candidate.
    QDirIterator it(m_dir, QDir::Files | QDir::NoDotAndDotDot | QDir::NoSymLinks, QDirIterator::Subdirectories);
    int seen = 0;
    while (it.hasNext()) {
        if ((seen & 255) == 0 && isCancelled(m_cancel))
            return; // left unindexed; find() reports the cancel
        if (seen >= kMaxIndexed) {
            m_truncated = true;
            break;
        }
        const QString path = it.next();
        ++seen;
        m_byName[it.fileName().toLower()].append(path);
    }
    for (QStringList &paths : m_byName)
        paths.sort();
    m_indexed = true;
}

MediaFacts MediaRelinker::factsOf(const QString &path)
{
    const auto it = m_facts.constFind(path);
    if (it != m_facts.constEnd())
        return *it;
    const MediaFacts facts = m_probe ? m_probe(path) : MediaFacts{};
    m_facts.insert(path, facts);
    return facts;
}

QByteArray MediaRelinker::hashOf(const QString &path)
{
    const auto it = m_hashes.constFind(path);
    if (it != m_hashes.constEnd())
        return *it;
    const QByteArray hash = partialHashOf(path);
    m_hashes.insert(path, hash);
    return hash;
}

RelinkOutcome MediaRelinker::find(const QString &stored, const QString &relative, const MediaNeed &need)
{
    RelinkOutcome out;
    const QString normalizedStored = normalised(stored);
    out.path = normalizedStored;

    if (!normalizedStored.isEmpty() && QFileInfo(normalizedStored).isFile()) {
        out.how = RelinkHow::AtStoredPath;
        out.path = QFileInfo(normalizedStored).absoluteFilePath();
        return out;
    }
    if (!relative.isEmpty()) {
        const QFileInfo info(QDir(m_dir).filePath(normalised(relative)));
        if (info.isFile()) {
            out.how = RelinkHow::RelativeToProject;
            out.path = info.absoluteFilePath();
            return out;
        }
    }

    const QString name = QFileInfo(normalizedStored).fileName();
    if (name.isEmpty())
        return out;
    indexFolder();
    if (isCancelled(m_cancel)) {
        out.cancelled = true;
        return out;
    }
    out.searchIncomplete = m_truncated;
    const QStringList sameName = m_byName.value(name.toLower());
    if (sameName.isEmpty())
        return out;

    // The person's answer to an earlier question, if there was one. It only counts when it names
    // one of the files this search finds.
    const QString key = relinkKey(stored);
    if (m_choices.contains(key)) {
        const QString chosen = m_choices.value(key);
        if (chosen.isEmpty()) {
            out.declined = true;
            return out;
        }
        const QString chosenCanonical = QFileInfo(chosen).canonicalFilePath();
        for (const QString &p : sameName) {
            if (QFileInfo(p).canonicalFilePath() == chosenCanonical) {
                out.how = RelinkHow::Chosen;
                out.path = chosenCanonical;
                return out;
            }
        }
    }

    // Drop what cannot be this clip's media.
    QList<MediaCandidate> kept;
    const int64_t slack = 200'000 + need.requiredDurationUs / 200;
    for (const QString &p : sameName) {
        if (isCancelled(m_cancel)) {
            out.cancelled = true;
            return out;
        }
        MediaCandidate c;
        c.path = QFileInfo(p).canonicalFilePath();
        c.size = QFileInfo(p).size();
        c.sharedFolders = sharedTrailingFolders(normalizedStored, c.path);
        bool reject = false;
        if (need.expectedSize >= 0) {
            if (c.size != need.expectedSize)
                reject = true;
            else
                c.verifiedBy << QStringLiteral("size");
        }
        if (!reject && need.requiredDurationUs > 0) {
            const MediaFacts facts = factsOf(c.path);
            c.durationUs = facts.durationUs;
            if (facts.known && facts.durationUs >= 0) {
                if (facts.durationUs + slack < need.requiredDurationUs)
                    reject = true;
                else
                    c.verifiedBy << QStringLiteral("duration");
            }
        }
        if (!reject && !need.expectedPartialHash.isEmpty()) {
            c.partialHash = hashOf(c.path);
            if (c.partialHash != need.expectedPartialHash)
                reject = true;
            else
                c.verifiedBy << QStringLiteral("hash");
        }
        if (reject)
            ++out.rejected;
        else
            kept.append(c);
    }

    if (kept.isEmpty())
        return out;
    if (kept.size() == 1) {
        out.how = RelinkHow::ByNameUnique;
        out.path = kept.first().path;
        out.verifiedBy = kept.first().verifiedBy;
        return out;
    }

    // Several fit. The same bytes in two places is one file; anything else is a question.
    bool identical = true;
    for (MediaCandidate &c : kept) {
        c.partialHash = hashOf(c.path);
        if (c.durationUs < 0 && m_probe)
            c.durationUs = factsOf(c.path).durationUs;
        if (c.partialHash.isEmpty() || c.size != kept.first().size || c.partialHash != kept.first().partialHash)
            identical = false;
    }
    rank(&kept);
    if (identical) {
        out.how = RelinkHow::ByNameIdenticalCopies;
        out.path = kept.first().path;
        out.verifiedBy = kept.first().verifiedBy;
        for (const char *what : {"size", "hash"}) {
            if (!out.verifiedBy.contains(QLatin1String(what)))
                out.verifiedBy << QLatin1String(what);
        }
        return out;
    }
    out.how = RelinkHow::Ambiguous;
    out.candidates = kept;
    return out;
}

} // namespace drift::interchange
