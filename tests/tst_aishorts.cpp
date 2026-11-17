// Dafin: AI › Smart short clips (segment scoring and sentence boundaries, the reframe maths) and
// AI › Media auto-tagging (tag cleaning, picking, storage and the media bin search).

#include "core/edit/AutoReframe.h"
#include "core/edit/ShortClipPlan.h"
#include "core/project/Project.h"
#include "core/project/Transcript.h"
#include "core/project/media/MediaTags.h"

#include <QtMath>
#include <QtTest>

#include <cmath>

using namespace drift;
using namespace drift::shorts;

namespace {

TranscriptWord word(double start, double end, const QString &text, int speaker = -1,
                    TranscriptTokenType type = TranscriptTokenType::Word)
{
    TranscriptWord w;
    w.startUs = secondsToUs(start);
    w.endUs = secondsToUs(end);
    w.text = text;
    w.speaker = qint16(speaker);
    w.type = type;
    return w;
}

// `count` sentences of `wordsEach` words, each word 0.4 s with 0.1 s between words and `gap` s
// between sentences. Sentence k's text starts with `first(k)`.
Transcript sentences(int count, int wordsEach, double gap, const std::function<QString(int)> &first = {})
{
    Transcript t;
    double at = 1.0;
    for (int k = 0; k < count; ++k) {
        for (int i = 0; i < wordsEach; ++i) {
            QString text = i == 0 && first ? first(k) : QStringLiteral("word%1").arg(i);
            if (i + 1 == wordsEach)
                text += QLatin1Char('.');
            t.words.append(word(at, at + 0.4, text));
            at += 0.5;
        }
        at += gap - 0.1;
    }
    return t;
}

// Every word is either wholly inside [a, b] or wholly outside it.
bool cutsBetweenWords(const Transcript &t, double a, double b)
{
    for (const TranscriptWord &w : t.words) {
        const double s = usToSeconds(w.startUs);
        const double e = usToSeconds(w.endUs);
        const bool inside = s >= a - 1e-9 && e <= b + 1e-9;
        const bool outside = e <= a + 1e-9 || s >= b - 1e-9;
        if (!inside && !outside)
            return false;
    }
    return true;
}

} // namespace

class TestAiShorts : public QObject
{
    Q_OBJECT

private slots:
    // ---- sentences ------------------------------------------------------------------------

    void sentenceEnds()
    {
        QVERIFY(endsSentence(QStringLiteral("done.")));
        QVERIFY(endsSentence(QStringLiteral("really?")));
        QVERIFY(endsSentence(QStringLiteral("wow!\"")));
        QVERIFY(endsSentence(QStringLiteral("so…")));
        QVERIFY(endsSentence(QStringLiteral("好。")));
        QVERIFY(!endsSentence(QStringLiteral("word")));
        QVERIFY(!endsSentence(QStringLiteral("comma,")));
        QVERIFY(!endsSentence(QStringLiteral("Mr.")));
        QVERIFY(!endsSentence(QStringLiteral("e.g.")));
    }

    void sentencesBreakOnPunctuationPauseAndSpeaker()
    {
        Transcript t;
        t.words = {
            word(0.0, 0.3, QStringLiteral("Hello")),  word(0.4, 0.7, QStringLiteral("there.")),
            word(0.8, 1.0, QStringLiteral("um"), -1, TranscriptTokenType::Filler),
            word(1.1, 1.4, QStringLiteral("Next")),   word(1.5, 1.8, QStringLiteral("one")),
            // 1.5 s pause, no punctuation: a new sentence
            word(3.3, 3.6, QStringLiteral("after")),  word(3.7, 4.0, QStringLiteral("pause")),
            // other speaker
            word(4.1, 4.4, QStringLiteral("Reply"), 1), word(4.5, 4.8, QStringLiteral("here?"), 1),
        };
        // speakers: the first words have none, so only the explicit change to 1 after 0 counts
        t.words[5].speaker = 0;
        t.words[6].speaker = 0;
        const QList<Unit> u = sentencesFromTranscript(t, 0, secondsToUs(10));
        QCOMPARE(u.size(), 4);
        QCOMPARE(u.at(0).text, QStringLiteral("Hello there."));
        QVERIFY(u.at(0).closed);
        // The filler times the sentence but is not in its text.
        QCOMPARE(u.at(1).text, QStringLiteral("Next one"));
        QCOMPARE(u.at(1).start, 0.8);
        QVERIFY(!u.at(1).closed);
        QCOMPARE(u.at(2).text, QStringLiteral("after pause"));
        QCOMPARE(u.at(3).text, QStringLiteral("Reply here?"));
        QVERIFY(u.at(3).closed);
        QCOMPARE(u.at(3).words, 2);

        // Only words wholly inside the range count.
        const QList<Unit> part = sentencesFromTranscript(t, secondsToUs(1.05), secondsToUs(4.0));
        QCOMPARE(part.size(), 2);
        QCOMPARE(part.first().text, QStringLiteral("Next one"));
    }

