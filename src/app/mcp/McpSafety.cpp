// Dafin: agent safety gates — protected source ranges, the link between two editions of the
// same material, and the native confirmation an agent needs before an op that cannot be
// undone. Kept apart from McpController.cpp so the cutting ops only gain a call each.

#include "app/mcp/McpController.h"

#include "app/editor/AppController.h"
#include "app/mcp/McpDetail.h"
#include "core/edit/ProtectedRanges.h"

#include <QEventLoop>
#include <QFileInfo>
#include <QSettings>
#include <QTimer>
#include <QUuid>

using namespace drift::mcp;
using drift::mcpdetail::findClipById;

namespace {

double seconds(drift::TimeUs us)
{
    return drift::usToSeconds(us);
}

QJsonObject rangeReply(const drift::ProtectedRange &r)
{
    QJsonObject o{{QStringLiteral("asset"), r.assetId},
                  {QStringLiteral("start"), seconds(r.srcStartUs)},
                  {QStringLiteral("end"), seconds(r.srcEndUs)}};
    if (!r.reason.isEmpty())
        o.insert(QStringLiteral("reason"), r.reason);
    return o;
}

// Reads set_protected_ranges' `ranges`: each needs `asset` (id) or `clip` (uuid, its asset is
// used), and source-second `start` < `end`.
bool parseRanges(const drift::Project &project, const QJsonArray &in, QList<drift::ProtectedRange> *out,
                 QString *error)
{
    for (const QJsonValue &v : in) {
        const QJsonObject o = v.toObject();
        drift::ProtectedRange r;
        r.assetId = o.value(QStringLiteral("asset")).toString();
        const QString clipId = o.value(QStringLiteral("clip")).toString();
        if (r.assetId.isEmpty() && !clipId.isEmpty()) {
            int t = -1, c = -1;
            if (!findClipById(project, clipId, &t, &c)) {
                *error = QStringLiteral("Unknown clip %1; re-read inspect").arg(clipId);
                return false;
            }
            r.assetId = project.tracks().at(t).clips.at(c).assetId;
        }
        if (r.assetId.isEmpty() || !project.asset(r.assetId)) {
            *error = QStringLiteral("Each range needs an asset id or a clip uuid of media in this project");
            return false;
        }
        const double a = o.value(QStringLiteral("start")).toDouble(-1);
        const double b = o.value(QStringLiteral("end")).toDouble(-1);
        if (a < 0 || b <= a) {
            *error = QStringLiteral("Each range needs start < end, in source seconds");
            return false;
        }
        r.srcStartUs = drift::secondsToUs(a);
        r.srcEndUs = drift::secondsToUs(b);
        r.reason = o.value(QStringLiteral("reason")).toString().left(200);
        out->append(r);
    }
    return true;
}

QJsonObject protectedError(const QList<drift::ProtectedRange> &hit)
{
    QStringList parts;
    QJsonArray list;
    for (const drift::ProtectedRange &r : hit) {
        parts.append(QStringLiteral("%1-%2 s%3")
                         .arg(seconds(r.srcStartUs), 0, 'f', 2)
                         .arg(seconds(r.srcEndUs), 0, 'f', 2)
                         .arg(r.reason.isEmpty() ? QString() : QStringLiteral(" (%1)").arg(r.reason)));
        list.append(rangeReply(r));
    }
    QJsonObject e = err("protected_range",
                        QStringLiteral("This cut would remove protected source footage: %1. Keep those "
                                       "seconds, or ask the user to remove the protection.")
                            .arg(parts.join(QStringLiteral(", "))));
    e.insert(QStringLiteral("protected"), list);
    return e;
}

} // namespace

// ---- Protected ranges -------------------------------------------------------------------------

QJsonObject McpController::setProtectedRanges(const QJsonArray &ranges)
{
    QList<drift::ProtectedRange> parsed;
    QString error;
    if (!parseRanges(m_app->m_project, ranges, &parsed, &error))
        return err("bad_args", error);
    if (parsed == m_app->m_project.protectedRanges())
        return ok({{QStringLiteral("n"), parsed.size()}, {QStringLiteral("unchanged"), true}});
    const drift::Project before = m_app->m_project;
    m_app->m_project.setProtectedRanges(parsed);
    m_app->pushProjectEdit(before, AppController::tr("Protected ranges"));
    m_app->finishEdit(AppController::tr("Protected ranges"));
    return listProtectedRanges({});
}

