#pragma once

#include "core/base/Time.h"
#include "core/edit/ShortClipPlan.h"

#include <QList>
#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVector>

class AppController;
class QTimer;

// Dafin: AI › Smart short clips, reached from QML as EditorState.ai.shorts. Everything runs on
// this computer, in the background (JobRegistry's model lane, cancellable, with progress).
//
// Analyze: the selected video clip's media is transcribed when it has no transcript yet (Whisper,
// as Auto captions does; the transcript is kept with the project), its loudness is measured and
// its shots are found (SceneDetect, cached). core/edit/ShortClipPlan then proposes highlights of
// the length setting (15–60 s) built from whole sentences — or whole shots when nothing is said —
// scored on speech energy, scene changes, keywords and a hook at the start.
//
// Create: every checked highlight becomes its own 9:16 project file next to this one ("<project> -
// Short N.drift"), the way Dafin keeps editions: a separate project tied to this one with the
// edition link (Project::variantLink, matched by the source file's content and the source range).
// The clip in it follows the faces (FaceLandmarker sampled 4× a second, smoothed, through the same
// framing maths as the agent's auto_reframe, core/edit/AutoReframe) and falls back to a
// full-height centre crop where there is no face or no face model. Captions from the transcript
// are added when asked for. The open project is not changed; the media is referenced, not copied.
class ShortClipsController : public QObject
{
    Q_OBJECT

    // The media the highlights are found in: the selected video clip on the timeline.
    Q_PROPERTY(bool canAnalyze READ canAnalyze NOTIFY sourceChanged)
    Q_PROPERTY(QString sourceName READ sourceName NOTIFY sourceChanged)
    Q_PROPERTY(QString sourceHint READ sourceHint NOTIFY sourceChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(bool analyzing READ analyzing NOTIFY busyChanged)
    Q_PROPERTY(bool creating READ creating NOTIFY busyChanged)
    Q_PROPERTY(double progress READ progress NOTIFY progressChanged)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    // index, score (0..100), start, end (source s), length (s), lengthLabel, firstWords, text,
    // checked, why (what made the score).
    Q_PROPERTY(QVariantList segments READ segments NOTIFY segmentsChanged)
    Q_PROPERTY(int checkedCount READ checkedCount NOTIFY segmentsChanged)
    // The clip the listed highlights were found in, and whether they came from speech or shots.
    Q_PROPERTY(QString resultName READ resultName NOTIFY segmentsChanged)
    Q_PROPERTY(bool resultFromSpeech READ resultFromSpeech NOTIFY segmentsChanged)
    // Length setting, 15..60 s (kept in settings), and the options for Create.
    Q_PROPERTY(int targetSeconds READ targetSeconds WRITE setTargetSeconds NOTIFY settingsChanged)
    Q_PROPERTY(bool captions READ captions WRITE setCaptions NOTIFY settingsChanged)
    Q_PROPERTY(QString keywords READ keywords WRITE setKeywords NOTIFY settingsChanged)
    // The editions the last Create wrote: name, path.
    Q_PROPERTY(QVariantList created READ created NOTIFY createdChanged)
    // The highlight playing on the timeline, -1 when none.
    Q_PROPERTY(int previewing READ previewing NOTIFY previewingChanged)

public:
    explicit ShortClipsController(AppController *app, QObject *parent = nullptr);

    bool canAnalyze() const { return !m_clipId.isEmpty(); }
    QString sourceName() const { return m_sourceName; }
    QString sourceHint() const { return m_sourceHint; }
    bool busy() const { return !m_jobId.isEmpty(); }
    bool analyzing() const { return busy() && !m_jobIsCreate; }
    bool creating() const { return busy() && m_jobIsCreate; }
    double progress() const { return m_progress; }
    QString status() const { return m_status; }
    QVariantList segments() const;
    int checkedCount() const;
    QString resultName() const { return m_result.name; }
    bool resultFromSpeech() const { return m_result.fromSpeech; }
    int targetSeconds() const { return m_targetSeconds; }
    void setTargetSeconds(int seconds);
    bool captions() const { return m_captions; }
    void setCaptions(bool on);
    QString keywords() const { return m_keywords; }
    void setKeywords(const QString &keywords);
    QVariantList created() const { return m_created; }
    int previewing() const { return m_previewing; }

    // Finds highlights in the selected clip. language: a Whisper code or empty to detect it, used
    // only when the media still has to be transcribed.
    Q_INVOKABLE bool analyze(const QString &language);
    Q_INVOKABLE void cancel();
    Q_INVOKABLE void setChecked(int index, bool checked);
    Q_INVOKABLE void setAllChecked(bool checked);
    // Plays highlight `index` on the timeline from its start and stops at its end; again stops it.
    Q_INVOKABLE void preview(int index);
    Q_INVOKABLE void stopPreview();
    // Writes every checked highlight as a 9:16 edition. False, with a message, when it cannot.
    Q_INVOKABLE bool create();

signals:
    void sourceChanged();
    void busyChanged();
    void progressChanged();
    void statusChanged();
    void segmentsChanged();
    void settingsChanged();
    void createdChanged();
    void previewingChanged();
    void finished(bool ok, const QString &message);

private:
    struct Result
    {
        QString clipId;
        QString assetId;
        QString path;
        QString name;
        bool fromSpeech = false;
        QList<drift::shorts::Segment> segments;
        QVector<bool> checked;
    };

    void refreshSource();
    bool locateResultClip(int *trackIndex, int *clipIndex) const;
    void setProgress(double progress);
    void setStatus(const QString &status);
    void setJob(const QString &id, bool create);
    bool guardAiAllowed();
    void previewTick();

    AppController *m_app = nullptr;
    QString m_clipId;
    QString m_sourceName;
    QString m_sourceHint;
    QString m_jobId;
    bool m_jobIsCreate = false;
    double m_progress = 0.0;
    QString m_status;
    Result m_result;
    int m_targetSeconds = 30;
    bool m_captions = true;
    QString m_keywords;
    QVariantList m_created;
    int m_previewing = -1;
    double m_previewEndSeconds = 0.0;
    double m_previewStartSeconds = 0.0;
    QTimer *m_previewTimer = nullptr;
};