    // ---- segments -------------------------------------------------------------------------

    void lengthWindowFollowsTheSetting()
    {
        QCOMPARE(minLengthFor(15), 15.0);
        QCOMPARE(maxLengthFor(15), 18.75);
        QCOMPARE(minLengthFor(30), 21.0);
        QCOMPARE(maxLengthFor(30), 37.5);
        QCOMPARE(minLengthFor(60), 42.0);
        QCOMPARE(maxLengthFor(60), 60.0);
        // Out-of-range settings are clamped to 15..60.
        QCOMPARE(maxLengthFor(5), 18.75);
        QCOMPARE(maxLengthFor(600), 60.0);
    }

    void segmentsLandOnSentenceBoundaries()
    {
        // 30 sentences of 8 words (about 4 s each) with 0.6 s between them.
        const Transcript t = sentences(30, 8, 0.6);
        const QList<Unit> units = sentencesFromTranscript(t, 0, secondsToUs(1000));
        QCOMPARE(units.size(), 30);
        for (double target : {15.0, 30.0, 45.0, 60.0}) {
            Options o;
            o.targetSeconds = target;
            o.maxResults = 20;
            const QList<Segment> segs = planShorts(units, {}, o);
            QVERIFY2(!segs.isEmpty(), qPrintable(QStringLiteral("target %1").arg(target)));
            for (const Segment &s : segs) {
                QVERIFY(s.length() >= minLengthFor(target) - 1e-9);
                QVERIFY(s.length() <= maxLengthFor(target) + 1e-9);
                QVERIFY(s.length() >= kMinShortSeconds - 1e-9);
                QVERIFY(s.length() <= kMaxShortSeconds + 1e-9);
                QVERIFY(cutsBetweenWords(t, s.start, s.end));
                // Starts no earlier than halfway into the gap before its first sentence, and the
                // padding never reaches a neighbouring sentence.
                const Unit &first = units.at(s.firstUnit);
                const Unit &last = units.at(s.lastUnit);
                QVERIFY(s.start <= first.start && s.start >= first.start - o.leadIn - 1e-9);
                QVERIFY(s.end >= last.end && s.end <= last.end + o.tail + 1e-9);
                if (s.firstUnit > 0)
                    QVERIFY(s.start >= 0.5 * (units.at(s.firstUnit - 1).end + first.start) - 1e-9);
                if (s.lastUnit + 1 < units.size())
                    QVERIFY(s.end <= 0.5 * (last.end + units.at(s.lastUnit + 1).start) + 1e-9);
                // The text is the whole sentences, first word to last.
                QVERIFY(s.text.startsWith(QStringLiteral("word0")));
                QVERIFY(s.text.endsWith(QLatin1Char('.')));
            }
        }
    }

    void paddingStaysInsideTheRange()
    {
        const Transcript t = sentences(10, 8, 0.6);
        const QList<Unit> units = sentencesFromTranscript(t, 0, secondsToUs(1000));
        Options o;
        o.targetSeconds = 15;
        o.rangeStart = units.first().start - 0.05; // the clip starts just before the first word
        o.rangeEnd = units.last().end + 0.05;
        o.maxResults = 10;
        for (const Segment &s : planShorts(units, {}, o)) {
            QVERIFY(s.start >= o.rangeStart - 1e-9);
            QVERIFY(s.end <= o.rangeEnd + 1e-9);
        }
    }

