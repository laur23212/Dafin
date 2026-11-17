#pragma once

// Dafin: the planning half of AI › Smart short clips. Pure arithmetic over seconds, words and
// levels, so it is tested without models or a project (tests/tst_aishorts.cpp); the controller
// (app/jobs/ai/ShortClipsController) reads the transcript, the audio and the shots, calls these
// and turns the chosen segments into 9:16 editions.
//
// A short is a run of whole units — sentences when there is speech, shots when there is not — so
// its cut points always land on a unit boundary, padded only into the silence between units and
// never into a neighbouring word.

#include "core/base/Time.h"

#include <QList>
#include <QString>
#include <QStringList>
#include <QVector>

namespace drift {
struct Transcript;
}

namespace drift::shorts {

// Every short lasts between these, whatever the length setting.
inline constexpr double kMinShortSeconds = 15.0;
inline constexpr double kMaxShortSeconds = 60.0;

// One sentence (or, without speech, one shot), in source seconds.
struct Unit
{
    double start = 0.0;
    double end = 0.0;
    QString text;        // the words, empty for a shot
    int firstWord = -1;  // into Transcript::words, -1 for a shot
    int lastWord = -1;
    bool closed = true;  // ended on . ! ? (or is a shot), rather than on a pause
    int words = 0;       // spoken words in it
};

// True when `word` ends a sentence: . ! ? … (and their CJK forms), allowing closing quotes or
// brackets after them, but not common abbreviations such as "Mr." or "e.g.".
bool endsSentence(const QString &word);

struct SentenceOptions
{
    double gapBreak = 1.2;     // a pause this long ends a sentence even without punctuation
    double longSentence = 25.0; // a sentence this long ends at the next pause of longGap
    double longGap = 0.3;
};

// The sentences of the speech fully inside [inUs, outUs), from the transcript's words (fillers time
// a sentence but are left out of its text). Breaks on terminal punctuation, a long pause, a change
// of speaker, or a very long run at its next short pause.
QList<Unit> sentencesFromTranscript(const Transcript &transcript, TimeUs inUs, TimeUs outUs,
                                    const SentenceOptions &options = {});

// Without speech: the shots between `cuts` inside [start, end]. Shots under a second join the
// next; shots over 10 s are split evenly so a long take can still give a short.
QList<Unit> shotsFromCuts(QList<double> cuts, double start, double end);

// The loudness envelope of mono audio: RMS in dBFS of each `hop`-second window, floored at -90.
QVector<float> levelsDbFromMono(const float *mono, qint64 frames, int sampleRate, double hop);

// Words that tend to start or carry a highlight in English ("secret", "how to", "mistake"…).
QStringList defaultKeywords();
// The keywords a user typed, comma or semicolon separated, lowercased and trimmed.
QStringList parseKeywords(const QString &typed);
// How often the keywords occur in `text`: whole words for one-word keywords, anywhere for phrases.
int countKeywordHits(const QString &text, const QStringList &keywordsLower);

// The first `count` words of `text`, with "…" when there are more.
QString firstWords(const QString &text, int count = 8);

struct Signals
{
    QVector<float> levelsDb; // levelsDbFromMono over the analysed audio
    double hop = 0.1;
    double levelsStart = 0.0; // source seconds of levelsDb[0]
    QList<double> sceneCuts;  // source seconds
    QStringList keywords;     // lowercase
};

struct Options
{
    double targetSeconds = 30.0; // the length setting, 15..60
    int maxResults = 8;
    double leadIn = 0.15; // silence kept before the first word…
    double tail = 0.30;   // …and after the last, never past half the gap to a neighbour
    double rangeStart = 0.0; // nothing before this (the clip's source in point)
    double rangeEnd = -1.0;  // nor after this; < 0 = the end of the last unit + tail
};

// The length window a target gives: 0.7× to 1.25× of it, inside [15, 60] s. When no run of whole
// units fits it, planShorts takes anything from 15 to 60 s instead.
double minLengthFor(double targetSeconds);
double maxLengthFor(double targetSeconds);

struct Segment
{
    double start = 0.0; // source seconds, padded
    double end = 0.0;
    int firstUnit = -1;
    int lastUnit = -1;
    double score = 0.0; // 0..100
    QString text;
    // The parts of the score, each 0..1, for the tooltip and the tests.
    double energy = 0.0;
    double density = 0.0;
    double keywords = 0.0;
    double scenes = 0.0;
    double hook = 0.0;
    double fit = 0.0;
    int keywordHits = 0;
    int sceneCuts = 0;
    double length() const { return end - start; }
};

// The best non-overlapping shorts, best first. Every candidate is a run of whole units whose padded
// length fits the target's window (else 15..60 s); it is scored on speech energy (louder than the rest of the
// file), speech density, keywords, scene changes inside it, a hook at its start (a question, a
// keyword, an exclamation) and how close it is to the target, less a little for opening on "and",
// "but" or "so" or ending without a full stop.
QList<Segment> planShorts(const QList<Unit> &units, const Signals &input, const Options &options);

} // namespace drift::shorts
