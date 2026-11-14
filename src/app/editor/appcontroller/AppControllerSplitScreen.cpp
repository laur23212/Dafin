// Dafin: split screen templates (Templates › Split Screen). The thirty layouts, their cells and
// their entrance are in core/edit/SplitScreen.h; this file turns them into clips.
//
// A split screen dropped on the timeline is a Composite clip (Clip::splitScreen set) whose nested
// timeline has one track per cell (SplitScreenInfo::cellTrackIds, in cell order). Each cell track
// holds one clip — a grey placeholder Shape until a video or picture goes in — cut to its cell by a
// Freeform mask pinned to it. The mask's path keys unfold the cell in (the entrance, as Filmora's
// cards play it), and the cells stand `borderPx` apart over a fill of the border colour on the
// bottom track, which fades in once the cells are whole. Double-click opens the composite, which is
// Filmora's "Advanced Split Screen Edit": one row per cell, to trim, slip, add effects or titles.
//
// Every edit runs on a scratch project holding the cell tracks (editSplitScreen) and goes back as
// one undo step. A filled cell keeps its clip's id, so the mask and any effects pinned to the cell
// stay with it, and media the user moved inside its cell stays where it was put unless the cell
// itself changes shape (a new layout, new media).

#include "app/editor/appcontroller/AppControllerInternal.h"
#include "app/bin/MediaBinController.h"
#include "core/edit/SplitScreen.h"
#include "core/edit/TimelineOps.h"
#include "core/shapes/ShapeStyle.h"

#include <QColor>
#include <QPolygonF>
#include <QSet>
#include <QUuid>

#include <algorithm>
#include <cmath>

using namespace drift::appdetail;
using drift::mcpdetail::findClipById;
using namespace drift::appcontroller_detail;

