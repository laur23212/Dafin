#pragma once

#include <QJsonObject>
#include <QString>

namespace drift {

// Dafin: what a Scene Title clip (ClipType::Scene, docs/scene-titles.md) plays. The scene file
// itself is embedded, so a project opens on any computer and never changes when the library's
// copy is updated; `params` holds only the values the creator changed (param id -> JSON value),
// the rest come from the document's defaults. Loading, timing and drawing live in core/scene and
// engine/render2d/scene; this struct is only what is saved.
struct SceneSource
{
    QJsonObject document;
    QJsonObject params;

    bool isEmpty() const { return document.isEmpty(); }
    QString sceneId() const { return document.value(QStringLiteral("id")).toString(); }

    QJsonObject toJson() const
    {
        return {{QStringLiteral("document"), document}, {QStringLiteral("params"), params}};
    }
    static SceneSource fromJson(const QJsonObject &o)
    {
        SceneSource s;
        s.document = o.value(QStringLiteral("document")).toObject();
        s.params = o.value(QStringLiteral("params")).toObject();
        return s;
    }
    bool operator==(const SceneSource &other) const
    {
        return document == other.document && params == other.params;
    }
};

} // namespace drift
