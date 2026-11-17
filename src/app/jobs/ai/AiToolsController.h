#pragma once

#include "core/base/Time.h"
#include "core/edit/CutOps.h"
#include "core/project/Transcript.h"

#include <QHash>

#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

class AppController;
class QTimer;

// Dafin: the back end of the assets panel's AI category, reached from QML as EditorState.ai.
// Every tool there runs on this computer; its models are add-ons downloaded on first use.
//
// Auto captions: the media behind each selected clip is transcribed once, word by word (Whisper,
// the CTC aligner when one is installed for the language, Silero VAD when installed). The
// transcript is kept with the project and the caption clip is built from it, so the cues land on
// the words and captioning the same media again is instant. The transcript of the clip in focus
// is listed for reading and correcting; a correction is saved with the project and rewrites the
// captions made from those words in one undo step. (Transcripts sit outside undo, as everywhere in
// Drift, so a line entered again brings undone captions back in line with it.)
//
// Text-based editing: the same transcript word by word. Deleting words cuts those source ranges
// out of the clip and its linked clips, closes the gaps with the 30 ms de-click of cut_words and
// moves the captions made from that media along, as one undo step. Protected ranges are refused.
// Silence & filler words: Silence remover's detection (AudioToolsController) plus the words of a
// per-language filler list (QSettings ai/fillerWords/<lang>) found in the transcript; the hits are
// listed with a checkbox each and Remove cuts the checked ones through the same path.
// The transcript in focus is the selected clip's take: every clip on its track that plays the same
// media, so the words stay listed after a cut has split the clip into pieces.
class AiToolsController : public QObject
{
    Q_OBJECT

    // A selected video or audio clip with sound, so Create captions has something to work on.
    Q_PROPERTY(bool canCaption READ canCaption NOTIFY canCaptionChanged)
    Q_PROPERTY(bool captioning READ captioning NOTIFY captioningChanged)
    Q_PROPERTY(double captionProgress READ captionProgress NOTIFY captionProgressChanged)
    Q_PROPERTY(QString captionStatus READ captionStatus NOTIFY captionStatusChanged)
    // The media clip whose transcript is listed: the selected video/audio clip, else the one the
    // last captions were made from. transcriptInfo: clipId, clipName, hasTranscript, language,
    // aligned (measured word times), words. transcriptLines: text, start, end (timeline seconds),
    // time (a label), speaker.
    Q_PROPERTY(QString transcriptClipId READ transcriptClipId NOTIFY transcriptChanged)
    Q_PROPERTY(QVariantMap transcriptInfo READ transcriptInfo NOTIFY transcriptChanged)
    Q_PROPERTY(QVariantList transcriptLines READ transcriptLines NOTIFY transcriptChanged)
    // Set by the panel while it shows the transcript; the lines are only worked out then.
    Q_PROPERTY(bool transcriptVisible READ transcriptVisible WRITE setTranscriptVisible
                   NOTIFY transcriptVisibleChanged)
    // Dafin: the same transcript word by word, in timeline order: i (index into the transcript),
    // clip (the clip that plays it), text, start, end (timeline seconds), line (index into
    // transcriptLines). Each line row also carries firstRow and rowCount into this list.
    Q_PROPERTY(QVariantList transcriptWords READ transcriptWords NOTIFY transcriptChanged)
    // True while the speech run in progress only transcribes (Text-based editing) and makes no
    // captions; captioning, captionProgress and captionStatus describe it as for captions.
    Q_PROPERTY(bool transcribeOnly READ transcribeOnly NOTIFY captioningChanged)
    // Silence & filler words: what Find found, waiting for Remove. kind ("silence" or "filler"),
    // text, time (a label), start, end (timeline seconds), seconds (what removing it saves).
    Q_PROPERTY(QVariantList cleanupHits READ cleanupHits NOTIFY cleanupChanged)
    Q_PROPERTY(QString cleanupClipId READ cleanupClipId NOTIFY cleanupChanged)
    Q_PROPERTY(bool cleanupFound READ cleanupFound NOTIFY cleanupChanged)
    Q_PROPERTY(bool cleanupBusy READ cleanupBusy NOTIFY cleanupBusyChanged)
    Q_PROPERTY(QString cleanupStatus READ cleanupStatus NOTIFY cleanupBusyChanged)

public:
    explicit AiToolsController(AppController *app, QObject *parent = nullptr);