namespace {

constexpr drift::TimeUs kBorderFadeUs = 200000;
constexpr double kMaxBorderPx = 60.0;

QString newId()
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

QString layoutLabel(const drift::SplitLayoutDef &layout)
{
    if (layout.number > 0)
        return AppController::tr("Split Screen %1").arg(layout.number);
    return AppController::tr(layout.label.toUtf8().constData());
}

QVariantList pointsToVariant(const QVector<QPointF> &points)
{
    QVariantList out;
    for (const QPointF &p : points)
        out.append(QVariantMap{{QStringLiteral("x"), p.x()}, {QStringLiteral("y"), p.y()}});
    return out;
}

QRectF polygonBox(const QVector<QPointF> &points)
{
    return QPolygonF(points).boundingRect();
}

// True when the polygon is its own bounding box (a plain rectangle).
bool isAxisBox(const QVector<QPointF> &points, const QRectF &box)
{
    if (points.size() != 4)
        return false;
    for (const QPointF &p : points) {
        const bool onX = std::abs(p.x() - box.left()) < 1e-6 || std::abs(p.x() - box.right()) < 1e-6;
        const bool onY = std::abs(p.y() - box.top()) < 1e-6 || std::abs(p.y() - box.bottom()) < 1e-6;
        if (!onX || !onY)
            return false;
    }
    return true;
}

// Where a clip is drawn, in canvas px, `relUs` into it.
QRectF clipRectPx(const drift::Clip &clip, double canvasW, double canvasH, drift::TimeUs relUs = 0)
{
    const double x = clip.transformX.isEmpty() ? 0.0 : clip.transformX.evaluateAt(relUs);
    const double y = clip.transformY.isEmpty() ? 0.0 : clip.transformY.evaluateAt(relUs);
    const double w = clip.transformW.isEmpty() ? canvasW : clip.transformW.evaluateAt(relUs);
    const double h = clip.transformH.isEmpty() ? canvasH : clip.transformH.evaluateAt(relUs);
    return QRectF(x, y, w, h);
}

// A picture of mediaW × mediaH scaled to cover the box and centred on it.
QRectF coverRect(const QRectF &box, double mediaW, double mediaH)
{
    if (mediaW <= 0.0 || mediaH <= 0.0)
        return box;
    const double scale = std::max(box.width() / mediaW, box.height() / mediaH);
    const double w = mediaW * scale;
    const double h = mediaH * scale;
    return QRectF(box.center().x() - w / 2.0, box.center().y() - h / 2.0, w, h);
}

// Canvas px into fractions of a clip's own frame, which is the space a mask is in.
QVector<QPointF> toClipSpace(const QVector<QPointF> &px, const QRectF &rect)
{
    QVector<QPointF> out;
    out.reserve(px.size());
    const double w = std::max(1e-9, rect.width());
    const double h = std::max(1e-9, rect.height());
    for (const QPointF &p : px)
        out.append(QPointF((p.x() - rect.x()) / w, (p.y() - rect.y()) / h));
    return out;
}

// The mask that cuts a clip drawn in `rect` (canvas px) to cell `index`, inset for the border, with
// the cell's entrance when the split screen animates.
drift::Mask cellMask(const drift::SplitLayoutDef &layout, int index, const drift::SplitScreenInfo &info,
                     const QRectF &rect, double canvasW, double canvasH)
{
    const drift::SplitCellDef &cell = layout.cells.at(index);
    const QVector<QPointF> px = drift::splitCellPolygonPx(cell, drift::splitLayoutBounds(layout), canvasW,
                                                          canvasH, std::max(0.0, info.borderPx));
    drift::Mask mask;
    mask.shape = drift::MaskShape::Freeform;
    mask.name = AppController::tr("Split screen cell %1").arg(index + 1);
    mask.points = toClipSpace(px, rect);
    const QRectF box = polygonBox(mask.points);
    mask.x = box.center().x();
    mask.y = box.center().y();
    mask.w = box.width();
    mask.h = box.height();
    if (info.animate)
        mask.pathKeys = drift::splitRevealKeys(mask.points, drift::splitCellReveal(cell),
                                               drift::TimeUs(index) * drift::kSplitStaggerUs);
    return mask;
}

// An empty cell: a dark grey rectangle filling the cell's box.
drift::Clip placeholderClip(const QString &id, int number, const QRectF &box, drift::TimeUs duration)
{
    drift::Clip clip;
    clip.id = id.isEmpty() ? newId() : id;
    clip.type = drift::ClipType::Shape;
    clip.name = AppController::tr("Cell %1").arg(number);
    clip.shapeStyle.kind = drift::ShapeKind::Rectangle;
    clip.shapeStyle.layers = drift::defaultShapeLayers(QColor(43, 48, 56), QColor(32, 36, 42), QColor(Qt::white), 0.0);
    clip.timelineStart = 0;
    clip.timelineDuration = duration;
    clip.srcIn = 0;
    clip.srcOut = duration;
    setClipLayoutPixels(clip, box.x(), box.y(), box.width(), box.height());
    return clip;
}

bool isPlaceholder(const drift::Clip &clip)
{
    return clip.type == drift::ClipType::Shape;
}

// The fill on the bottom track that shows between the cells: the layout's box in the border colour.
// With the entrance on it fades in once the last cell is whole, so the canvas stays empty until then.
drift::Clip borderClip(const QString &id, const drift::SplitLayoutDef &layout, const drift::SplitScreenInfo &info,
                       drift::TimeUs duration, double canvasW, double canvasH)
{
    drift::Clip clip;
    clip.id = id.isEmpty() ? newId() : id;
    clip.type = drift::ClipType::Shape;
    clip.name = AppController::tr("Split screen border");
    clip.shapeStyle.kind = drift::ShapeKind::Rectangle;
    QColor colour(info.borderColor);
    if (!colour.isValid())
        colour = Qt::white;
    clip.shapeStyle.layers = drift::defaultShapeLayers(colour, colour, QColor(Qt::white), 0.0);
    clip.timelineStart = 0;
    clip.timelineDuration = duration;
    clip.srcIn = 0;
    clip.srcOut = duration;
    const QRectF bounds = drift::splitLayoutBounds(layout);
    setClipLayoutPixels(clip, bounds.x() * canvasW, bounds.y() * canvasH, bounds.width() * canvasW,
                        bounds.height() * canvasH);
    if (info.animate) {
        const drift::TimeUs whole = drift::splitEntranceUs(int(layout.cells.size()));
        clip.opacity.setKeyframe(0, 0.0);
        clip.opacity.setKeyframe(whole, 0.0);
        clip.opacity.setKeyframe(whole + kBorderFadeUs, 1.0);
    }
    return clip;
}

// The clip a cell track shows (its first that is not an adjustment), or -1.
int contentClipIndex(const drift::Track &track)
{
    for (int i = 0; i < track.clips.size(); ++i) {
        if (track.clips.at(i).type != drift::ClipType::Adjustment)
            return i;
    }
    return -1;
}

// Removes a track and the lanes hanging off it.
void removeTrackWithLanes(drift::Project &project, const QString &trackId)
{
    if (trackId.isEmpty())
        return;
    QList<drift::Track> &tracks = project.tracks();
    for (int i = tracks.size() - 1; i >= 0; --i) {
        if (tracks.at(i).id == trackId || tracks.at(i).parentTrackId == trackId)
            tracks.removeAt(i);
    }
}

// The length of a split screen: its nested timeline's.
drift::TimeUs splitLengthUs(const drift::Project &project, const drift::Clip &split)
{
    const drift::TimeUs length = project.sequenceDurationUs(split.sequenceId);
    return length > 0 ? length : qMax<drift::TimeUs>(split.timelineDuration, drift::secondsToUs(1.0));
}

using MediaSize = std::function<QSizeF(const drift::Clip &)>;

// Brings a split screen's cell tracks (the tracks of `cells`) in line with its layout and look: a
// track per cell (a new cell gets a placeholder, cells the layout lacks go with their lanes), each
// cell clip sized and masked, and the border fill on the bottom track (or none). Placeholders always
// fill their cell's box; media is fitted to cover its cell again only for the cells in `refit`, so
// a picture moved inside its cell stays put when only the look changes.
void syncSplitCells(drift::Project &cells, const drift::SplitLayoutDef &layout, drift::SplitScreenInfo &info,
                    drift::TimeUs duration, double canvasW, double canvasH, const MediaSize &mediaSize,
                    const QSet<int> &refit)
{
    QList<drift::Track> &tracks = cells.tracks();
    const int count = int(layout.cells.size());
    const QRectF bounds = drift::splitLayoutBounds(layout);

    for (int i = count; i < info.cellTrackIds.size(); ++i)
        removeTrackWithLanes(cells, info.cellTrackIds.at(i));
    while (info.cellTrackIds.size() > count)
        info.cellTrackIds.removeLast();

    const auto indexOf = [&cells](const QString &id) { return id.isEmpty() ? -1 : cells.trackIndexById(id); };

    for (int i = 0; i < count; ++i) {
        const QRectF box =
            polygonBox(drift::splitCellPolygonPx(layout.cells.at(i), bounds, canvasW, canvasH, 0.0));
        int t = i < info.cellTrackIds.size() ? indexOf(info.cellTrackIds.at(i)) : -1;
        if (t < 0) {
            // New cells go above the border fill, which stays the bottom track.
            drift::Track track{.type = drift::TrackType::Shape};
            track.id = newId();
            const int border = indexOf(info.borderTrackId);
            t = border >= 0 ? border : int(tracks.size());
            tracks.insert(t, track);
            if (i < info.cellTrackIds.size())
                info.cellTrackIds[i] = track.id;
            else
                info.cellTrackIds.append(track.id);
        }

        drift::Track &track = tracks[t];
        track.name = AppController::tr("Cell %1").arg(i + 1);
        int c = contentClipIndex(track);
        if (c < 0) {
            track.clips.append(placeholderClip(QString(), i + 1, box, duration));
            c = int(track.clips.size()) - 1;
        }
        drift::Clip &clip = track.clips[c];
        if (isPlaceholder(clip)) {
            clip.name = AppController::tr("Cell %1").arg(i + 1);
            setClipLayoutPixels(clip, box.x(), box.y(), box.width(), box.height());
        } else if (refit.contains(i) && mediaSize) {
            const QSizeF size = mediaSize(clip);
            const QRectF rect = coverRect(box, size.width(), size.height());
            setClipLayoutPixels(clip, rect.x(), rect.y(), rect.width(), rect.height());
            clip.rotation = {};
        }
        track.type = clip.type == drift::ClipType::Video ? drift::TrackType::Video : drift::TrackType::Shape;

        const drift::Mask mask = cellMask(layout, i, info, clipRectPx(clip, canvasW, canvasH), canvasW, canvasH);
        // Inserts a lane on the first call, so nothing indexed above is used past this line.
        drift::setLinkedMask(cells, t, c, mask);
    }

    int border = indexOf(info.borderTrackId);
    if (info.borderPx > 0.0) {
        if (border < 0) {
            drift::Track track{.type = drift::TrackType::Shape};
            track.id = newId();
            tracks.append(track);
            border = int(tracks.size()) - 1;
            info.borderTrackId = track.id;
        }
        drift::Track &track = tracks[border];
        track.name = AppController::tr("Border");
        const QString keepId = track.clips.isEmpty() ? QString() : track.clips.first().id;
        track.clips.clear();
        track.clips.append(borderClip(keepId, layout, info, duration, canvasW, canvasH));
    } else if (border >= 0) {
        removeTrackWithLanes(cells, info.borderTrackId);
        info.borderTrackId.clear();
    } else {
        info.borderTrackId.clear();
    }
}

// The clip a cell shows, or null.
const drift::Clip *cellContent(const drift::Project &project, const drift::Clip &split, int cell)
{
    const QString trackId = split.splitScreen.cellTrackIds.value(cell);
    if (trackId.isEmpty())
        return nullptr;
    for (const drift::Track &track : project.sequenceTracks(split.sequenceId)) {
        if (track.id != trackId)
            continue;
        const int c = contentClipIndex(track);
        return c >= 0 ? &track.clips.at(c) : nullptr;
    }
    return nullptr;
}

bool cellIsEmpty(const drift::Project &project, const drift::Clip &split, int cell)
{
    const drift::Clip *content = cellContent(project, split, cell);
    return !content || isPlaceholder(*content);
}

} // namespace