    void aSentenceLongerThanTheLimitIsNeverCut()
    {
        // One 75 s sentence: no short can be made without cutting it.
        Transcript t;
        for (int i = 0; i < 150; ++i)
            t.words.append(word(i * 0.5, i * 0.5 + 0.45, i == 149 ? QStringLiteral("end.") : QStringLiteral("w")));
        SentenceOptions so;
        so.longSentence = 1000; // keep it one sentence
        const QList<Unit> units = sentencesFromTranscript(t, 0, secondsToUs(1000), so);
        QCOMPARE(units.size(), 1);
        Options o;
        o.targetSeconds = 60;
        QVERIFY(planShorts(units, {}, o).isEmpty());
    }

    void longSentencesFallBackToTheFullRange()
    {
        // 12 s sentences: one is too short for a 15 s short and two overshoot its 18.75 s window,
        // so the plan takes two anyway (inside 15..60) rather than nothing.
        Transcript t;
        double at = 0.0;
        for (int k = 0; k < 6; ++k) {
            for (int i = 0; i < 24; ++i) {
                t.words.append(word(at, at + 0.45, i == 23 ? QStringLiteral("end.") : QStringLiteral("w")));
                at += 0.5;
            }
            at += 0.6;
        }
        const QList<Unit> units = sentencesFromTranscript(t, 0, secondsToUs(1000));
        QCOMPARE(units.size(), 6);
        Options o;
        o.targetSeconds = 15;
        const QList<Segment> segs = planShorts(units, {}, o);
        QVERIFY(!segs.isEmpty());
        for (const Segment &s : segs) {
            QCOMPARE(s.lastUnit - s.firstUnit, 1);
            QVERIFY(s.length() >= kMinShortSeconds && s.length() <= kMaxShortSeconds);
            QVERIFY(cutsBetweenWords(t, s.start, s.end));
        }
    }

    void segmentsDoNotOverlapAndAreCapped()
    {
        const Transcript t = sentences(60, 8, 0.6);
        const QList<Unit> units = sentencesFromTranscript(t, 0, secondsToUs(10000));
        Options o;
        o.targetSeconds = 20;
        o.maxResults = 5;
        const QList<Segment> segs = planShorts(units, {}, o);
        QCOMPARE(segs.size(), 5);
        for (int i = 0; i < segs.size(); ++i) {
            if (i > 0)
                QVERIFY(segs.at(i - 1).score >= segs.at(i).score);
            for (int j = i + 1; j < segs.size(); ++j)
                QVERIFY(segs.at(i).end <= segs.at(j).start + 1e-9 || segs.at(j).end <= segs.at(i).start + 1e-9);
        }
    }

    void loudKeywordQuestionWins()
    {
        // 40 sentences; 20..25 are louder, start with a question and say "secret" and "mistake".
        const Transcript t = sentences(40, 8, 0.6, [](int k) {
            if (k == 20)
                return QStringLiteral("Why");
            if (k >= 20 && k <= 25)
                return QStringLiteral("secret");
            return QStringLiteral("word0");
        });
        Transcript asked = t;
        asked.words[20 * 8 + 7].text = QStringLiteral("word7?"); // sentence 20 asks a question
        const QList<Unit> units = sentencesFromTranscript(asked, 0, secondsToUs(10000));
        QCOMPARE(units.size(), 40);

        Signals sig;
        sig.hop = 0.1;
        sig.levelsStart = 0.0;
        const double total = units.last().end + 2.0;
        sig.levelsDb.fill(-30.0f, int(total / sig.hop) + 1);
        for (int k = 20; k <= 25; ++k) {
            for (int i = int(units.at(k).start / sig.hop); i <= int(units.at(k).end / sig.hop); ++i)
                sig.levelsDb[i] = -12.0f;
        }
        sig.keywords = defaultKeywords();
        Options o;
        o.targetSeconds = 20;
        o.maxResults = 3;
        const QList<Segment> segs = planShorts(units, sig, o);
        QVERIFY(!segs.isEmpty());
        const Segment &best = segs.first();
        QVERIFY2(best.firstUnit >= 19 && best.lastUnit <= 26,
                 qPrintable(QStringLiteral("best was %1..%2").arg(best.firstUnit).arg(best.lastUnit)));
        QVERIFY(best.keywordHits >= 3);
        QVERIFY(best.energy > 0.8);
        QVERIFY(best.score > segs.last().score);
    }

