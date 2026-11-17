#pragma once

#include "core/base/Time.h"
#include "engine/ml/audio/StemSeparation.h"

#include <QAtomicInt>
#include <QFuture>
#include <QObject>
#include <QString>

class AppController;
class QTimer;

// Dafin: AI › Vocal remover, reached from QML as EditorState.vocals.
//
// The sound of the selected clip is split by HTDemucs (engine/ml/audio/DemucsSeparator) into the
// voice and the music (everything else: the mix minus the voice), on this computer. The clip's
// source window is streamed through the model in overlapping 7.8 s segments
// (engine/ml/audio/StemSeparation), so an hour-long clip needs no more memory than a short one,
// and each part is written to a FLAC in Dafin's cache as it is finished.
//
//  - Vocals only / Music only: the clip is pointed at that file, as Audio tools do.
//  - Both: the clip becomes the vocals and a copy of it, pointed at the music, goes on a new audio
//    track right under it, linked to it and starting at the same frame.
// Timing, volume dots, fades and audio effects carry over (the files cover exactly the old source
// window, so speed and reverse stay on the clip). A video still playing its own sound gets it
// separated onto an audio track first and the picture is left alone. One undo step.
class VocalRemoverController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool running READ running NOTIFY runningChanged)
    Q_PROPERTY(double progress READ progress NOTIFY progressChanged)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    // The selection has a clip with sound to separate, and what it is called.
    Q_PROPERTY(bool canSeparate READ canSeparate NOTIFY selectionInfoChanged)
    Q_PROPERTY(QString clipName READ clipName NOTIFY selectionInfoChanged)

public:
    enum Output { VocalsOnly = 0, MusicOnly = 1, Both = 2 };
    Q_ENUM(Output)

    explicit VocalRemoverController(AppController *app, QObject *parent = nullptr);
    ~VocalRemoverController() override;

    bool running() const { return m_running; }
    double progress() const { return m_progress; }
    QString status() const { return m_status; }
    bool canSeparate() const { return m_canSeparate; }
    QString clipName() const { return m_clipName; }

    // The model file and the AI engine are both installed (file checks only).
    Q_INVOKABLE bool modelReady() const;
    Q_INVOKABLE bool canProcess(int trackIndex, int clipIndex) const;
    // The selected clip; `output` is an Output.
    Q_INVOKABLE void separate(int output);
    Q_INVOKABLE void separateClip(int trackIndex, int clipIndex, int output);
    Q_INVOKABLE void cancel();
    // Lets go of the loaded model so Settings can delete its file. False while a run is going.
    Q_INVOKABLE bool releaseModel();

    // Tests: run this stand-in instead of HTDemucs, on segments of `segmentFrames` at `sampleRate`.
    void setModelForTesting(drift::stems::SegmentModel model, int segmentFrames, int sampleRate);

signals:
    void runningChanged();
    void progressChanged();
    void statusChanged();
    void selectionInfoChanged();
    void finished(bool ok, const QString &message);

private:
    void scheduleRefresh();
    void refresh();
    bool begin(const QString &message);
    void end();
    void reportProgress(double fraction, const QString &status = {});
    void finishSeparation(const QString &clipId, drift::TimeUs srcIn, drift::TimeUs srcOut, int output,
                          const QString &vocalsPath, const QString &musicPath);
    void failRun(const QString &message, bool cancelled, const QString &vocalsPath, const QString &musicPath);

    AppController *m_app = nullptr;
    QTimer *m_refreshTimer = nullptr;
    bool m_running = false;
    double m_progress = 0.0;
    QString m_status;
    bool m_canSeparate = false;
    QString m_clipName;
    QAtomicInt m_cancel = 0;
    QFuture<void> m_future;

    drift::stems::SegmentModel m_testModel;
    int m_testSegmentFrames = 0;
    int m_testSampleRate = 0;
};