// ---------------------------------------------------------------------------
// The cards

QVariantList AppController::splitScreenLayouts() const
{
    QVariantList out;
    for (const drift::SplitLayoutDef &layout : drift::splitScreenLayoutDefs()) {
        QVariantList cells;
        for (int i = 0; i < layout.cells.size(); ++i) {
            const drift::SplitCellDef &cell = layout.cells.at(i);
            cells.append(QVariantMap{
                {QStringLiteral("points"), pointsToVariant(cell.points)},
                {QStringLiteral("from"),
                 pointsToVariant(drift::splitCollapsedPolygon(cell.points, drift::splitCellReveal(cell)))},
                {QStringLiteral("delay"), drift::usToSeconds(drift::TimeUs(i) * drift::kSplitStaggerUs)},
                {QStringLiteral("duration"), drift::usToSeconds(drift::kSplitRevealUs)}});
        }
        out.append(QVariantMap{{QStringLiteral("id"), layout.id},
                               {QStringLiteral("label"), layoutLabel(layout)},
                               {QStringLiteral("number"), layout.number},
                               {QStringLiteral("cells"), cells},
                               {QStringLiteral("entrance"),
                                drift::usToSeconds(drift::splitEntranceUs(int(layout.cells.size())))}});
    }
    return out;
}

// ---------------------------------------------------------------------------
// Applying a layout to clips already on the timeline (a click on a card with clips selected)