    void openingOnAConjunctionCostsPoints()
    {
        const Transcript plain = sentences(12, 8, 0.6);
        const Transcript joined = sentences(12, 8, 0.6, [](int k) {
            return k == 0 ? QStringLiteral("And") : QStringLiteral("word0");
        });
        Options o;
        o.targetSeconds = 15;
        o.maxResults = 1;
        // Restrict to the first sentences so both plans score the same window.
        const QList<Unit> a = sentencesFromTranscript(plain, 0, secondsToUs(20)).mid(0, 4);
        const QList<Unit> b = sentencesFromTranscript(joined, 0, secondsToUs(20)).mid(0, 4);
        const QList<Segment> sa = planShorts(a, {}, o);
        const QList<Segment> sb = planShorts(b, {}, o);
        QCOMPARE(sa.size(), 1);
        QCOMPARE(sb.size(), 1);
        QCOMPARE(sa.first().firstUnit, 0);
        if (sb.first().firstUnit == 0)
            QVERIFY(sb.first().score < sa.first().score);
        else
            QVERIFY(sb.first().firstUnit > 0); // it avoided opening on "And"
    }

    void shotsWhenThereIsNoSpeech()
    {
        // Cuts at 0.5 (a 0.5 s shot joins the next), 10, 34; 34..40 stays; 10..34 is split in three.
        const QList<Unit> shots = shotsFromCuts({34.0, 0.5, 10.0}, 0.0, 40.0);
        QCOMPARE(shots.size(), 5);
        QCOMPARE(shots.at(0).start, 0.0);
        QCOMPARE(shots.at(0).end, 10.0);
        QCOMPARE(shots.at(1).start, 10.0);
        QCOMPARE(shots.at(3).end, 34.0);
        QCOMPARE(shots.at(4).start, 34.0);
        QCOMPARE(shots.at(4).end, 40.0);
        for (const Unit &u : shots)
            QVERIFY(u.end - u.start <= 10.0 + 1e-9);

        Options o;
        o.targetSeconds = 15;
        o.leadIn = 0;
        o.tail = 0;
        Signals sig;
        sig.sceneCuts = {0.5, 10.0, 34.0};
        const QList<Segment> segs = planShorts(shots, sig, o);
        QVERIFY(!segs.isEmpty());
        // Boundaries are shot boundaries.
        QList<double> bounds;
        for (const Unit &u : shots)
            bounds << u.start << u.end;
        for (const Segment &s : segs) {
            QVERIFY(bounds.contains(s.start));
            QVERIFY(bounds.contains(s.end));
        }
    }

    void levelsAndKeywords()
    {
        QVector<float> mono(48000);
        for (int i = 0; i < mono.size(); ++i)
            mono[i] = float(0.5 * std::sin(2.0 * M_PI * 440.0 * i / 48000.0));
        const QVector<float> db = levelsDbFromMono(mono.constData(), mono.size(), 48000, 0.1);
        QCOMPARE(db.size(), 10);
        // RMS of a 0.5 sine is 0.354 → -9.03 dBFS.
        QVERIFY(std::abs(db.at(5) - (-9.03f)) < 0.1f);
        const QVector<float> silent = levelsDbFromMono(QVector<float>(4800, 0.0f).constData(), 4800, 48000, 0.1);
        QCOMPARE(silent.first(), -90.0f);

        const QStringList k = parseKeywords(QStringLiteral(" Secret ; how to, , Mistake\nsecret"));
        QCOMPARE(k, (QStringList{QStringLiteral("secret"), QStringLiteral("how to"), QStringLiteral("mistake")}));
        QCOMPARE(countKeywordHits(QStringLiteral("The SECRET of how to win: no secrets, a secret."), k), 3);
        QCOMPARE(countKeywordHits(QStringLiteral("How to? how to!"), k), 2);
        QCOMPARE(firstWords(QStringLiteral("one two  three four"), 3), QStringLiteral("one two three…"));
        QCOMPARE(firstWords(QStringLiteral("one two"), 3), QStringLiteral("one two"));
    }

    // ---- reframe --------------------------------------------------------------------------