QJsonObject McpController::listProtectedRanges(const QString &assetId) const
{
    QJsonArray out;
    for (const drift::ProtectedRange &r : m_app->m_project.protectedRanges()) {
        if (assetId.isEmpty() || r.assetId == assetId)
            out.append(rangeReply(r));
    }
    return ok({{QStringLiteral("ranges"), out}, {QStringLiteral("n"), out.size()}});
}

// Called by the cutting ops between their edit and its undo push. On a loss the edit is rolled
// back (nothing was pushed or refreshed yet) and the refusal returned.
QJsonObject McpController::refuseProtectedLoss(const drift::Project &before)
{
    const QList<drift::ProtectedLoss> loss =
        drift::protectedLoss(before.protectedRanges(), before, m_app->m_project);
    if (loss.isEmpty())
        return {};
    m_app->m_project = before;
    QList<drift::ProtectedRange> hit;
    for (const drift::ProtectedLoss &l : loss)
        hit.append(l.range);
    return protectedError(hit);
}

// Planned source cuts of one asset, checked before anything is edited (so dry runs agree).
QJsonObject McpController::refuseProtectedCuts(const QString &assetId,
                                               const QList<drift::TimeRangeUs> &removedSource) const
{
    const QList<drift::ProtectedRange> hit =
        drift::protectedOverlaps(m_app->m_project.protectedRanges(), assetId, removedSource);
    return hit.isEmpty() ? QJsonObject{} : protectedError(hit);
}

// ---- Edition link -----------------------------------------------------------------------------

QJsonObject McpController::linkVariant(const QString &assetId, double srcStart, double srcEnd,
                                       const QString &otherProject, const QString &expectHash,
                                       double expectStart, double expectEnd)
{
    const drift::MediaAsset *asset = m_app->m_project.asset(assetId);
    if (!asset)
        return err("not_found", QStringLiteral("Unknown asset; use an id from list_assets or inspect"));
    if (srcStart < 0 || srcEnd <= srcStart)
        return err("bad_args", QStringLiteral("start < end required, in source seconds"));
    if (otherProject.trimmed().isEmpty())
        return err("bad_args", QStringLiteral("other_project (path of the other edition's project) required"));
    qint64 size = 0;
    const QString hash = drift::sampledContentHash(asset->path, &size);
    if (hash.isEmpty())
        return err("not_found", QStringLiteral("Can't read the asset's file to identify it"));
    // The other edition's values, read there with get_variant_link: a different file is refused
    // whatever it is called, and so is a range that does not overlap.
    if (!expectHash.isEmpty() && expectHash != hash)
        return err("source_mismatch",
                   QStringLiteral("This asset is a different file from the other edition's source "
                                  "(content differs). Same titles do not make the same recording."));
    if (expectEnd > expectStart && qMin(srcEnd, expectEnd) - qMax(srcStart, expectStart) <= 0)
        return err("range_mismatch",
                   QStringLiteral("The source ranges of the two editions do not overlap"));

    drift::VariantLink link;
    link.otherProject = otherProject.trimmed();
    link.assetId = assetId;
    link.contentHash = hash;
    link.contentSize = size;
    link.srcStartUs = drift::secondsToUs(srcStart);
    link.srcEndUs = drift::secondsToUs(srcEnd);
    if (!(link == m_app->m_project.variantLink())) {
        const drift::Project before = m_app->m_project;
        m_app->m_project.setVariantLink(link);
        m_app->pushProjectEdit(before, AppController::tr("Link edition"));
        m_app->finishEdit(AppController::tr("Link edition"));
    }
    return getVariantLink();
}

QJsonObject McpController::getVariantLink() const
{
    const drift::VariantLink &l = m_app->m_project.variantLink();
    if (!l.isSet())
        return ok({{QStringLiteral("linked"), false}});
    const drift::MediaAsset *asset = m_app->m_project.asset(l.assetId);
    // Says whether the file on disk is still the one that was linked.
    const bool current = asset && drift::sampledContentHash(asset->path) == l.contentHash;
    return ok({{QStringLiteral("linked"), true},
               {QStringLiteral("other_project"), l.otherProject},
               {QStringLiteral("asset"), l.assetId},
               {QStringLiteral("hash"), l.contentHash},
               {QStringLiteral("size"), static_cast<double>(l.contentSize)},
               {QStringLiteral("start"), seconds(l.srcStartUs)},
               {QStringLiteral("end"), seconds(l.srcEndUs)},
               {QStringLiteral("source_unchanged"), current}});
}