int AppController::applySplitScreen(const QString &layoutId)
{
    const drift::SplitLayoutDef *layout = drift::splitScreenLayout(layoutId);
    if (!layout)
        return 0;

    const auto isPicture = [](const drift::Clip &clip) {
        return (clip.type == drift::ClipType::Video || clip.type == drift::ClipType::Image) && !clip.id.isEmpty();
    };

    // The clips to place, top track first: the selection when it holds two or more pictures,
    // otherwise every picture under the playhead.
    QList<QPair<int, int>> refs;
    for (const auto &pair : m_selection) {
        if (pair.first >= 0 && pair.first < m_project.tracks().size() && pair.second >= 0
            && pair.second < m_project.tracks().at(pair.first).clips.size()
            && isPicture(m_project.tracks().at(pair.first).clips.at(pair.second)))
            refs.append(pair);
    }
    if (refs.size() < 2) {
        refs.clear();
        for (int t = 0; t < m_project.tracks().size(); ++t) {
            const drift::Track &track = m_project.tracks().at(t);
            if (track.hidden)
                continue;
            for (int c = 0; c < track.clips.size(); ++c) {
                if (isPicture(track.clips.at(c)) && track.clips.at(c).containsTime(m_playheadUs))
                    refs.append({t, c});
            }
        }
    }
    if (refs.isEmpty()) {
        setLastMessage(tr("Put clips on different video tracks, or select them, then pick a split screen"),
                       QStringLiteral("warning"));
        return 0;
    }
    std::sort(refs.begin(), refs.end());

    // Indices shift when a mask lane is inserted, so the clips are followed by id.
    QStringList ids;
    for (const auto &pair : refs)
        ids.append(m_project.tracks().at(pair.first).clips.at(pair.second).id);

    const double canvasW = qMax(1, m_project.width());
    const double canvasH = qMax(1, m_project.height());
    const QRectF bounds = drift::splitLayoutBounds(*layout);
    const drift::Project before = m_project;
    int placed = 0;
    for (int i = 0; i < ids.size() && i < layout->cells.size(); ++i) {
        int t = -1;
        int c = -1;
        if (!findClipById(m_project, ids.at(i), &t, &c))
            continue;
        drift::Clip &clip = m_project.tracks()[t].clips[c];
        const QVector<QPointF> cellPx = drift::splitCellPolygonPx(layout->cells.at(i), bounds, canvasW, canvasH, 0.0);
        const QRectF cellBox = polygonBox(cellPx);

        // The picture's own shape: the asset's size with its rotation correction, else the shape
        // of the rect it is drawn in now.
        QSizeF media = splitMediaSize(clip);
        if (media.width() <= 0 || media.height() <= 0) {
            const drift::TimeUs rel = qMax<drift::TimeUs>(0, m_playheadUs - clip.timelineStart);
            media = clipRectPx(clip, canvasW, canvasH, rel).size();
        }

        // Scale to cover the cell, centred on it, then cut the overflow away with a mask.
        const QRectF rect = coverRect(cellBox, media.width(), media.height());
        setClipLayoutPixels(clip, rect.x(), rect.y(), rect.width(), rect.height());
        clip.rotation = {};

        // Mask coordinates are fractions of the clip's own frame.
        drift::Mask mask;
        mask.name = tr("Split screen");
        if (isAxisBox(cellPx, cellBox)) {
            mask.shape = drift::MaskShape::Rectangle;
            mask.w = cellBox.width() / rect.width();
            mask.h = cellBox.height() / rect.height();
            mask.x = (cellBox.center().x() - rect.x()) / rect.width();
            mask.y = (cellBox.center().y() - rect.y()) / rect.height();
        } else {
            mask.shape = drift::MaskShape::Freeform;
            mask.points = toClipSpace(cellPx, rect);
            const QRectF box = polygonBox(mask.points);
            mask.x = box.center().x();
            mask.y = box.center().y();
            mask.w = box.width();
            mask.h = box.height();
        }
        writeClipMask(t, c, mask);
        ++placed;
    }

    pushProjectEdit(before, tr("Split screen"));
    finishEdit(ids.size() > layout->cells.size()
                   ? tr("Split screen: %1 of %2 clips placed").arg(placed).arg(ids.size())
                   : tr("Split screen applied"));
    return placed;
}

// ---------------------------------------------------------------------------
// Split screen clips

QSizeF AppController::splitMediaSize(const drift::Clip &clip) const
{
    double w = 0.0;
    double h = 0.0;
    if (const drift::MediaAsset *asset = m_project.asset(clip.assetId)) {
        w = asset->width;
        h = asset->height;
        if (clip.rotationCorrection % 180 != 0)
            std::swap(w, h);
    }
    return QSizeF(w, h);
}

QString AppController::insertSplitScreen(const QString &layoutId, int trackIndex, double atSeconds)
{
    const drift::SplitLayoutDef *layout = drift::splitScreenLayout(layoutId);
    if (!layout || layout->cells.isEmpty())
        return {};
    setPlaying(false);

    const double canvasW = qMax(1, m_project.width());
    const double canvasH = qMax(1, m_project.height());
    const drift::TimeUs duration = qMax(drift::secondsToUs(1.0), drift::imageClipDurationUs());
    const drift::TimeUs wantStart = atSeconds < 0.0 ? m_playheadUs : drift::secondsToUs(atSeconds);

    const drift::Project before = m_project.detachedCopy();

    // The cells, built on a scratch project so the lane helpers can work on them.
    drift::SplitScreenInfo info;
    info.layout = layout->id;
    drift::Project cells;
    cells.setResolution(m_project.width(), m_project.height());
    cells.setFps(m_project.fps());
    cells.tracks().clear();
    syncSplitCells(cells, *layout, info, duration, canvasW, canvasH, MediaSize(), {});
    syncLinkedAdjustments(cells);
    const QString sequenceId = m_project.addSequence(cells.tracks());

    drift::MediaAsset asset;
    asset.kind = drift::MediaKind::Composite;
    asset.name = layoutLabel(*layout);
    asset.sequenceId = sequenceId;
    asset.durationUs = duration;
    asset.width = m_project.width();
    asset.height = m_project.height();
    asset.fps = m_project.fps();
    asset.folderId = m_bin ? m_bin->currentFolderId() : QString();
    const QString assetId = m_project.addAsset(asset);

    drift::Clip clip;
    clip.id = newId();
    clip.type = drift::ClipType::Composite;
    clip.assetId = assetId;
    clip.sequenceId = sequenceId;
    clip.name = asset.name;
    clip.timelineDuration = duration;
    clip.srcIn = 0;
    clip.srcOut = duration;
    clip.splitScreen = info;

    // The track asked for when it takes the clip there (stacking onto a free track as media does),
    // else the first free video track.
    int target = trackIndex;
    if (target >= 0 && target < m_project.tracks().size()
        && m_project.tracks().at(target).allowsClipType(drift::ClipType::Composite)
        && !m_project.tracks().at(target).isAdjustmentLane()) {
        const StackPlan plan = stackPlan(target, drift::ClipType::Composite, wantStart, duration);
        if (plan.insertIndex >= 0)
            target = drift::insertTrackAboveForClipType(m_project, plan.insertIndex, drift::ClipType::Composite);
        else if (plan.track >= 0)
            target = plan.track;
    } else {
        target = drift::ensureFreeTrackForClipType(m_project, drift::ClipType::Composite, wantStart, duration, true);
    }
    if (target < 0 || target >= m_project.tracks().size()) {
        m_project = before;
        return {};
    }

    drift::Track &track = m_project.tracks()[target];
    clip.timelineStart =
        drift::resolveClipStart(m_project, track, -1, wantStart, duration, m_snapEnabled, m_playheadUs);
    int insertAt = 0;
    while (insertAt < track.clips.size() && track.clips.at(insertAt).timelineStart < clip.timelineStart)
        ++insertAt;
    track.clips.insert(insertAt, clip);

    if (m_assetLibrary)
        m_assetLibrary->syncToProject();
    pushProjectEdit(before, tr("Split screen added"));
    finishEdit(tr("%1 added — drop videos or pictures onto its cells in the preview").arg(clip.name));
    selectClipById(clip.id);
    return clip.id;
}

