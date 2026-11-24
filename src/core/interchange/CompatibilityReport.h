#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QString>

#include <cstdint>
#include <utility>

// Dafin: what an importer did with each construct of another editor's project, kept next to the
// timeline it read (ImportedTimeline::report) so that "imported" never silently means "identical".
//
// The vocabulary follows Stage 21 Pass 5, PR-04. It is Dafin's own, not an external format:
//   preserved          carried across as the source states it
//   approximated       carried across, but through a lower-fidelity model (the entry says which)
//   omitted            present in the file and understood, left out by this importer
//   unsupported        a kind of construct this importer does not handle at all
//   missing_dependency something it needs (media, a texture…) was not found on this computer
//   unknown            something was seen that this importer does not understand
namespace drift::interchange {

enum class CompatStatus {
    Preserved,
    Approximated,
    Omitted,
    Unsupported,
    MissingDependency,
    Unknown,
};

inline QString compatStatusKey(CompatStatus status)
{
    switch (status) {
    case CompatStatus::Preserved:
        return QStringLiteral("preserved");
    case CompatStatus::Approximated:
        return QStringLiteral("approximated");
    case CompatStatus::Omitted:
        return QStringLiteral("omitted");
    case CompatStatus::Unsupported:
        return QStringLiteral("unsupported");
    case CompatStatus::MissingDependency:
        return QStringLiteral("missing_dependency");
    case CompatStatus::Unknown:
        return QStringLiteral("unknown");
    }
    return QStringLiteral("unknown");
}

struct CompatEntry
{
    CompatStatus status = CompatStatus::Unknown;
    // Machine key of the kind of construct: "media_clip", "attached_effect", "speed_curve",
    // "frame_rate", "non_media_clip", "other_timeline", "media_file", "empty_clip"…
    QString construct;
    // A stable pointer into the source file: "<timelineId>:<video|audio|trackType N>:<track>:<clip>"
    // (positions in the file, as the source states no id for a clip), or "timeline:<id>".
    QString sourceId;
    // The same in words for a message, and the part of the edit it is on.
    QString where;
    int64_t sourceTimeUs = -1; // where it starts on the source timeline; -1 when not known
    QString reason;            // why it is not "preserved"
    QString applied;           // what Dafin actually did instead
    QString hint;              // what the person can do about it
};

struct CompatibilityReport
{
    QList<CompatEntry> entries;
    // "preserved" entries are only kept up to kMaxPreservedEntries; the count is always exact.
    int preservedCount = 0;
    static constexpr int kMaxPreservedEntries = 2000;

    void add(CompatEntry entry)
    {
        if (entry.status == CompatStatus::Preserved) {
            ++preservedCount;
            if (preservedCount > kMaxPreservedEntries)
                return;
        }
        entries.append(std::move(entry));
    }

    int count(CompatStatus status) const
    {
        if (status == CompatStatus::Preserved)
            return preservedCount;
        int n = 0;
        for (const CompatEntry &e : entries)
            if (e.status == status)
                ++n;
        return n;
    }

    // True when anything is not carried across as the source states it. `unknown` counts: a
    // construct nobody understood cannot be called preserved.
    bool hasLoss() const
    {
        for (const CompatEntry &e : entries)
            if (e.status != CompatStatus::Preserved)
                return true;
        return false;
    }

    bool isEmpty() const { return entries.isEmpty() && preservedCount == 0; }

    // For diagnostics and tests; field names are this struct's, not a published schema.
    QJsonObject toJson() const
    {
        QJsonArray list;
        for (const CompatEntry &e : entries) {
            QJsonObject o;
            o.insert(QStringLiteral("status"), compatStatusKey(e.status));
            o.insert(QStringLiteral("construct"), e.construct);
            o.insert(QStringLiteral("sourceId"), e.sourceId);
            o.insert(QStringLiteral("where"), e.where);
            if (e.sourceTimeUs >= 0)
                o.insert(QStringLiteral("sourceTimeUs"), double(e.sourceTimeUs));
            o.insert(QStringLiteral("reason"), e.reason);
            o.insert(QStringLiteral("applied"), e.applied);
            o.insert(QStringLiteral("hint"), e.hint);
            list.append(o);
        }
        QJsonObject out;
        out.insert(QStringLiteral("preservedCount"), preservedCount);
        out.insert(QStringLiteral("entries"), list);
        return out;
    }
};

} // namespace drift::interchange