    void centreFallbackIsTheFullHeight()
    {
        // A 16:9 source on a 1080x1920 canvas with no face: the full-height middle strip.
        const reframe::Result r =
            reframe::reframeKeys({reframe::centreSample(0.0)}, 1080, 1920, 16.0 / 9.0, 9.0 / 16.0, 1920);
        QCOMPARE(r.keys.size(), 1);
        const reframe::Key &k = r.keys.first();
        QVERIFY(std::abs(k.height - 1920.0) < 1e-6);
        QVERIFY(std::abs(k.width - 1920.0 * 16.0 / 9.0) < 1e-6);
        QVERIFY(std::abs(k.y) < 1e-6);
        QVERIFY(std::abs((k.x + k.width / 2.0) - 540.0) < 1e-6); // centred
        // The strip is 1920 * (9/16) / (16/9) = 607.5 source pixels wide, drawn 1080 wide.
        QVERIFY(std::abs(r.tightestScale - 1080.0 / 607.5) < 1e-6);
    }

    void reframeFollowsTheFace()
    {
        reframe::FaceSample left;
        left.cx = 0.25;
        left.cy = 0.4;
        left.ry = 0.2;
        const reframe::Result r = reframe::reframeKeys({left}, 1080, 1920, 16.0 / 9.0, 9.0 / 16.0, 1920);
        const reframe::Key &k = r.keys.first();
        // The face's centre lands on the canvas's centre line.
        QVERIFY(std::abs((k.x + 0.25 * k.width) - 540.0) < 1e-6);
        // Never stretched: the source keeps its aspect.
        QVERIFY(std::abs(k.width / k.height - 16.0 / 9.0) < 1e-9);
    }

    void faceGapsHoldTheLastFace()
    {
        reframe::FaceSample missing;
        missing.t = -1;
        reframe::FaceSample a;
        a.cx = 0.3;
        reframe::FaceSample b;
        b.cx = 0.7;
        const QList<reframe::FaceSample> filled =
            reframe::fillFaceGaps({missing, a, missing, missing, b, missing}, 0.25);
        QCOMPARE(filled.size(), 6);
        QCOMPARE(filled.at(0).cx, 0.3); // before the first face: that face
        QCOMPARE(filled.at(2).cx, 0.3);
        QCOMPARE(filled.at(3).cx, 0.3);
        QCOMPARE(filled.at(5).cx, 0.7);
        QCOMPARE(filled.at(3).t, 0.75);
        const QList<reframe::FaceSample> none = reframe::fillFaceGaps({missing, missing}, 0.5);
        QCOMPARE(none.at(1).ry, 0.5); // a centre crop
        QCOMPARE(none.at(1).cx, 0.5);
    }

    // ---- tags -----------------------------------------------------------------------------

    void tagsAreCleanedAndMerged()
    {
        QCOMPARE(normalizeTag(QStringLiteral("  The Dog, ")), QStringLiteral("dog"));
        QCOMPARE(normalizeTag(QStringLiteral("“Rock   Music”")), QStringLiteral("rock music"));
        QCOMPARE(normalizeTag(QStringLiteral("a")), QStringLiteral("a"));
        QCOMPARE(normalizeTag(QStringLiteral("...")), QString());
        const QStringList merged = mergeTags({QStringLiteral("dog"), QStringLiteral("Beach")},
                                             {QStringLiteral("beach"), QStringLiteral("a person"), QString()});
        QCOMPARE(merged, (QStringList{QStringLiteral("dog"), QStringLiteral("beach"), QStringLiteral("person")}));
        QStringList many;
        for (int i = 0; i < 30; ++i)
            many << QStringLiteral("tag %1").arg(i);
        QCOMPARE(mergeTags({}, many).size(), kMaxMediaTags);
        QCOMPARE(withoutTag(merged, QStringLiteral(" BEACH")), (QStringList{QStringLiteral("dog"), QStringLiteral("person")}));
    }

    void tagsArePicked()
    {
        // Five frames: person in four (weak), dog once strongly, car once weakly, cat below the floor.
        const QList<QList<LabelHit>> frames = {
            {{QStringLiteral("person"), 0.5}, {QStringLiteral("person"), 0.45}, {QStringLiteral("cat"), 0.2}},
            {{QStringLiteral("person"), 0.4}, {QStringLiteral("dog"), 0.9}},
            {{QStringLiteral("person"), 0.5}},
            {{QStringLiteral("person"), 0.42}, {QStringLiteral("car"), 0.4}},
            {},
        };
        QCOMPARE(pickTags(frames), (QStringList{QStringLiteral("person"), QStringLiteral("dog")}));
        TagPickOptions one;
        one.maxTags = 1;
        QCOMPARE(pickTags(frames, one), QStringList{QStringLiteral("person")});
        // A single sample (a photo) needs a confident label.
        QCOMPARE(pickTags({{{QStringLiteral("cup"), 0.5}, {QStringLiteral("laptop"), 0.8}}}),
                 QStringList{QStringLiteral("laptop")});
    }

