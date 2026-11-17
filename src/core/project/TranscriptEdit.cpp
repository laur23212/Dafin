#include "core/project/TranscriptEdit.h"

#include <QRegularExpression>
#include <QStringList>

#include <algorithm>
#include <cmath>

namespace drift {

namespace {

QStringList splitOnSpace(const QString &text)
{
    static const QRegularExpression space(QStringLiteral("\\s+"));
    return text.split(space, Qt::SkipEmptyParts);
}

// A word that closes a sentence: ends in . ? ! … or their CJK forms, ignoring closing quotes.
bool endsSentence(const QString &word)
{
    int i = int(word.size()) - 1;
    while (i >= 0 && (word.at(i) == QLatin1Char('"') || word.at(i) == QLatin1Char('\'')
                      || word.at(i) == QChar(0x201D) || word.at(i) == QChar(0x2019)
                      || word.at(i) == QLatin1Char(')')))
        --i;
    if (i < 0)
        return false;
    const QChar c = word.at(i);
    return c == QLatin1Char('.') || c == QLatin1Char('?') || c == QLatin1Char('!')
           || c == QChar(0x2026) || c == QChar(0x3002) || c == QChar(0xFF1F) || c == QChar(0xFF01);
}

TranscriptTokenType typeForText(const QString &text)
{
    return isFillerWord(text) ? TranscriptTokenType::Filler : TranscriptTokenType::Word;
}

} // namespace

QList<TranscriptLine> transcriptLines(const Transcript &transcript, TimeUs inUs, TimeUs outUs,
                                      TimeUs breakOnGapUs, int minSentenceWords, int maxWords)
{
    QList<TranscriptLine> lines;
    TranscriptLine cur;
    int count = 0;
    bool sentenceEnded = false;
    auto flush = [&] {
        if (cur.firstWord >= 0)
            lines.append(cur);
        cur = TranscriptLine{};
        count = 0;
        sentenceEnded = false;
    };

    for (const int i : transcript.wordsInRange(inUs, outUs)) {
        const TranscriptWord &w = transcript.words.at(i);
        if (!isSpeechToken(w)) {
            if (w.type == TranscriptTokenType::AudioEvent)
                flush();
            continue;
        }
        const QString text = w.text.trimmed();
        if (text.isEmpty())
            continue;
        if (cur.firstWord >= 0
            && (w.startUs - cur.endUs >= breakOnGapUs || w.speaker != cur.speaker
                || (sentenceEnded && count >= minSentenceWords) || (maxWords > 0 && count >= maxWords)))
            flush();
        if (cur.firstWord < 0) {
            cur.startUs = w.startUs;
            cur.endUs = w.endUs;
            cur.speaker = w.speaker;
            cur.firstWord = i;
        }
        cur.endUs = std::max(cur.endUs, w.endUs);
        cur.lastWord = i;
        if (!cur.text.isEmpty())
            cur.text += QLatin1Char(' ');
        cur.text += text;
        ++count;
        sentenceEnded = endsSentence(text);
    }
    flush();
    return lines;
}

bool replaceTranscriptWords(Transcript &transcript, int firstWord, int lastWord, const QString &text)
{
    QList<TranscriptWord> &words = transcript.words;
    if (firstWord < 0 || lastWord >= words.size() || firstWord > lastWord)
        return false;

    QList<int> speech;
    for (int i = firstWord; i <= lastWord; ++i) {
        if (isSpeechToken(words.at(i)))
            speech.append(i);
    }
    if (speech.isEmpty())
        return false;

    const QStringList newWords = splitOnSpace(text);
    QStringList oldWords;
    for (const int i : std::as_const(speech))
        oldWords.append(words.at(i).text.trimmed());
    if (oldWords == newWords)
        return false;

    const TimeUs spanStart = words.at(speech.first()).startUs;
    TimeUs spanEnd = spanStart;
    for (const int i : std::as_const(speech))
        spanEnd = std::max(spanEnd, words.at(i).endUs);
    const qint16 speaker = words.at(speech.first()).speaker;

    QList<TranscriptWord> replacement;
    replacement.reserve(newWords.size());
    if (newWords.size() == speech.size()) {
        for (int k = 0; k < speech.size(); ++k) {
            TranscriptWord w = words.at(speech.at(k));
            if (w.text.trimmed() != newWords.at(k)) {
                w.text = newWords.at(k);
                w.type = typeForText(w.text);
                w.confidence = NAN;
            }
            replacement.append(w);
        }
    } else if (!newWords.isEmpty()) {
        int total = 0;
        for (const QString &w : newWords)
            total += std::max(1, int(w.size()));
        const double span = double(std::max<TimeUs>(1, spanEnd - spanStart));
        TimeUs cursor = spanStart;
        int done = 0;
        for (int k = 0; k < newWords.size(); ++k) {
            done += std::max(1, int(newWords.at(k).size()));
            TranscriptWord w;
            w.startUs = cursor;
            w.endUs = k + 1 == newWords.size()
                          ? spanEnd
                          : spanStart + static_cast<TimeUs>(std::llround(span * done / total));
            w.endUs = std::max(w.endUs, w.startUs);
            w.text = newWords.at(k);
            w.type = typeForText(w.text);
            w.speaker = speaker;
            w.interpolated = true;
            replacement.append(w);
            cursor = w.endUs;
        }
    }

    QList<TranscriptWord> out;
    out.reserve(words.size() - speech.size() + replacement.size());
    for (int i = 0; i < firstWord; ++i)
        out.append(words.at(i));
    out.append(replacement);
    for (int i = firstWord; i <= lastWord; ++i) {
        if (words.at(i).type == TranscriptTokenType::AudioEvent)
            out.append(words.at(i));
    }
    for (int i = lastWord + 1; i < words.size(); ++i)
        out.append(words.at(i));
    std::stable_sort(out.begin(), out.end(), [](const TranscriptWord &a, const TranscriptWord &b) {
        return a.startUs < b.startUs;
    });
    words = out;
    return true;
}

QString transcriptTextInWindow(const Transcript &transcript, TimeUs startUs, TimeUs endUs)
{
    QStringList parts;
    if (endUs <= startUs)
        return {};
    // A word whose middle is inside the window overlaps it, so wordsInRange already holds it.
    for (const int i : transcript.wordsInRange(startUs, endUs)) {
        const TranscriptWord &w = transcript.words.at(i);
        if (!isSpeechToken(w))
            continue;
        const QString text = w.text.trimmed();
        if (text.isEmpty())
            continue;
        const TimeUs mid = w.startUs + (std::max(w.endUs, w.startUs) - w.startUs) / 2;
        if (mid >= startUs && mid < endUs)
            parts.append(text);
    }
    return parts.join(QLatin1Char(' '));
}

QList<FillerWord> defaultFillerWords(const QString &language)
{
    const QString lang = language.trimmed().toLower().section(QLatin1Char('-'), 0, 0)
                             .section(QLatin1Char('_'), 0, 0);
    if (lang == QLatin1String("en")) {
        // "you know" and "like" are real words far more often than they are filler: off until asked.
        return {{QStringLiteral("um"), true},       {QStringLiteral("uh"), true},
                {QStringLiteral("er"), true},       {QStringLiteral("ah"), true},
                {QStringLiteral("hmm"), true},      {QStringLiteral("you know"), false},
                {QStringLiteral("like"), false}};
    }
    if (lang == QLatin1String("ro")) {
        return {{QString::fromUtf8("ăă"), true},  {QString::fromUtf8("îî"), true},
                {QString::fromUtf8("păi"), false}, {QStringLiteral("deci"), false}};
    }
    return {};
}

QString fillerMatchKey(const QString &text)
{
    QString out;
    out.reserve(text.size());
    for (const QChar c : text) {
        if (!c.isLetterOrNumber())
            continue;
        const QChar lower = c.toLower();
        if (!out.isEmpty() && out.back() == lower && lower.isLetter())
            continue;
        out.append(lower);
    }
    return out;
}

QList<FillerHit> findFillerWords(const Transcript &transcript, TimeUs inUs, TimeUs outUs,
                                 const QList<FillerWord> &fillers)
{
    // Each enabled entry as its words' keys; the longest are tried first.
    QList<QStringList> needles;
    for (const FillerWord &f : fillers) {
        if (!f.enabled)
            continue;
        QStringList keys;
        for (const QString &word : splitOnSpace(f.text)) {
            const QString key = fillerMatchKey(word);
            if (!key.isEmpty())
                keys.append(key);
        }
        if (!keys.isEmpty() && !needles.contains(keys))
            needles.append(keys);
    }
    std::stable_sort(needles.begin(), needles.end(),
                     [](const QStringList &a, const QStringList &b) { return a.size() > b.size(); });
    if (needles.isEmpty() || outUs <= inUs)
        return {};

    QList<int> speech;
    QStringList keys;
    for (const int i : transcript.wordsInRange(inUs, outUs)) {
        const TranscriptWord &w = transcript.words.at(i);
        if (!isSpeechToken(w))
            continue;
        speech.append(i);
        keys.append(fillerMatchKey(w.text));
    }

    QList<FillerHit> out;
    for (int p = 0; p < speech.size();) {
        int matched = 0;
        for (const QStringList &needle : std::as_const(needles)) {
            if (p + needle.size() > speech.size())
                continue;
            bool hit = true;
            for (int k = 0; k < needle.size() && hit; ++k)
                hit = keys.at(p + k) == needle.at(k);
            if (hit) {
                matched = int(needle.size());
                break;
            }
        }
        if (matched == 0) {
            ++p;
            continue;
        }
        FillerHit h;
        h.firstWord = speech.at(p);
        h.lastWord = speech.at(p + matched - 1);
        h.startUs = transcript.words.at(h.firstWord).startUs;
        h.endUs = h.startUs;
        QStringList said;
        for (int k = 0; k < matched; ++k) {
            const TranscriptWord &w = transcript.words.at(speech.at(p + k));
            h.endUs = std::max(h.endUs, w.endUs);
            said.append(w.text.trimmed());
        }
        h.text = said.join(QLatin1Char(' '));
        out.append(h);
        p += matched;
    }
    return out;
}

} // namespace drift
