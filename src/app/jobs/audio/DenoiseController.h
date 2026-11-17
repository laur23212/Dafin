#pragma once

#include "core/base/Time.h"

#include <QAtomicInt>
#include <QObject>
#include <QString>

class AppController;

// Noise removal (DeepFilterNet3), reached from QML as EditorState.denoise.
class DenoiseController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool running READ running NOTIFY runningChanged)
    Q_PROPERTY(double progress READ progress NOTIFY progressChanged)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)

public:
    explicit DenoiseController(AppController *app, QObject *parent = nullptr);

    bool running() const { return m_running; }
    double progress() const { return m_progress; }
    QString status() const { return m_status; }

    // Noise removal (DeepFilterNet3). preview renders a short window either side of the
    // model — original and cleaned — so the denoise window can A/B them before anything is
    // committed; apply renders the whole clip and adds it to the timeline.
    Q_INVOKABLE bool available();
    Q_INVOKABLE void preview(int trackIndex, int clipIndex, double atSeconds);
    Q_INVOKABLE void apply(int trackIndex, int clipIndex);
    // Dafin, the AI tab: renders the clip's sound (a video's separated audio partner when it has
    // one) and points the clip at the result in place, keeping its timing, volume dots, fades and
    // effects, as one undo step: the same behaviour as Audio tools › Noise reduction. strength
    // 0..1 mixes the cleaned sound with the original.
    Q_INVOKABLE void applyInPlace(int trackIndex, int clipIndex, double strength);
    // A clip the AI tab can clean: one with sound of its own, or a video whose audio partner has it.
    Q_INVOKABLE bool canApplyInPlace(int trackIndex, int clipIndex) const;
    Q_INVOKABLE void cancel();

signals:
    void runningChanged();
    void progressChanged();
    void statusChanged();
    void previewReady(const QString &originalPath, const QString &cleanPath);
    void finished(bool ok, const QString &message);

private:
    void finalize(const QString &clipId, const QString &audioPath);
    // Shared body of the two denoise jobs: decodes [srcIn, srcIn + span) of `path` at the model's
    // rate, runs each channel through it, and writes the result. Runs on a worker thread.
    // `originalPathOut` is written only when non-empty, for the preview's A/B source.
    bool renderAudio(const QString &path, drift::TimeUs srcIn, drift::TimeUs span,
                     const QString &outPath, const QString &originalPath, double progressFrom,
                     double progressTo, QString *errorOut, double strength = 1.0,
                     int audioStreamOrdinal = 0);

    AppController *m_app = nullptr;
    bool m_running = false;
    double m_progress = 0.0;
    QString m_status;
    QAtomicInt m_cancel = 0;
    // The A/B snippets currently on offer. Dragging the preview window along a clip re-renders
    // repeatedly, so each pair is deleted as the next supersedes it.
    QString m_previewClean;
    QString m_previewOriginal;
};