    void binSearchMatchesNameAndTags()
    {
        const QStringList tags{QStringLiteral("dog"), QStringLiteral("beach"), QStringLiteral("rock music")};
        QVERIFY(mediaMatchesSearch(QStringLiteral("IMG_0042.mp4"), tags, QString()));
        QVERIFY(mediaMatchesSearch(QStringLiteral("IMG_0042.mp4"), tags, QStringLiteral("   ")));
        QVERIFY(mediaMatchesSearch(QStringLiteral("IMG_0042.mp4"), tags, QStringLiteral("img_00")));
        QVERIFY(mediaMatchesSearch(QStringLiteral("IMG_0042.mp4"), tags, QStringLiteral("Dog")));
        QVERIFY(mediaMatchesSearch(QStringLiteral("IMG_0042.mp4"), tags, QStringLiteral("dog beach")));
        QVERIFY(mediaMatchesSearch(QStringLiteral("IMG_0042.mp4"), tags, QStringLiteral("rock")));
        QVERIFY(mediaMatchesSearch(QStringLiteral("IMG_0042.mp4"), tags, QStringLiteral("0042 music")));
        QVERIFY(!mediaMatchesSearch(QStringLiteral("IMG_0042.mp4"), tags, QStringLiteral("cat")));
        QVERIFY(!mediaMatchesSearch(QStringLiteral("IMG_0042.mp4"), tags, QStringLiteral("dog cat")));
        QVERIFY(!mediaMatchesSearch(QStringLiteral("IMG_0042.mp4"), {}, QStringLiteral("dog")));
    }

    void tagsAreSavedWithTheMedia()
    {
        Project project;
        MediaAsset tagged;
        tagged.id = QStringLiteral("a1");
        tagged.name = QStringLiteral("beach.mp4");
        tagged.path = QStringLiteral("/media/beach.mp4");
        tagged.kind = MediaKind::Video;
        tagged.tags = {QStringLiteral("dog"), QStringLiteral("beach")};
        tagged.tagged = true;
        project.addAsset(tagged);
        MediaAsset looked;
        looked.id = QStringLiteral("a2");
        looked.name = QStringLiteral("blank.png");
        looked.path = QStringLiteral("/media/blank.png");
        looked.kind = MediaKind::Image;
        looked.tagged = true; // the tagger ran and found nothing
        project.addAsset(looked);
        MediaAsset plain;
        plain.id = QStringLiteral("a3");
        plain.name = QStringLiteral("old.wav");
        plain.path = QStringLiteral("/media/old.wav");
        plain.kind = MediaKind::Audio;
        project.addAsset(plain);

        const QJsonObject json = project.toJson();
        QString error;
        const Project loaded = Project::fromJson(json, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QVERIFY(loaded.asset(QStringLiteral("a1")));
        QCOMPARE(loaded.asset(QStringLiteral("a1"))->tags, tagged.tags);
        QVERIFY(loaded.asset(QStringLiteral("a1"))->tagged);
        QVERIFY(loaded.asset(QStringLiteral("a2"))->tags.isEmpty());
        QVERIFY(loaded.asset(QStringLiteral("a2"))->tagged);
        QVERIFY(loaded.asset(QStringLiteral("a3"))->tags.isEmpty());
        QVERIFY(!loaded.asset(QStringLiteral("a3"))->tagged);

        // Media the tagger never saw is written exactly as before: no new keys.
        const QJsonDocument doc(json);
        const QByteArray text = doc.toJson(QJsonDocument::Compact);
        QCOMPARE(text.count("\"tagged\""), 2);
        QCOMPARE(text.count("\"tags\""), 1);

        // And the search finds the loaded tags.
        const MediaAsset *a = loaded.asset(QStringLiteral("a1"));
        QVERIFY(mediaMatchesSearch(a->name, a->tags, QStringLiteral("dog")));
    }
};

QTEST_GUILESS_MAIN(TestAiShorts)
#include "tst_aishorts.moc"
