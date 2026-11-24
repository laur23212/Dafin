#include "core/scene/SceneCatalog.h"

#include "core/scene/SceneTitle.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QStandardPaths>

namespace drift::scene {

SceneCatalog &SceneCatalog::instance()
{
    static SceneCatalog catalog;
    return catalog;
}

QString SceneCatalog::installedDir()
{
    const QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    return base.isEmpty() ? QString() : QDir(base).filePath(QStringLiteral("scene-titles"));
}

void SceneCatalog::ensureLoaded() const
{
    if (m_loaded)
        return;
    m_loaded = true;
    m_entries.clear();
    const auto loadDir = [this](const QString &dir, const QString &origin) {
        const QStringList files = QDir(dir).entryList({QStringLiteral("*.json")}, QDir::Files, QDir::Name);
        for (const QString &name : files) {
            QFile f(QDir(dir).filePath(name));
            if (!f.open(QIODevice::ReadOnly))
                continue;
            const QJsonObject doc = QJsonDocument::fromJson(f.readAll()).object();
            if (!loadScene(doc))
                continue;
            SceneEntry e;
            e.id = doc.value(QStringLiteral("id")).toString();
            e.label = doc.value(QStringLiteral("label")).toString(e.id);
            e.category = doc.value(QStringLiteral("category")).toString();
            e.origin = origin;
            e.path = f.fileName();
            e.document = doc;
            // An installed copy of a built-in id replaces it, like an update would.
            for (int i = 0; i < m_entries.size(); ++i)
                if (m_entries.at(i).id == e.id)
                    m_entries.removeAt(i--);
            m_entries.append(e);
        }
    };
    loadDir(QStringLiteral(":/scene-titles"), QStringLiteral("builtin"));
    if (const QString dir = installedDir(); !dir.isEmpty())
        loadDir(dir, QStringLiteral("installed"));
}

QList<SceneEntry> SceneCatalog::entries() const
{
    QMutexLocker lock(&m_mutex);
    ensureLoaded();
    return m_entries;
}

std::optional<SceneEntry> SceneCatalog::find(const QString &id) const
{
    QMutexLocker lock(&m_mutex);
    ensureLoaded();
    for (const SceneEntry &e : std::as_const(m_entries))
        if (e.id == id)
            return e;
    return std::nullopt;
}

void SceneCatalog::reload()
{
    QMutexLocker lock(&m_mutex);
    m_loaded = false;
}

} // namespace drift::scene