void AppController::activateSplitScreen(const QString &layoutId)
{
    int pictures = 0;
    for (const auto &pair : m_selection) {
        if (!isValidClipIndex(pair.first, pair.second))
            continue;
        const drift::ClipType type = m_project.tracks().at(pair.first).clips.at(pair.second).type;
        if (type == drift::ClipType::Video || type == drift::ClipType::Image)
            ++pictures;
    }
    if (pictures >= 2)
        applySplitScreen(layoutId);
    else
        insertSplitScreen(layoutId, -1, -1.0);
}

int AppController::splitScreenClipAt(int trackIndex, double atSeconds) const
{
    if (trackIndex < 0 || trackIndex >= m_project.tracks().size())
        return -1;
    const drift::TimeUs at = drift::secondsToUs(qMax(0.0, atSeconds));
    const drift::Track &track = m_project.tracks().at(trackIndex);
    for (int c = track.clips.size() - 1; c >= 0; --c) {
        const drift::Clip &clip = track.clips.at(c);
        if (!clip.splitScreen.isEmpty() && clip.containsTime(at))
            return c;
    }
    return -1;
}

bool AppController::editSplitScreen(int trackIndex, int clipIndex, const QString &undoText,
                                    const std::function<bool(drift::Project &, drift::Clip &)> &edit)
{
    if (!isValidClipIndex(trackIndex, clipIndex))
        return false;
    {
        const drift::Clip &split = m_project.tracks().at(trackIndex).clips.at(clipIndex);
        if (split.splitScreen.isEmpty() || split.sequenceId.isEmpty() || !m_project.hasSequence(split.sequenceId)
            || split.sequenceId == m_project.activeSequenceId())
            return false;
    }
    setPlaying(false);

    // Snapshot first: references taken below must not reach into it.
    const drift::Project before = m_project.detachedCopy();
    drift::Clip &split = m_project.tracks()[trackIndex].clips[clipIndex];
    drift::Project cells;
    cells.setResolution(m_project.width(), m_project.height());
    cells.setFps(m_project.fps());
    cells.tracks() = m_project.sequenceTracks(split.sequenceId);

    if (!edit(cells, split))
        return false;
    syncLinkedAdjustments(cells);
    m_project.replaceSequenceTracks(split.sequenceId, cells.tracks());
    if (m_assetLibrary)
        m_assetLibrary->syncToProject();
    pushProjectEdit(before, undoText);
    finishEdit(undoText);
    return true;
}

bool AppController::fillSplitScreenCell(int trackIndex, int clipIndex, int cell, int assetIndex)
{
    if (!m_assetLibrary || !isValidClipIndex(trackIndex, clipIndex))
        return false;
    const drift::Clip &split = m_project.tracks().at(trackIndex).clips.at(clipIndex);
    const drift::SplitLayoutDef *layout = drift::splitScreenLayout(split.splitScreen.layout);
    if (!layout)
        return false;

    const QVariantMap asset = m_assetLibrary->assetAt(assetIndex);
    const QString kind = asset.value(QStringLiteral("kind")).toString();
    if (kind != QLatin1String("video") && kind != QLatin1String("image")) {
        setLastMessage(tr("Only videos and pictures go into the cells of a split screen"), QStringLiteral("warning"));
        return false;
    }
    if (cell < 0) {
        for (int i = 0; i < layout->cells.size() && cell < 0; ++i) {
            if (cellIsEmpty(m_project, split, i))
                cell = i;
        }
        if (cell < 0) {
            setLastMessage(tr("Every cell is filled. Drop onto a cell in the preview to replace what it shows."),
                           QStringLiteral("info"));
            return false;
        }
    }
    if (cell >= layout->cells.size())
        return false;

    // The media clip, made as a timeline drop makes one, then cut to the split screen's length.
    m_assetLibrary->ensureMedia(assetIndex);
    drift::Clip media;
    media.assetId = m_assetLibrary->assetIdAt(assetIndex);
    media.type = drift::clipTypeFromString(kind);
    media.name = asset.value(QStringLiteral("name")).toString();
    media.path = asset.value(QStringLiteral("path")).toString();
    media.thumbnailPath = m_assetLibrary->thumbnailAt(assetIndex);
    media.filmstripPath = m_assetLibrary->filmstripAt(assetIndex);
    media.srcIn = 0;
    media.srcOut = clipDurationForAssetIndex(assetIndex);
    applyAssetLayout(media, asset, m_project.width(), m_project.height());
    const drift::TimeUs length = splitLengthUs(m_project, split);
    media.timelineStart = 0;
    if (media.type == drift::ClipType::Image) {
        media.timelineDuration = length;
        media.srcOut = media.srcIn + length;
    } else {
        const drift::TimeUs available = media.srcOut - media.srcIn;
        media.timelineDuration = available > 0 ? qMin(length, available) : length;
        media.srcOut = media.srcIn + media.timelineDuration;
    }

    const double canvasW = qMax(1, m_project.width());
    const double canvasH = qMax(1, m_project.height());
    return editSplitScreen(trackIndex, clipIndex, tr("Media put into cell %1").arg(cell + 1),
                           [&](drift::Project &cells, drift::Clip &splitClip) {
        drift::SplitScreenInfo info = splitClip.splitScreen;
        // Makes sure the cell's track exists before media goes on it.
        syncSplitCells(cells, *layout, info, length, canvasW, canvasH, MediaSize(), {});
        const int t = cells.trackIndexById(info.cellTrackIds.value(cell));
        if (t < 0)
            return false;
        drift::Track &track = cells.tracks()[t];
        const int c = contentClipIndex(track);
        drift::Clip filled = media;
        // The cell's clip keeps its id, so the mask (and any effects) pinned to it stay.
        filled.id = c >= 0 ? track.clips.at(c).id : newId();
        if (c >= 0)
            track.clips[c] = filled;
        else
            track.clips.append(filled);
        syncSplitCells(cells, *layout, info, length, canvasW, canvasH,
                       [this](const drift::Clip &clip) { return splitMediaSize(clip); }, {cell});
        splitClip.splitScreen = info;
        return true;
    });
}

