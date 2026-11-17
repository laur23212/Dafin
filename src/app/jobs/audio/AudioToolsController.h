#pragma once

#include "core/base/Time.h"

#include <QAtomicInt>
#include <QFuture>
#include <QObject>
#include <QString>
#include <QVariantList>

class AppController;

// Dafin's three local audio tools, all FFmpeg filters and no AI model, reached from QML as
// EditorState.audioTools:
//  - Loudness: two-pass loudnorm to a target (-14 LUFS for online video…).
//  - Noise reduction: afftdn with a strength.
//  - Silence remover: silencedetect finds the quiet stretches of the clip as it plays, the
//    Properties panel shows them, and Apply cuts them out of the clip and its linked partners and
//    closes the gaps, as one undo step.
// Loudness and noise reduction render the clip's audio to a new file in Dafin's cache and point the
// clip at it (a video's sound is separated onto an audio track first), also as one undo step.
class AudioToolsController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool running READ running NOTIFY runningChanged)
    Q_PROPERTY(double progress READ progress NOTIFY progressChanged)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    // What Silence remover found, waiting for Apply: [{start, end}] in seconds from the clip's
    // start on the timeline, plus its clip id and how much it removes.
    Q_PROPERTY(QVariantList silencePreview READ silencePreview NOTIFY silencePreviewChanged)
    Q_PROPERTY(QString silencePreviewClipId READ silencePreviewClipId NOTIFY silencePreviewChanged)
    Q_PROPERTY(double silencePreviewSeconds READ silencePreviewSeconds NOTIFY silencePreviewChanged)
    Q_PROPERTY(double silencePreviewClipSeconds READ silencePreviewClipSeconds NOTIFY silencePreviewChanged)

public:
    explicit AudioToolsController(AppController *app, QObject *parent = nullptr);
    ~AudioToolsController() override;

    bool running() const { return m_running; }
    double progress() const { return m_progress; }
    QString status() const { return m_status; }
    QVariantList silencePreview() const { return m_preview; }
    QString silencePreviewClipId() const { return m_previewClipId; }
    double silencePreviewSeconds() const;
    double silencePreviewClipSeconds() const { return drift::usToSeconds(m_previewDurationUs); }

    // True for a clip with sound of its own, or a video whose separated audio partner has it.
    Q_INVOKABLE bool canProcess(int trackIndex, int clipIndex) const;
    // The id the silence preview is keyed by for this clip (the clip whose sound is analysed).
    Q_INVOKABLE QString clipIdAt(int trackIndex, int clipIndex) const;

    Q_INVOKABLE void normalizeLoudness(int trackIndex, int clipIndex, double targetLufs);
    Q_INVOKABLE void reduceNoise(int trackIndex, int clipIndex, double strength);

    // Silence below thresholdDb for at least minSeconds; paddingSeconds of it is kept either side
    // of the sound so words aren't clipped. Fills silencePreview; nothing is cut yet.
    Q_INVOKABLE void findSilence(int trackIndex, int clipIndex, double thresholdDb, double minSeconds,
                                 double paddingSeconds);
    // Puts the playhead just before silent part `index`, to hear what would be cut.
    Q_INVOKABLE void seekToSilence(int index);
    Q_INVOKABLE void applySilenceRemoval();
    Q_INVOKABLE void clearSilencePreview();

    Q_INVOKABLE void cancel();

    // The audio a tool should render for the clip at (trackIndex, clipIndex): the clip itself, or
    // a video's separated audio partner. clipId empty when the clip has no usable sound.
    struct AudibleSource
    {
        QString clipId;
        QString path;
        int audioStreamIndex = 0;
        drift::TimeUs srcIn = 0;
        drift::TimeUs srcOut = 0;
    };
    AudibleSource audibleSourceAt(int trackIndex, int clipIndex) const;
    // Points clip `clipId` at `audioPath`, a render of exactly its old source window
    // [srcIn, srcOut), keeping its timing, link, volume dots, fades and effects; a video still
    // playing its own sound has it separated onto an audio track first. One undo step labelled
    // `undoLabel`; `nameSuffix` is added to the clip's name once. Returns an empty string on
    // success, else the reason (the file is then deleted and the project left as it was). Shared
    // by Reduce noise here and the AI tab's noise removal (DenoiseController).
    QString replaceClipAudio(const QString &clipId, const QString &audioPath, drift::TimeUs srcIn,
                             drift::TimeUs srcOut, const QString &undoLabel,
                             const QString &nameSuffix, const QString &doneText);

signals:
    void runningChanged();
    void progressChanged();
    void statusChanged();
    void silencePreviewChanged();
    void finished(bool ok, const QString &message);

private:
    enum class RenderKind { Loudness, Noise };
    void startRender(int trackIndex, int clipIndex, RenderKind kind, double amount);
    void finishRender(const QString &clipId, const QString &audioPath, drift::TimeUs srcIn,
                      drift::TimeUs srcOut, RenderKind kind);
    bool begin(const QString &message);
    void end();
    void reportProgress(double fraction, const QString &status = {});

    AppController *m_app = nullptr;
    bool m_running = false;
    double m_progress = 0.0;
    QString m_status;
    QAtomicInt m_cancel = 0;
    QFuture<void> m_future;

    QVariantList m_preview;
    QString m_previewClipId;
    drift::TimeUs m_previewStartUs = 0;
    drift::TimeUs m_previewDurationUs = 0;
};
