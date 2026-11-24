#pragma once

#include <QJsonObject>
#include <QList>
#include <QMutex>
#include <QString>

#include <optional>

namespace drift::scene {

// Dafin: the Scene Titles the library lists (Titles › Scenes): the built-in ports (":/scene-titles",
// from tools/scene-ref/scenes) and the files the user installed (<AppData>/scene-titles, one file
// per scene id, a newer install replacing the older). A file that does not load is skipped here;
// installing checks it first and reports why (AppController::checkSceneTitleFile).
struct SceneEntry
{
    QString id, label, category;
    QString origin; // "builtin" or "installed"
    QString path;
    QJsonObject document;
};

class SceneCatalog
{
public:
    static SceneCatalog &instance();
    static QString installedDir();

    QList<SceneEntry> entries() const;
    std::optional<SceneEntry> find(const QString &id) const;
    void reload();

private:
    void ensureLoaded() const;
    mutable QMutex m_mutex;
    mutable bool m_loaded = false;
    mutable QList<SceneEntry> m_entries;
};

} // namespace drift::scene
