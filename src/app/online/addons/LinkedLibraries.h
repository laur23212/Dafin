#pragma once

#include "engine/packages/linked/FilmoraLibraryImporter.h"

#include <QFutureWatcher>
#include <QHash>
#include <QObject>
#include <QStringList>
#include <QUrl>
#include <QVariantList>

#include <atomic>
#include <memory>

class AddonManager;
class QOffscreenSurface;

// Dafin: Settings › Asset Providers › Effect library folders. Folders of effects downloaded by
// Filmora (Filter, Overlay, transition, Element, resFont, title ... each item a folder with a
// resinfo.json) that Dafin reads and shows as its own: every usable item is converted once into
// a Dafin package (FilmoraLibraryImporter) and appears in Effects, Transitions, Stickers and the
// font list under its category, like a built-in one. The folders are remembered in settings and
// stay where they are (big media plays from there); the converted copies live in
// <AppData>/linked-libraries. Each folder is converted in the background when it is added, at
// start-up (only items that changed) and on Rescan. Every converted shader is compiled once on
// this computer's graphics driver first, so one that cannot run here is listed as skipped rather
// than shown as an effect that draws nothing.
class LinkedLibraries : public QObject
{
    Q_OBJECT
    // One map per folder: path, key, missing, scanning, scanned (a scan finished this session),
    // effects, transitions, stickers, fonts, looks, skipped, found, error, note.
    Q_PROPERTY(QVariantList folders READ folders NOTIFY foldersChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)

public:
    explicit LinkedLibraries(AddonManager *addons, QObject *parent = nullptr);
    ~LinkedLibraries() override;

    QVariantList folders() const;
    bool busy() const { return !m_current.isEmpty(); }

    // Empty on success, else why not, in plain words. Refused: not a local folder, a whole drive,
    // Dafin's own data folder, a folder already linked or inside one. A folder that holds linked
    // folders replaces them (it covers them).
    Q_INVOKABLE QString addFolder(const QUrl &url);
    // Unlinks the folder and removes what was converted from it. The folder itself is untouched.
    Q_INVOKABLE void removeFolder(const QString &path);
    // Converts what changed in every linked folder.
    Q_INVOKABLE void rescan();
    // The items in one folder Dafin could not use (or only partly): [{name, reason}].
    Q_INVOKABLE QVariantList skippedItems(const QString &path) const;

    // At start-up, from main(): catch up with what changed while Dafin was closed.
    void start();

signals:
    void foldersChanged();
    void busyChanged();
    // A scan finished: how many items of the folder are now usable in Dafin.
    void scanFinished(const QString &path, int imported, int skipped, const QString &error);

private:
    void queue(const QString &path);
    void runNext();
    void finished();
    void ensureSurface();

    AddonManager *m_addons = nullptr;
    QStringList m_paths;
    QHash<QString, dafin::linked::ImportReport> m_reports;
    QStringList m_queue;
    QString m_current;
    QStringList m_removeAfterScan; // output folders to delete once the scan using them stops
    std::unique_ptr<QOffscreenSurface> m_surface;
    std::shared_ptr<std::atomic_bool> m_cancel;
    QFutureWatcher<dafin::linked::ImportReport> m_watcher;
};