void AppController::clearSplitScreenCell(int trackIndex, int clipIndex, int cell)
{
    if (!isValidClipIndex(trackIndex, clipIndex))
        return;
    const drift::Clip &split = m_project.tracks().at(trackIndex).clips.at(clipIndex);
    const drift::SplitLayoutDef *layout = drift::splitScreenLayout(split.splitScreen.layout);
    if (!layout || cell < 0 || cell >= layout->cells.size() || cellIsEmpty(m_project, split, cell))
        return;
    const drift::TimeUs length = splitLengthUs(m_project, split);
    const double canvasW = qMax(1, m_project.width());
    const double canvasH = qMax(1, m_project.height());
    editSplitScreen(trackIndex, clipIndex, tr("Cell %1 emptied").arg(cell + 1),
                    [&](drift::Project &cells, drift::Clip &splitClip) {
        drift::SplitScreenInfo info = splitClip.splitScreen;
        const int t = cells.trackIndexById(info.cellTrackIds.value(cell));
        if (t < 0)
            return false;
        drift::Track &track = cells.tracks()[t];
        const int c = contentClipIndex(track);
        if (c < 0)
            return false;
        track.clips[c] = placeholderClip(track.clips.at(c).id, cell + 1, QRectF(0, 0, canvasW, canvasH), length);
        syncSplitCells(cells, *layout, info, length, canvasW, canvasH, MediaSize(), {});
        splitClip.splitScreen = info;
        return true;
    });
}

void AppController::swapSplitScreenCells(int trackIndex, int clipIndex, int cellA, int cellB)
{
    if (!isValidClipIndex(trackIndex, clipIndex) || cellA == cellB)
        return;
    const drift::Clip &split = m_project.tracks().at(trackIndex).clips.at(clipIndex);
    const drift::SplitLayoutDef *layout = drift::splitScreenLayout(split.splitScreen.layout);
    if (!layout || cellA < 0 || cellB < 0 || cellA >= layout->cells.size() || cellB >= layout->cells.size())
        return;
    const drift::TimeUs length = splitLengthUs(m_project, split);
    const double canvasW = qMax(1, m_project.width());
    const double canvasH = qMax(1, m_project.height());
    editSplitScreen(trackIndex, clipIndex, tr("Cells %1 and %2 swapped").arg(cellA + 1).arg(cellB + 1),
                    [&](drift::Project &cells, drift::Clip &splitClip) {
        drift::SplitScreenInfo info = splitClip.splitScreen;
        syncSplitCells(cells, *layout, info, length, canvasW, canvasH, MediaSize(), {});
        const int ta = cells.trackIndexById(info.cellTrackIds.value(cellA));
        const int tb = cells.trackIndexById(info.cellTrackIds.value(cellB));
        if (ta < 0 || tb < 0)
            return false;
        const int ca = contentClipIndex(cells.tracks().at(ta));
        const int cb = contentClipIndex(cells.tracks().at(tb));
        if (ca < 0 || cb < 0)
            return false;
        // What each cell shows changes places; each cell keeps its clip id, and with it its mask.
        drift::Clip a = cells.tracks().at(ta).clips.at(ca);
        drift::Clip b = cells.tracks().at(tb).clips.at(cb);
        std::swap(a.id, b.id);
        cells.tracks()[ta].clips[ca] = b;
        cells.tracks()[tb].clips[cb] = a;
        syncSplitCells(cells, *layout, info, length, canvasW, canvasH,
                       [this](const drift::Clip &clip) { return splitMediaSize(clip); }, {cellA, cellB});
        splitClip.splitScreen = info;
        return true;
    });
}

void AppController::setSplitScreenLayout(int trackIndex, int clipIndex, const QString &layoutId)
{
    const drift::SplitLayoutDef *layout = drift::splitScreenLayout(layoutId);
    if (!layout || !isValidClipIndex(trackIndex, clipIndex))
        return;
    const drift::Clip &split = m_project.tracks().at(trackIndex).clips.at(clipIndex);
    const drift::SplitLayoutDef *previous = drift::splitScreenLayout(split.splitScreen.layout);
    if (!previous || previous->id == layout->id)
        return;

    int dropped = 0;
    for (int i = int(layout->cells.size()); i < previous->cells.size(); ++i) {
        if (!cellIsEmpty(m_project, split, i))
            ++dropped;
    }
    const QString oldLabel = layoutLabel(*previous);
    const QString newLabel = layoutLabel(*layout);
    const drift::TimeUs length = splitLengthUs(m_project, split);
    const double canvasW = qMax(1, m_project.width());
    const double canvasH = qMax(1, m_project.height());
    QSet<int> every;
    for (int i = 0; i < layout->cells.size(); ++i)
        every.insert(i);

    const bool changed = editSplitScreen(trackIndex, clipIndex, tr("Split screen layout changed"),
                                         [&](drift::Project &cells, drift::Clip &splitClip) {
        drift::SplitScreenInfo info = splitClip.splitScreen;
        info.layout = layout->id;
        syncSplitCells(cells, *layout, info, length, canvasW, canvasH,
                       [this](const drift::Clip &clip) { return splitMediaSize(clip); }, every);
        splitClip.splitScreen = info;
        // A name the user gave stays; the template's own name follows the layout.
        if (splitClip.name == oldLabel)
            splitClip.name = newLabel;
        if (drift::MediaAsset *asset = m_project.asset(splitClip.assetId)) {
            if (asset->name == oldLabel)
                asset->name = newLabel;
        }
        return true;
    });
    if (changed && dropped > 0)
        setLastMessage(tr("%1 has fewer cells: %n clip(s) left out", nullptr, dropped).arg(newLabel),
                       QStringLiteral("info"));
}

