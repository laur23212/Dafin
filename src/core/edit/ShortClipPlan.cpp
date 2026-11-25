#include "core/edit/ShortClipPlan.h"

#include "core/project/Transcript.h"
#include "core/project/clip/Clip.h"

#include <QRegularExpression>
#include <QSet>

#include <algorithm>
#include <cmath>

namespace drift::shorts {

namespace {

QString stripClosers(const QString &word)
{
    QString w = word.trimmed();
    while (!w.isEmpty()) {
        const QChar c = w.back();
        if (c == QLatin1Char('"') || c == QLatin1Char('\'') || c == QLatin1Char(')') || c == QLatin1Char(']')
            || c == QChar(0x201D) || c == QChar(0x2019) || c == QChar(0x00BB) || c == QChar(0x300D))
            w.chop(1);
        else
            break;
    }
    return w;
}

// Lowercase words of a text without the punctuation around them.
QStringList plainWords(const QString &text)
{
    static const QRegularExpression split(QStringLiteral("[^\\w']+"),
                                          QRegularExpression::UseUnicodePropertiesOption);
    return text.toLower().split(split, Qt::SkipEmptyParts);
}

// Fraction of `values` strictly below `v`, plus half the ties: a rank in 0..1.
double rankOf(const QVector<double> &sorted, double v)
{
    if (sorted.size() <= 1)
        return 0.5;
    const auto lo = std::lower_bound(sorted.cbegin(), sorted.cend(), v);
    const auto hi = std::upper_bound(sorted.cbegin(), sorted.cend(), v);
    const double below = double(lo - sorted.cbegin());
    const double ties = double(hi - lo);
    return (below + 0.5 * (ties - 1.0)) / double(sorted.size() - 1);
}

bool opensOnConjunction(const QString &text)
{
    static const QSet<QString> openers = {
        QStringLiteral("and"),  QStringLiteral("but"),   QStringLiteral("so"),   QStringLiteral("because"),
        QStringLiteral("or"),   QStringLiteral("then"),  QStringLiteral("also"), QStringLiteral("which"),
        QStringLiteral("that"), QStringLiteral("cause"),
    };
    const QStringList words = plainWords(text);
    return !words.isEmpty() && openers.contains(words.first());
}

double hookOf(const QString &text, int keywordHits)
{
    static const QSet<QString> questionWords = {
        QStringLiteral("how"), QStringLiteral("why"), QStringLiteral("what"),  QStringLiteral("when"),
        QStringLiteral("who"), QStringLiteral("where"), QStringLiteral("did"), QStringLiteral("do"),
        QStringLiteral("is"),  QStringLiteral("are"), QStringLiteral("can"),   QStringLiteral("you"),
        QStringLiteral("imagine"), QStringLiteral("here's"), QStringLiteral("this"),
    };
    const QString trimmed = stripClosers(text);
    double hook = 0.0;
    if (trimmed.endsWith(QLatin1Char('?')) || trimmed.endsWith(QChar(0xFF1F)))
        hook = 1.0;
    if (keywordHits > 0)
        hook = std::max(hook, 0.7);
    if (trimmed.endsWith(QLatin1Char('!')) || trimmed.endsWith(QChar(0xFF01)))
        hook = std::max(hook, 0.5);
    const QStringList words = plainWords(text);
    if (!words.isEmpty() && questionWords.contains(words.first()))
        hook = std::max(hook, 0.5);
    if (!words.isEmpty() && words.first().front().isDigit())
        hook = std::max(hook, 0.5); // "3 things…"
    return hook;
}

} // namespace

bool endsSentence(const QString &word)
{
    const QString w = stripClosers(word);
    if (w.isEmpty())
        return false;
    const QChar last = w.back();
    static const QString enders = QStringLiteral(".!?…。！？");
    if (!enders.contains(last))
        return false;
    if (last == QLatin1Char('.')) {
        static const QSet<QString> abbreviations = {
            QStringLiteral("mr."),  QStringLiteral("mrs."), QStringLiteral("ms."), QStringLiteral("dr."),
            QStringLiteral("st."),  QStringLiteral("vs."),  QStringLiteral("etc."), QStringLiteral("e.g."),
            QStringLiteral("i.e."), QStringLiteral("prof."), QStringLiteral("jr."), QStringLiteral("sr."),
        };
        if (abbreviations.contains(w.toLower()))
            return false;
    }
    return true;
}

QList<Unit> sentencesFromTranscript(const Transcript &transcript, TimeUs inUs, TimeUs outUs,
                                    const SentenceOptions &options)
{
    QList<Unit> out;
    Unit current;
    bool open = false;
    int speaker = -1;
    QStringList text;

    const auto close = [&](bool closed) {
        if (!open)
            return;
        current.text = text.join(QLatin1Char(' '));
        current.closed = closed;
        if (current.words > 0)
            out.append(current);
        current = Unit{};
        text.clear();
        open = false;
    };

    for (int i = 0; i < transcript.words.size(); ++i) {
        const TranscriptWord &w = transcript.words.at(i);
        if (!isSpeechToken(w) || w.startUs < inUs || w.endUs > outUs || w.endUs <= w.startUs)
            continue;
        const double start = usToSeconds(w.startUs);
        const double end = usToSeconds(w.endUs);
        if (open) {
            const double gap = start - current.end;
            const bool otherSpeaker = w.speaker >= 0 && speaker >= 0 && w.speaker != speaker;
            const bool tooLong = current.end - current.start >= options.longSentence && gap >= options.longGap;
            if (gap >= options.gapBreak || otherSpeaker || tooLong)
                close(false);
        }
        if (!open) {
            current.start = start;
            current.firstWord = i;
            open = true;
            speaker = w.speaker;
        }
        current.end = std::max(current.end, end);
        current.lastWord = i;
        if (w.type == TranscriptTokenType::Word) {
            const QString t = w.text.trimmed();
            if (!t.isEmpty()) {
                text.append(t);
                ++current.words;
            }
        }
        if (w.speaker >= 0)
            speaker = w.speaker;
        if (w.type == TranscriptTokenType::Word && endsSentence(w.text))
            close(true);
    }
    close(false);
    return out;
}

QList<Unit> shotsFromCuts(QList<double> cuts, double start, double end)
{
    QList<Unit> out;
    if (end <= start)
        return out;
    std::sort(cuts.begin(), cuts.end());
    QList<double> bounds{start};
    for (double c : std::as_const(cuts)) {
        if (c > bounds.last() && c < end)
            bounds.append(c);
    }
    bounds.append(end);

    // Shots under a second join the next one (the last joins the one before).
    QList<std::pair<double, double>> shots;
    for (int i = 0; i + 1 < bounds.size(); ++i) {
        const double a = bounds.at(i);
        const double b = bounds.at(i + 1);
        if (!shots.isEmpty() && shots.last().second - shots.last().first < 1.0)
            shots.last().second = b;
        else
            shots.append({a, b});
    }
    if (shots.size() > 1 && shots.last().second - shots.last().first < 1.0) {
        const double b = shots.takeLast().second;
        shots.last().second = b;
    }

    for (const auto &[a, b] : std::as_const(shots)) {
        const int pieces = std::max(1, int(std::ceil((b - a) / 10.0 - 1e-9)));
        const double step = (b - a) / pieces;
        for (int p = 0; p < pieces; ++p) {
            Unit u;
            u.start = a + p * step;
            u.end = p + 1 == pieces ? b : a + (p + 1) * step;
            u.closed = true;
            out.append(u);
        }
    }
    return out;
}

QVector<float> levelsDbFromMono(const float *mono, qint64 frames, int sampleRate, double hop)
{
    QVector<float> out;
    if (!mono || frames <= 0 || sampleRate <= 0 || hop <= 0)
        return out;
    const qint64 window = std::max<qint64>(1, qint64(std::llround(hop * sampleRate)));
    out.reserve(int(frames / window) + 1);
    for (qint64 at = 0; at < frames; at += window) {
        const qint64 n = std::min(window, frames - at);
        double sum = 0.0;
        for (qint64 i = 0; i < n; ++i)
            sum += double(mono[at + i]) * double(mono[at + i]);
        const double rms = std::sqrt(sum / double(n));
        out.append(float(std::max(-90.0, rms > 0 ? 20.0 * std::log10(rms) : -90.0)));
    }
    return out;
}

QStringList defaultKeywords()
{
    return {
        QStringLiteral("secret"),    QStringLiteral("mistake"),  QStringLiteral("mistakes"), QStringLiteral("best"),
        QStringLiteral("worst"),     QStringLiteral("never"),    QStringLiteral("always"),   QStringLiteral("important"),
        QStringLiteral("tip"),       QStringLiteral("tips"),     QStringLiteral("trick"),    QStringLiteral("hack"),
        QStringLiteral("how to"),    QStringLiteral("why"),      QStringLiteral("free"),     QStringLiteral("easy"),
        QStringLiteral("amazing"),   QStringLiteral("crazy"),    QStringLiteral("wow"),      QStringLiteral("favorite"),
        QStringLiteral("favourite"), QStringLiteral("biggest"),  QStringLiteral("problem"),  QStringLiteral("truth"),
        QStringLiteral("finally"),   QStringLiteral("surprise"), QStringLiteral("you need"), QStringLiteral("stop"),
        QStringLiteral("don't"),     QStringLiteral("nobody"),   QStringLiteral("everyone"), QStringLiteral("love"),
    };
}

QStringList parseKeywords(const QString &typed)
{
    static const QRegularExpression separators(QStringLiteral("[,;\\n]"));
    QStringList out;
    for (const QString &part : typed.split(separators, Qt::SkipEmptyParts)) {
        const QString k = part.simplified().toLower();
        if (!k.isEmpty() && !out.contains(k))
            out.append(k);
    }
    return out;
}

int countKeywordHits(const QString &text, const QStringList &keywordsLower)
{
    if (text.isEmpty() || keywordsLower.isEmpty())
        return 0;
    const QString lower = text.toLower();
    const QStringList words = plainWords(text);
    int hits = 0;
    for (const QString &k : keywordsLower) {
        if (k.isEmpty())
            continue;
        if (k.contains(QLatin1Char(' '))) {
            for (qsizetype at = lower.indexOf(k); at >= 0; at = lower.indexOf(k, at + k.size()))
                ++hits;
        } else {
            hits += int(words.count(k));
        }
    }
    return hits;
}

QString firstWords(const QString &text, int count)
{
    const QStringList words = text.simplified().split(QLatin1Char(' '), Qt::SkipEmptyParts);
    if (words.size() <= count)
        return words.join(QLatin1Char(' '));
    return words.mid(0, count).join(QLatin1Char(' ')) + QStringLiteral("…");
}

double minLengthFor(double targetSeconds)
{
    const double t = std::clamp(targetSeconds, kMinShortSeconds, kMaxShortSeconds);
    return std::clamp(t * 0.7, kMinShortSeconds, kMaxShortSeconds);
}

double maxLengthFor(double targetSeconds)
{
    const double t = std::clamp(targetSeconds, kMinShortSeconds, kMaxShortSeconds);
    return std::clamp(t * 1.25, kMinShortSeconds, kMaxShortSeconds);
}

QList<Segment> planShorts(const QList<Unit> &units, const Signals &input, const Options &options)
{
    QList<Segment> out;
    const int n = int(units.size());
    if (n == 0)
        return out;

    const double target = std::clamp(options.targetSeconds, kMinShortSeconds, kMaxShortSeconds);
    const double minLen = minLengthFor(target);
    const double maxLen = maxLengthFor(target);
    const double rangeEnd = options.rangeEnd >= 0 ? options.rangeEnd : units.last().end + options.tail;

    // Padded edges: into the silence only, never past half the gap to the neighbouring unit.
    QVector<double> padStart(n), padEnd(n);
    for (int i = 0; i < n; ++i) {
        const Unit &u = units.at(i);
        const double before = i > 0 ? 0.5 * (units.at(i - 1).end + u.start) : options.rangeStart;
        const double after = i + 1 < n ? 0.5 * (u.end + units.at(i + 1).start) : rangeEnd;
        padStart[i] = std::max({u.start - options.leadIn, before, options.rangeStart});
        padEnd[i] = std::min({u.end + options.tail, after, rangeEnd});
        if (padStart[i] > u.start)
            padStart[i] = u.start;
        if (padEnd[i] < u.end)
            padEnd[i] = u.end;
    }

    // Per-unit sums, so a candidate's figures are differences of prefix sums.
    bool hasText = false;
    QVector<int> unitHits(n), unitWords(n);
    QVector<double> prefWords(n + 1, 0.0), prefSpeech(n + 1, 0.0), prefHits(n + 1, 0.0);
    for (int i = 0; i < n; ++i) {
        const Unit &u = units.at(i);
        unitHits[i] = countKeywordHits(u.text, input.keywords);
        unitWords[i] = u.words;
        hasText = hasText || u.words > 0;
        prefWords[i + 1] = prefWords[i] + u.words;
        prefSpeech[i + 1] = prefSpeech[i] + std::max(0.0, u.end - u.start);
        prefHits[i + 1] = prefHits[i] + unitHits[i];
    }

    // Loudness: mean level of the windows that carry sound (> -60 dBFS), from prefix sums.
    const int levelCount = int(input.levelsDb.size());
    QVector<double> prefDb(levelCount + 1, 0.0), prefOn(levelCount + 1, 0.0);
    for (int i = 0; i < levelCount; ++i) {
        const bool on = input.levelsDb.at(i) > -60.0f;
        prefDb[i + 1] = prefDb[i] + (on ? input.levelsDb.at(i) : 0.0);
        prefOn[i + 1] = prefOn[i] + (on ? 1.0 : 0.0);
    }
    const auto meanDb = [&](double a, double b, bool *any) {
        *any = false;
        if (levelCount == 0 || input.hop <= 0)
            return 0.0;
        const int i0 = std::clamp(int(std::floor((a - input.levelsStart) / input.hop)), 0, levelCount);
        const int i1 = std::clamp(int(std::ceil((b - input.levelsStart) / input.hop)), 0, levelCount);
        const double on = prefOn[i1] - prefOn[i0];
        if (on <= 0)
            return -90.0;
        *any = true;
        return (prefDb[i1] - prefDb[i0]) / on;
    };

    QList<double> cuts = input.sceneCuts;
    std::sort(cuts.begin(), cuts.end());
    const auto cutsInside = [&](double a, double b) {
        // A cut right at an edge is the short's own cut, not a change inside it.
        const auto lo = std::upper_bound(cuts.cbegin(), cuts.cend(), a + 0.5);
        const auto hi = std::lower_bound(cuts.cbegin(), cuts.cend(), b - 0.5);
        return hi > lo ? int(hi - lo) : 0;
    };

    struct Raw
    {
        Segment seg;
        double energyDb = 0.0;
        bool energyKnown = false;
        double wordsPerSecond = 0.0;
    };
    QList<Raw> raw;
    const auto collect = [&](double lo, double hi) {
    for (int i = 0; i < n; ++i) {
        for (int j = i; j < n; ++j) {
            const double a = padStart[i];
            const double b = padEnd[j];
            const double len = b - a;
            if (len > hi + 1e-9)
                break; // only longer from here; a single unit over the limit is never cut
            if (len < lo - 1e-9)
                continue;
            Raw r;
            r.seg.start = a;
            r.seg.end = b;
            r.seg.firstUnit = i;
            r.seg.lastUnit = j;
            r.seg.keywordHits = int(prefHits[j + 1] - prefHits[i]);
            r.seg.sceneCuts = cutsInside(a, b);
            r.energyDb = meanDb(units.at(i).start, units.at(j).end, &r.energyKnown);
            const double speech = prefSpeech[j + 1] - prefSpeech[i];
            r.wordsPerSecond = speech > 0 ? (prefWords[j + 1] - prefWords[i]) / speech : 0.0;
            raw.append(r);
        }
    }
    };
    collect(minLen, maxLen);
    // Sentences that never add up to the window (long ones, say) still give shorts: anything from
    // 15 to 60 s, the closest to the target scoring best.
    if (raw.isEmpty())
        collect(kMinShortSeconds, kMaxShortSeconds);
    if (raw.isEmpty())
        return out;

    // Energy and density count relative to the rest of this media: a quiet narrator's liveliest
    // minute should still rank first.
    QVector<double> energies, densities;
    for (const Raw &r : std::as_const(raw)) {
        if (r.energyKnown)
            energies.append(r.energyDb);
        densities.append(r.wordsPerSecond);
    }
    std::sort(energies.begin(), energies.end());
    std::sort(densities.begin(), densities.end());

    QList<Segment> candidates;
    candidates.reserve(raw.size());
    for (Raw &r : raw) {
        Segment &s = r.seg;
        const double len = s.length();
        s.energy = r.energyKnown ? rankOf(energies, r.energyDb) : (energies.isEmpty() ? 0.5 : 0.0);
        s.scenes = std::min(1.0, (s.sceneCuts / std::max(len / 60.0, 1e-6)) / 12.0);
        s.fit = std::clamp(1.0 - std::abs(len - target) / target, 0.0, 1.0);
        double score = 0.0;
        if (hasText) {
            s.density = rankOf(densities, r.wordsPerSecond);
            s.keywords = std::min(1.0, s.keywordHits / 3.0);
            s.hook = hookOf(units.at(s.firstUnit).text, unitHits[s.firstUnit]);
            score = 0.30 * s.energy + 0.15 * s.density + 0.20 * s.keywords + 0.10 * s.scenes + 0.15 * s.hook
                    + 0.10 * s.fit;
            score *= 100.0;
            if (opensOnConjunction(units.at(s.firstUnit).text))
                score -= 8.0; // it leans on what came before
            if (!units.at(s.lastUnit).closed)
                score -= 4.0; // trails off on a pause rather than a full stop
        } else {
            score = 100.0 * (0.6 * s.energy + 0.3 * s.scenes + 0.1 * s.fit);
        }
        s.score = std::clamp(score, 0.0, 100.0);
        candidates.append(s);
    }

    std::sort(candidates.begin(), candidates.end(), [](const Segment &a, const Segment &b) {
        if (a.score != b.score)
            return a.score > b.score;
        if (a.start != b.start)
            return a.start < b.start;
        return a.end > b.end;
    });

    for (const Segment &c : std::as_const(candidates)) {
        if (int(out.size()) >= std::max(1, options.maxResults))
            break;
        const bool overlaps = std::any_of(out.cbegin(), out.cend(), [&c](const Segment &taken) {
            return c.start < taken.end - 1e-9 && taken.start < c.end - 1e-9;
        });
        if (overlaps)
            continue;
        Segment chosen = c;
        QStringList text;
        for (int k = c.firstUnit; k <= c.lastUnit; ++k) {
            if (!units.at(k).text.isEmpty())
                text.append(units.at(k).text);
        }
        chosen.text = text.join(QLatin1Char(' '));
        out.append(chosen);
    }
    return out;
}

QPair<double, double> timelineSpanOfSource(const Clip &clip, double sourceStart, double sourceEnd)
{
    // The clip's own inverse mapping, so a speed curve and a reversed clip land where they play.
    const double start = usToSeconds(clip.timelineStart);
    const double a = start + usToSeconds(clip.sourceUsToClipLocalUs(secondsToUs(sourceStart)));
    const double b = start + usToSeconds(clip.sourceUsToClipLocalUs(secondsToUs(sourceEnd)));
    return {std::min(a, b), std::max(a, b)};
}

} // namespace drift::shorts