QJsonObject McpController::clearVariantLink()
{
    if (!m_app->m_project.variantLink().isSet())
        return ok({{QStringLiteral("linked"), false}});
    const drift::Project before = m_app->m_project;
    m_app->m_project.setVariantLink({});
    m_app->pushProjectEdit(before, AppController::tr("Unlink edition"));
    m_app->finishEdit(AppController::tr("Unlink edition"));
    return ok({{QStringLiteral("linked"), false}});
}

// ---- Confirm gate -----------------------------------------------------------------------------

void McpController::setConfirmIrreversible(bool enabled)
{
    if (m_confirmIrreversible == enabled)
        return;
    m_confirmIrreversible = enabled;
    QSettings().setValue(QStringLiteral("mcp/confirmIrreversible"), enabled);
    emit confirmIrreversibleChanged();
}

QString McpController::confirmReason(const QString &tool, const QJsonObject &args) const
{
    if (tool == QLatin1String("set_beat_layers"))
        return tr("Change your own snapping settings. This cannot be undone.");
    if (tool == QLatin1String("restore_snapshot"))
        return tr("Replace the whole project with a saved snapshot. This is outside the undo history.");
    if (tool == QLatin1String("reset_shortcuts"))
        return tr("Reset all your keyboard shortcuts. This cannot be undone.");
    if (tool == QLatin1String("import_theme") && args.value(QStringLiteral("replace")).toBool())
        return tr("Replace an installed theme with “%1”. This cannot be undone.")
            .arg(QFileInfo(args.value(QStringLiteral("path")).toString()).fileName());
    if (tool == QLatin1String("export_video")) {
        const QString path = args.value(QStringLiteral("path")).toString();
        if (!path.isEmpty() && QFileInfo::exists(path))
            return tr("Overwrite the existing file “%1”.").arg(QFileInfo(path).fileName());
    }
    if (tool == QLatin1String("set_protected_ranges")) {
        QList<drift::ProtectedRange> next;
        QString error;
        if (!parseRanges(m_app->m_project, args.value(QStringLiteral("ranges")).toArray(), &next, &error))
            return {}; // the op itself reports bad_args
        int removed = 0;
        for (const drift::ProtectedRange &r : m_app->m_project.protectedRanges())
            if (!next.contains(r))
                ++removed;
        if (removed > 0)
            return tr("Remove or change %n protected range(s), so agent cuts may remove that footage.",
                      nullptr, removed);
    }
    return {};
}

QJsonObject McpController::confirmGate(const QString &tool, const QJsonObject &args)
{
    if (!m_confirmIrreversible || !m_confirmUiRegistered)
        return {};
    const QString reason = confirmReason(tool, args);
    if (reason.isEmpty())
        return {};
    if (!m_pendingConfirmId.isEmpty())
        return err("busy", QStringLiteral("Another action is already waiting for the user's answer"));

    const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    m_pendingConfirmId = id;
    m_confirmAnswer = ConfirmAnswer::Pending;
    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    connect(this, &McpController::confirmationAnswered, &loop, &QEventLoop::quit);
    emit confirmationRequested(id, tool, reason);
    if (m_confirmAnswer == ConfirmAnswer::Pending) {
        timer.start(m_confirmTimeoutMs);
        loop.exec();
    }
    const ConfirmAnswer answer = m_confirmAnswer;
    m_pendingConfirmId.clear();
    if (answer == ConfirmAnswer::Pending)
        emit confirmationClosed(id);
    if (answer == ConfirmAnswer::Allowed)
        return {};
    return err("not_confirmed",
               answer == ConfirmAnswer::Denied
                   ? QStringLiteral("The user declined: %1").arg(reason)
                   : QStringLiteral("No answer from the user in time: %1").arg(reason));
}

void McpController::answerConfirmation(const QString &requestId, bool allow)
{
    if (requestId.isEmpty() || requestId != m_pendingConfirmId || m_confirmAnswer != ConfirmAnswer::Pending)
        return; // stale, replayed or unknown request
    m_confirmAnswer = allow ? ConfirmAnswer::Allowed : ConfirmAnswer::Denied;
    emit confirmationAnswered();
}