void AppController::setSplitScreenAnimation(int trackIndex, int clipIndex, bool on)
{
    if (!isValidClipIndex(trackIndex, clipIndex))
        return;
    const drift::Clip &split = m_project.tracks().at(trackIndex).clips.at(clipIndex);
    const drift::SplitLayoutDef *layout = drift::splitScreenLayout(split.splitScreen.layout);
    if (!layout || split.splitScreen.animate == on)
        return;
    const drift::TimeUs length = splitLengthUs(m_project, split);
    const double canvasW = qMax(1, m_project.width());
    const double canvasH = qMax(1, m_project.height());
    editSplitScreen(trackIndex, clipIndex, on ? tr("Split screen animation on") : tr("Split screen animation off"),
                    [&](drift::Project &cells, drift::Clip &splitClip) {
        drift::SplitScreenInfo info = splitClip.splitScreen;
        info.animate = on;
        syncSplitCells(cells, *layout, info, length, canvasW, canvasH, MediaSize(), {});
        splitClip.splitScreen = info;
        return true;
    });
}

void AppController::setSplitScreenBorder(int trackIndex, int clipIndex, double widthPx, const QString &color)
{
    if (!isValidClipIndex(trackIndex, clipIndex))
        return;
    const drift::Clip &split = m_project.tracks().at(trackIndex).clips.at(clipIndex);
    const drift::SplitLayoutDef *layout = drift::splitScreenLayout(split.splitScreen.layout);
    if (!layout)
        return;
    const double width = qBound(0.0, widthPx, kMaxBorderPx);
    const QColor parsed(color);
    const QString colour = parsed.isValid() ? parsed.name(QColor::HexRgb) : split.splitScreen.borderColor;
    if (std::abs(width - split.splitScreen.borderPx) < 1e-6 && colour == split.splitScreen.borderColor)
        return;
    const drift::TimeUs length = splitLengthUs(m_project, split);
    const double canvasW = qMax(1, m_project.width());
    const double canvasH = qMax(1, m_project.height());
    editSplitScreen(trackIndex, clipIndex, width > 0.0 ? tr("Split screen border") : tr("Split screen border off"),
                    [&](drift::Project &cells, drift::Clip &splitClip) {
        drift::SplitScreenInfo info = splitClip.splitScreen;
        info.borderPx = width;
        info.borderColor = colour;
        syncSplitCells(cells, *layout, info, length, canvasW, canvasH, MediaSize(), {});
        splitClip.splitScreen = info;
        return true;
    });
}

void AppController::setSplitScreenDuration(int trackIndex, int clipIndex, double seconds)
{
    if (!isValidClipIndex(trackIndex, clipIndex))
        return;
    const drift::Clip &split = m_project.tracks().at(trackIndex).clips.at(clipIndex);
    const drift::SplitLayoutDef *layout = drift::splitScreenLayout(split.splitScreen.layout);
    if (!layout)
        return;
    drift::TimeUs length = qBound(drift::secondsToUs(0.5), drift::secondsToUs(seconds), drift::secondsToUs(3600.0));
    // It grows only as far as the next clip on its track.
    for (const drift::Clip &other : m_project.tracks().at(trackIndex).clips) {
        if (other.id != split.id && other.timelineStart >= split.timelineEnd())
            length = qMin(length, other.timelineStart - split.timelineStart);
    }
    if (length == split.timelineDuration && length == splitLengthUs(m_project, split))
        return;
    const double canvasW = qMax(1, m_project.width());
    const double canvasH = qMax(1, m_project.height());

    editSplitScreen(trackIndex, clipIndex, tr("Split screen length"), [&](drift::Project &cells, drift::Clip &splitClip) {
        drift::SplitScreenInfo info = splitClip.splitScreen;
        syncSplitCells(cells, *layout, info, length, canvasW, canvasH, MediaSize(), {});
        for (const QString &trackId : info.cellTrackIds + QStringList{info.borderTrackId}) {
            const int t = cells.trackIndexById(trackId);
            if (t < 0)
                continue;
            drift::Track &track = cells.tracks()[t];
            const int c = contentClipIndex(track);
            if (c < 0)
                continue;
            drift::Clip &clip = track.clips[c];
            clip.timelineStart = 0;
            if (clip.type == drift::ClipType::Video) {
                // A video plays as far as its media goes.
                drift::TimeUs available = length;
                if (const drift::MediaAsset *asset = m_project.asset(clip.assetId)) {
                    if (asset->durationUs > clip.srcIn)
                        available = asset->durationUs - clip.srcIn;
                }
                clip.speed = 1.0;
                clip.speedCurve = {};
                clip.reverse = false;
                clip.timelineDuration = qMax<drift::TimeUs>(1, qMin(length, available));
                clip.srcOut = clip.srcIn + clip.timelineDuration;
            } else {
                clip.timelineDuration = length;
                clip.srcOut = clip.srcIn + length;
            }
        }
        splitClip.timelineDuration = length;
        splitClip.srcIn = 0;
        splitClip.srcOut = length;
        if (drift::MediaAsset *asset = m_project.asset(splitClip.assetId))
            asset->durationUs = length;
        splitClip.splitScreen = info;
        return true;
    });
}

