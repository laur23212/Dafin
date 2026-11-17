#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>

class AppController;
class QTimer;

// Dafin: AI › Media auto-tagging, reached from QML as EditorState.ai.tagging. A background job
// (cancellable, with progress, on the model lane of JobRegistry) looks at media in the bin and
// stores what it finds on each item (MediaAsset::tags), which the media bin search then matches.
//
// Sound: the existing audio tagger (CLAP, plus AST for speech and music when installed) on up to a
// minute from the middle of the file. Pictures: the existing YOLOX-tiny object detector (the
// object.yolox-tiny add-on, Apache-2.0) on five frames spread over a video, or on the image itself.
// Both models are add-ons downloaded on first use. Nothing leaves this computer.
//
// Tags land as one undo step per run, and a run that is cancelled keeps what it had finished.
// "Auto-tag new imports" (off by default, kept in settings) queues media as it arrives in the bin.
class MediaTaggingController : public QObject
{
    Q_OBJECT

    Q_PROPERTY(bool running READ running NOTIFY runningChanged)
    Q_PROPERTY(double progress READ progress NOTIFY progressChanged)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    Q_PROPERTY(bool autoTagImports READ autoTagImports WRITE setAutoTagImports NOTIFY autoTagImportsChanged)
    // Media the tagger can look at (video, images, audio) and has not yet.
    Q_PROPERTY(int untaggedCount READ untaggedCount NOTIFY mediaChanged)
    // Every media item with tags: assetId, name, kind, tags.
    Q_PROPERTY(QVariantList taggedMedia READ taggedMedia NOTIFY mediaChanged)
    // Whether the models for sound / pictures are installed (the runtime too).
    Q_PROPERTY(bool canHear READ canHear NOTIFY modelsChanged)
    Q_PROPERTY(bool canSee READ canSee NOTIFY modelsChanged)

public:
    explicit MediaTaggingController(AppController *app, QObject *parent = nullptr);

    bool running() const { return !m_jobId.isEmpty(); }
    double progress() const { return m_progress; }
    QString status() const { return m_status; }
    bool autoTagImports() const { return m_autoTagImports; }
    void setAutoTagImports(bool on);
    int untaggedCount() const;
    QVariantList taggedMedia() const;
    bool canHear() const;
    bool canSee() const;

    // Every taggable item the tagger has not looked at yet.
    Q_INVOKABLE bool tagUntagged();
    // Everything again, replacing the tags it has.
    Q_INVOKABLE bool tagAll();
    // These items; `again` re-tags ones already looked at. While a run is going they are queued.
    Q_INVOKABLE bool tagAssets(const QStringList &assetIds, bool again);
    Q_INVOKABLE void cancel();
    // One undo step each. False when nothing changed.
    Q_INVOKABLE bool removeTag(const QString &assetId, const QString &tag);
    Q_INVOKABLE bool clearTags(const QStringList &assetIds);
    // The models changed on disk (installed, deleted): re-read canHear / canSee.
    Q_INVOKABLE void refreshModels();

signals:
    void runningChanged();
    void progressChanged();
    void statusChanged();
    void autoTagImportsChanged();
    void mediaChanged();
    void modelsChanged();
    void finished(bool ok, const QString &message);

private:
    bool taggable(const QString &assetId) const;
    bool start(const QStringList &assetIds);
    void onAssetsImported(const QStringList &assetIds);
    void setProgress(double progress);
    void setStatus(const QString &status);

    AppController *m_app = nullptr;
    QString m_jobId;
    double m_progress = 0.0;
    QString m_status;
    bool m_autoTagImports = false;
    // Asked for while a run was going; started when it ends.
    QStringList m_queued;
    // Imports wait a moment, so a drop of many files is one run and their probes have landed.
    QTimer *m_importTimer = nullptr;
    QStringList m_arrived;
    mutable bool m_canHear = false;
    mutable bool m_canSee = false;
};
