#pragma once

#include "core/base/Time.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QString>

namespace drift {

// Dafin: a stretch of one source file that agent cuts must not remove — a safety warning, a
// backup step, the moment a button is pressed. Kept in SOURCE time so it survives every move,
// trim, split and reorder of the clips that play it. Saved with the project and part of its
// undoable state, so adding or removing a range is one undo step like any other edit.
struct ProtectedRange
{
    QString assetId;
    TimeUs srcStartUs = 0;
    TimeUs srcEndUs = 0;
    QString reason;

    bool isValid() const { return !assetId.isEmpty() && srcEndUs > srcStartUs; }
    bool operator==(const ProtectedRange &o) const
    {
        return assetId == o.assetId && srcStartUs == o.srcStartUs && srcEndUs == o.srcEndUs
               && reason == o.reason;
    }

    QJsonObject toJson() const
    {
        QJsonObject o{{QStringLiteral("asset"), assetId},
                      {QStringLiteral("srcStartUs"), static_cast<double>(srcStartUs)},
                      {QStringLiteral("srcEndUs"), static_cast<double>(srcEndUs)}};
        if (!reason.isEmpty())
            o.insert(QStringLiteral("reason"), reason);
        return o;
    }
    static ProtectedRange fromJson(const QJsonObject &o)
    {
        ProtectedRange r;
        r.assetId = o.value(QStringLiteral("asset")).toString();
        r.srcStartUs = static_cast<TimeUs>(o.value(QStringLiteral("srcStartUs")).toDouble());
        r.srcEndUs = static_cast<TimeUs>(o.value(QStringLiteral("srcEndUs")).toDouble());
        r.reason = o.value(QStringLiteral("reason")).toString();
        return r;
    }
};

// Dafin: links this project to another edition of the same material (a 9:16 version of a 16:9
// tutorial, say). Matched by the content of the source file and a source range, never by title
// or file name: two tutorials with the same title can be different recordings.
struct VariantLink
{
    QString otherProject;   // path of the other edition's project file
    QString assetId;        // the asset in THIS project the range refers to
    QString contentHash;    // drift::sampledContentHash() of that asset's file when linked
    qint64 contentSize = 0; // its size in bytes, part of the match
    TimeUs srcStartUs = 0;
    TimeUs srcEndUs = 0;

    bool isSet() const { return !otherProject.isEmpty() && !contentHash.isEmpty(); }
    bool operator==(const VariantLink &o) const
    {
        return otherProject == o.otherProject && assetId == o.assetId && contentHash == o.contentHash
               && contentSize == o.contentSize && srcStartUs == o.srcStartUs && srcEndUs == o.srcEndUs;
    }

    QJsonObject toJson() const
    {
        return {{QStringLiteral("otherProject"), otherProject},
                {QStringLiteral("asset"), assetId},
                {QStringLiteral("contentHash"), contentHash},
                {QStringLiteral("contentSize"), static_cast<double>(contentSize)},
                {QStringLiteral("srcStartUs"), static_cast<double>(srcStartUs)},
                {QStringLiteral("srcEndUs"), static_cast<double>(srcEndUs)}};
    }
    static VariantLink fromJson(const QJsonObject &o)
    {
        VariantLink l;
        l.otherProject = o.value(QStringLiteral("otherProject")).toString();
        l.assetId = o.value(QStringLiteral("asset")).toString();
        l.contentHash = o.value(QStringLiteral("contentHash")).toString();
        l.contentSize = static_cast<qint64>(o.value(QStringLiteral("contentSize")).toDouble());
        l.srcStartUs = static_cast<TimeUs>(o.value(QStringLiteral("srcStartUs")).toDouble());
        l.srcEndUs = static_cast<TimeUs>(o.value(QStringLiteral("srcEndUs")).toDouble());
        return l;
    }
};

} // namespace drift