QVariantMap AppController::splitScreenInfo(int trackIndex, int clipIndex) const
{
    if (!isValidClipIndex(trackIndex, clipIndex))
        return {};
    const drift::Clip &split = m_project.tracks().at(trackIndex).clips.at(clipIndex);
    const drift::SplitLayoutDef *layout = drift::splitScreenLayout(split.splitScreen.layout);
    if (!layout)
        return {};
    QVariantList cells;
    int filled = 0;
    for (int i = 0; i < layout->cells.size(); ++i) {
        const drift::Clip *content = cellContent(m_project, split, i);
        const bool hasMedia = content && !isPlaceholder(*content);
        if (hasMedia)
            ++filled;
        cells.append(QVariantMap{
            {QStringLiteral("number"), i + 1},
            {QStringLiteral("filled"), hasMedia},
            {QStringLiteral("name"), hasMedia ? content->name : QString()},
            {QStringLiteral("kind"), hasMedia ? drift::clipTypeToString(content->type) : QString()},
            {QStringLiteral("thumbnail"), hasMedia ? content->thumbnailPath : QString()}});
    }
    return {{QStringLiteral("layout"), layout->id},
            {QStringLiteral("label"), layoutLabel(*layout)},
            {QStringLiteral("number"), layout->number},
            {QStringLiteral("animate"), split.splitScreen.animate},
            {QStringLiteral("borderPx"), split.splitScreen.borderPx},
            {QStringLiteral("borderColor"), split.splitScreen.borderColor},
            {QStringLiteral("durationSeconds"), drift::usToSeconds(split.timelineDuration)},
            {QStringLiteral("cells"), cells},
            {QStringLiteral("filledCount"), filled}};
}

namespace {

// The split screen clips at `atUs` on the open timeline, the selected one first, then top track first.
QList<QPair<int, int>> splitScreensAt(const drift::Project &project, drift::TimeUs atUs, int selectedTrack,
                                      int selectedClip)
{
    QList<QPair<int, int>> out;
    const QList<drift::Track> &tracks = project.tracks();
    if (selectedTrack >= 0 && selectedTrack < tracks.size() && selectedClip >= 0
        && selectedClip < tracks.at(selectedTrack).clips.size() && !tracks.at(selectedTrack).hidden) {
        const drift::Clip &clip = tracks.at(selectedTrack).clips.at(selectedClip);
        if (!clip.splitScreen.isEmpty() && clip.containsTime(atUs))
            out.append({selectedTrack, selectedClip});
    }
    for (int t = 0; t < tracks.size(); ++t) {
        if (tracks.at(t).hidden)
            continue;
        for (int c = 0; c < tracks.at(t).clips.size(); ++c) {
            const drift::Clip &clip = tracks.at(t).clips.at(c);
            if (!clip.splitScreen.isEmpty() && clip.containsTime(atUs) && !out.contains(qMakePair(t, c)))
                out.append({t, c});
        }
    }
    return out;
}

} // namespace

QVariantMap AppController::splitScreenOverlay() const
{
    const QList<QPair<int, int>> splits = splitScreensAt(m_project, m_playheadUs, m_selectedTrack, m_selectedClip);
    if (splits.isEmpty())
        return {};
    const int t = splits.first().first;
    const int c = splits.first().second;
    const drift::Clip &split = m_project.tracks().at(t).clips.at(c);
    const drift::SplitLayoutDef *layout = drift::splitScreenLayout(split.splitScreen.layout);
    if (!layout)
        return {};
    const double canvasW = qMax(1, m_project.width());
    const double canvasH = qMax(1, m_project.height());
    const QRectF rect = clipRectPx(split, canvasW, canvasH, m_playheadUs - split.timelineStart);
    QVariantList cells;
    for (int i = 0; i < layout->cells.size(); ++i) {
        QVector<QPointF> points;
        for (const QPointF &p : layout->cells.at(i).points)
            points.append(QPointF((rect.x() + p.x() * rect.width()) / canvasW, (rect.y() + p.y() * rect.height()) / canvasH));
        cells.append(QVariantMap{{QStringLiteral("number"), i + 1},
                                 {QStringLiteral("filled"), !cellIsEmpty(m_project, split, i)},
                                 {QStringLiteral("points"), pointsToVariant(points)}});
    }
    return {{QStringLiteral("track"), t},
            {QStringLiteral("clip"), c},
            {QStringLiteral("selected"), t == m_selectedTrack && c == m_selectedClip},
            {QStringLiteral("cells"), cells}};
}

QVariantMap AppController::splitScreenCellAtCanvasPoint(double canvasX, double canvasY) const
{
    const double canvasW = qMax(1, m_project.width());
    const double canvasH = qMax(1, m_project.height());
    for (const QPair<int, int> &ref : splitScreensAt(m_project, m_playheadUs, m_selectedTrack, m_selectedClip)) {
        const drift::Clip &split = m_project.tracks().at(ref.first).clips.at(ref.second);
        const drift::SplitLayoutDef *layout = drift::splitScreenLayout(split.splitScreen.layout);
        if (!layout)
            continue;
        const QRectF rect = clipRectPx(split, canvasW, canvasH, m_playheadUs - split.timelineStart);
        if (rect.width() <= 0 || rect.height() <= 0)
            continue;
        const QPointF local((canvasX - rect.x()) / rect.width(), (canvasY - rect.y()) / rect.height());
        const int cell = drift::splitCellAt(*layout, local);
        if (cell < 0)
            continue;
        const QRectF box = polygonBox(layout->cells.at(cell).points);
        return {{QStringLiteral("track"), ref.first},
                {QStringLiteral("clip"), ref.second},
                {QStringLiteral("cell"), cell},
                {QStringLiteral("x"), rect.x() + box.x() * rect.width()},
                {QStringLiteral("y"), rect.y() + box.y() * rect.height()},
                {QStringLiteral("width"), box.width() * rect.width()},
                {QStringLiteral("height"), box.height() * rect.height()},
                {QStringLiteral("rotation"), 0.0}};
    }
    return {};
}