    bool transcriptVisible() const { return m_transcriptVisible; }
    void setTranscriptVisible(bool visible);
    bool canCaption() const { return m_canCaption; }
    bool captioning() const { return m_captioning; }
    double captionProgress() const { return m_captionProgress; }
    QString captionStatus() const { return m_captionStatus; }
    QString transcriptClipId() const { return m_targetClipId; }
    QVariantMap transcriptInfo() const { return m_info; }
    QVariantList transcriptLines() const { return m_lineRows; }
    QVariantList transcriptWords() const { return m_wordRows; }
    bool transcribeOnly() const { return m_captioning && !m_runMakesCaptions; }
    QVariantList cleanupHits() const { return m_cleanupRows; }
    QString cleanupClipId() const { return m_cleanupClipId; }
    bool cleanupFound() const { return m_cleanupFound; }
    bool cleanupBusy() const { return m_cleanupBusy; }
    QString cleanupStatus() const { return m_cleanupStatus; }

    // Transcribes whatever the selected video/audio clips still need, then adds one caption clip
    // for them. language: a Whisper code, empty to detect it; a code other than the one a stored
    // transcript was made in transcribes again. maxWordsPerCue: 0 packs by line width. False, with
    // a message, when the selection cannot be captioned.
    Q_INVOKABLE bool createCaptions(const QString &language, int maxWordsPerCue);
    Q_INVOKABLE void cancelCaptions();

    // Replaces the words of transcript line `line` with `text`; an empty text removes them. The
    // captions made from those words are rewritten to match. False when nothing changed.
    Q_INVOKABLE bool setLineText(int line, const QString &text);
    // Puts the playhead where the line starts in the clip.
    Q_INVOKABLE void seekToLine(int line);

    // ---- Text-based editing -------------------------------------------------------------------
    // Transcribes the media of the selected clips like Create captions, but makes no caption clip.
    Q_INVOKABLE bool transcribe(const QString &language);
    // Cuts the words `words` ([{clip, i}], i into the transcript, clip the id that plays it, as in
    // transcriptWords) out of the timeline: see sourceCutsForWords. One undo step. False, with a
    // message, when nothing was cut (protected footage, a stale list, no words).
    Q_INVOKABLE bool cutWords(const QVariantList &words);
    // Puts the playhead on a word (timeline seconds of transcriptWords).
    Q_INVOKABLE void seekToWord(int row);

    // ---- Silence & filler words --------------------------------------------------------------
    // The filler list for `language` as [{text, enabled}]: the user's, else the one Dafin ships.
    Q_INVOKABLE QVariantList fillerWords(const QString &language) const;
    Q_INVOKABLE void setFillerWords(const QString &language, const QVariantList &rows);
    Q_INVOKABLE void resetFillerWords(const QString &language);
    // The language the filler list should start on: the transcript's, else "en".
    Q_INVOKABLE QString fillerLanguage() const;
    // Finds silences (Silence remover's settings) and/or filler words in the take in focus. Fillers
    // are found at once; silences on a worker, one clip after another (cleanupBusy). Fills
    // cleanupHits; nothing is cut yet.
    Q_INVOKABLE bool findCleanup(bool silences, bool fillers, double thresholdDb, double minSeconds,
                                 double paddingSeconds, const QString &language);
    Q_INVOKABLE void cancelCleanup();
    Q_INVOKABLE void clearCleanup();
    // Seconds removing the hits `rows` saves, overlaps counted once.
    Q_INVOKABLE double cleanupSeconds(const QVariantList &rows) const;
    // Plays hit `row` with a little around it.
    Q_INVOKABLE void playCleanupHit(int row);
    // Cuts the hits `rows` (indexes into cleanupHits), one undo step.
    Q_INVOKABLE bool removeCleanupHits(const QVariantList &rows);

