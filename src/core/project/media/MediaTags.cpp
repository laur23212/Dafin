#include "core/project/media/MediaTags.h"

#include <QHash>
#include <QRegularExpression>
#include <QSet>

#include <algorithm>
#include <cmath>

namespace drift {

QString normalizeTag(const QString &tag)
{
    QString t = tag.toLower().simplified();
    // Punctuation and quotes at either end ("dog," / "“music”"), never inside ("rock & roll").
    while (!t.isEmpty() && !t.front().isLetterOrNumber())
        t.remove(0, 1);
    while (!t.isEmpty() && !t.back().isLetterOrNumber())
        t.chop(1);
    for (const QLatin1String article : {QLatin1String("a "), QLatin1String("an "), QLatin1String("the ")}) {
        if (t.startsWith(article) && t.size() > article.size()) {
            t.remove(0, article.size());
            break;
        }
    }
    if (t.size() > 40)
        t = t.left(40).trimmed();
    return t;
}

QStringList mergeTags(const QStringList &existing, const QStringList &added, int maxTags)
{
    QStringList out;
    QSet<QString> seen;
    const auto take = [&](const QStringList &list) {
        for (const QString &raw : list) {
            if (out.size() >= maxTags)
                return;
            const QString t = normalizeTag(raw);
            if (t.isEmpty() || seen.contains(t))
                continue;
            seen.insert(t);
            out.append(t);
        }
    };
    take(existing);
    take(added);
    return out;
}

QStringList withoutTag(const QStringList &tags, const QString &tag)
{
    const QString drop = normalizeTag(tag);
    QStringList out;
    for (const QString &t : tags) {
        if (normalizeTag(t) != drop)
            out.append(t);
    }
    return out;
}

bool mediaMatchesSearch(const QString &name, const QStringList &tags, const QString &query)
{
    static const QRegularExpression spaces(QStringLiteral("\\s+"));
    const QStringList words = query.toLower().split(spaces, Qt::SkipEmptyParts);
    if (words.isEmpty())
        return true;
    const QString lowerName = name.toLower();
    for (const QString &word : words) {
        bool found = lowerName.contains(word);
        for (int i = 0; !found && i < tags.size(); ++i)
            found = tags.at(i).toLower().contains(word);
        if (!found)
            return false;
    }
    return true;
}

QStringList pickTags(const QList<QList<LabelHit>> &samples, const TagPickOptions &options)
{
    struct Tally
    {
        QString label;
        int count = 0;
        double best = 0.0;
    };
    QHash<QString, Tally> tallies;
    for (const QList<LabelHit> &sample : samples) {
        // Once per sample: three people in one frame are one sighting of "person".
        QHash<QString, double> inSample;
        for (const LabelHit &hit : sample) {
            const QString label = normalizeTag(hit.label);
            if (label.isEmpty() || !(hit.score >= options.weakScore))
                continue;
            inSample[label] = std::max(inSample.value(label, 0.0), hit.score);
        }
        for (auto it = inSample.cbegin(); it != inSample.cend(); ++it) {
            Tally &t = tallies[it.key()];
            t.label = it.key();
            ++t.count;
            t.best = std::max(t.best, it.value());
        }
    }

    const int n = int(samples.size());
    const int steady = std::max(2, int(std::ceil(options.minShare * n)));
    QList<Tally> kept;
    for (const Tally &t : std::as_const(tallies)) {
        if (t.best >= options.strongScore || t.count >= steady)
            kept.append(t);
    }
    std::sort(kept.begin(), kept.end(), [](const Tally &a, const Tally &b) {
        if (a.count != b.count)
            return a.count > b.count;
        if (a.best != b.best)
            return a.best > b.best;
        return a.label < b.label;
    });
    QStringList out;
    for (const Tally &t : std::as_const(kept)) {
        if (out.size() >= options.maxTags)
            break;
        out.append(t.label);
    }
    return out;
}

} // namespace drift
