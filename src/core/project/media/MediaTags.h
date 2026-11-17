#pragma once

// Dafin: the pure half of AI › Media auto-tagging. Tags live on MediaAsset::tags; these helpers
// clean them, merge new ones in, decide which detector labels are worth keeping, and match them
// against the media bin search. No models and no project here, so all of it is tested directly
// (tests/tst_aishorts.cpp); the job that runs the models is app/jobs/ai/MediaTaggingController.

#include <QList>
#include <QString>
#include <QStringList>

namespace drift {

// Most tags a media item keeps. More would turn the search into "everything matches".
inline constexpr int kMaxMediaTags = 12;

// Lowercase, single spaces, no leading "a " / "an " / "the ", no surrounding punctuation, at most
// 40 characters. Empty when nothing is left.
QString normalizeTag(const QString &tag);

// `existing` followed by whatever of `added` is new, every entry normalized, duplicates and empty
// ones dropped, at most `maxTags`. Order is kept: the first tags are the most confident.
QStringList mergeTags(const QStringList &existing, const QStringList &added, int maxTags = kMaxMediaTags);

// `tags` without `tag` (compared after normalizing).
QStringList withoutTag(const QStringList &tags, const QString &tag);

// The media bin search. Every word of `query` has to appear in the name or in one of the tags
// (case-insensitive, substring), so "dog beach" finds a clip tagged dog and beach. An empty or
// blank query matches everything.
bool mediaMatchesSearch(const QString &name, const QStringList &tags, const QString &query);

// One label a detector reported for one sampled frame or audio excerpt.
struct LabelHit
{
    QString label;
    double score = 0.0; // 0..1
};

struct TagPickOptions
{
    double strongScore = 0.60; // seen once this clearly is enough
    double weakScore = 0.35;   // below this a hit does not count at all
    double minShare = 0.34;    // a weak label must be in at least this share of the samples (and in 2)
    int maxTags = 6;
};

// Which labels to keep from the hits of several samples (`samples[i]` = everything found in sample
// i). A label counts once per sample. Kept when it was strong anywhere, or weak but steady across
// the samples. Most frequent first, then most confident, then alphabetical; normalized.
QStringList pickTags(const QList<QList<LabelHit>> &samples, const TagPickOptions &options = {});

} // namespace drift