    // Frees the speech models a caption run loaded (about 1 GB of memory), so Settings can delete
    // their files: Windows will not remove a file a loaded model still holds. False, and nothing
    // freed, while anything is still listening.
    Q_INVOKABLE bool releaseSpeechModels();

signals:
    void canCaptionChanged();
    void captioningChanged();
    void captionProgressChanged();
    void captionStatusChanged();
    void transcriptChanged();
    void transcriptVisibleChanged();
    void cleanupChanged();
    void cleanupBusyChanged();
    // A caption run ended: ok when the caption clip was added.
    void captionsFinished(bool ok, const QString &message);

private:
    struct CleanupHit
    {
        QString kind;
        QString clipId;
        drift::TimeRangeUs source; // what Remove cuts, in the asset's source µs
        drift::TimeRangeUs timeline;
        QString text;
    };
    struct TakeClip
    {
        QString id;
        drift::TimeUs startUs = 0;
        drift::TimeUs durationUs = 0;
    };

    bool startRun(const QString &language, int maxWordsPerCue, bool makeCaptions);
    bool aiAllowed() const;
    // The clips of the take in focus (m_targetClipId): see takeOf in the .cpp.
    QList<drift::Clip> focusTake() const;
    // Cuts source ranges out of the clips they name, back to front, as one undo step `editName`:
    // linked clips, ripple, 30 ms de-click, captions follow; protected footage refuses it all.
    bool applySourceCuts(const QHash<QString, QList<drift::TimeRangeUs>> &cuts, const QString &editName,
                         drift::TimeUs *removedUs);
    void setCleanupBusy(bool busy, const QString &status = {});
    void publishCleanup();
    void findNextSilence();
    void silenceFound(bool ok, const QString &message);

    struct Line
    {
        int firstWord = -1;
        int lastWord = -1;
        drift::TimeUs sourceStartUs = 0;
        drift::TimeUs sourceEndUs = 0;
        drift::TimeUs timelineStartUs = 0;
        // As listed, to notice the transcript changed under the line before an edit lands.
        QString text;
    };

    void scheduleRefresh();
    void refresh();
    void setCaptioning(bool captioning);
    void setCaptionProgress(double progress);
    void setCaptionStatus(const QString &status);
    void reportJobProgress(int index, double fraction, const QString &status);
    void jobDone(const QString &jobId, bool ok, const QString &code, const QString &message);
    void finishCaptions();
    void endCaptionRun(bool ok, const QString &message);
    void storeTranscript(const QString &assetId, std::shared_ptr<drift::Transcript> transcript);
    // Rewrites, from the transcript, the text of every caption tied to `sourceKey` whose words lie
    // over the source span [startUs, endUs). True when a caption changed (one undo step).
    bool syncCaptions(const QString &sourceKey, const drift::Transcript &transcript,
                      drift::TimeUs startUs, drift::TimeUs endUs);

    AppController *m_app = nullptr;
    QTimer *m_refreshTimer = nullptr;
    bool m_transcriptVisible = false;

    bool m_canCaption = false;
    bool m_captioning = false;
    double m_captionProgress = 0.0;
    QString m_captionStatus;

    // The run in progress: its jobs, the clips to caption when they are done, and the settings.
    QStringList m_jobIds;
    int m_jobCount = 0;
    int m_jobsDone = 0;
    QString m_runError;
    bool m_runCancelled = false;
    QStringList m_runClipIds;
    QString m_runLanguage;
    int m_runWordsPerCue = 0;

    QString m_lastCaptionedClipId;
    QString m_targetClipId;
    QString m_targetAssetId;
    QString m_targetSourceKey;
    QList<Line> m_lines;
    QVariantMap m_info;
    QVariantList m_lineRows;
    QVariantList m_wordRows;
    bool m_runMakesCaptions = true;

    // Silence & filler words.
    QList<CleanupHit> m_cleanupHits;
    QVariantList m_cleanupRows;
    QString m_cleanupClipId;
    QList<TakeClip> m_cleanupTake;
    bool m_cleanupFound = false;
    bool m_cleanupBusy = false;
    QString m_cleanupStatus;
    QStringList m_silenceQueue;
    QString m_silenceClipId;
    double m_silenceThreshold = -40.0;
    double m_silenceMin = 0.6;
    double m_silencePadding = 0.1;
    QMetaObject::Connection m_silenceConnection;
    QTimer *m_hitStopTimer = nullptr;
};
