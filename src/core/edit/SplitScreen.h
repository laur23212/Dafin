#pragma once

#include "core/base/Time.h"

#include <QMap>
#include <QPointF>
#include <QRectF>
#include <QString>
#include <QVector>

namespace drift {

// Dafin: the split screen templates of Templates › Split Screen — Filmora's thirty "Basic" layouts
// (Split Screen 1 … 30) and two plain extras. Pure geometry and timing; AppControllerSplitScreen.cpp
// turns a layout into clips.
//
// A cell is a polygon in canvas fractions (0..1 both ways, y down), numbered the way the template
// card numbers it. Its entrance unfolds the cell from a line or a point (a canvas edge it touches,
// its own centre line, or its centre), cell after cell, as the cards in Filmora play on hover.

// Where a cell unfolds from. Auto picks the canvas edge the cell shares most of, or the cell's
// centre when it touches no edge (splitCellReveal).
enum class SplitReveal { Auto, Left, Right, Top, Bottom, Horizontal, Vertical, Centre };

struct SplitCellDef
{
    QVector<QPointF> points;
    SplitReveal reveal = SplitReveal::Auto;
};

struct SplitLayoutDef
{
    QString id;         // "split1" … "split30", then "left1right2" and "grid3x3"
    int number = 0;     // 1 … 30 for Filmora's layouts ("Split Screen <n>"), 0 for the extras
    QString label;      // the extras' own name; empty for the numbered ones
    QVector<SplitCellDef> cells;
};

// Every layout, Split Screen 1 … 30 first.
const QVector<SplitLayoutDef> &splitScreenLayoutDefs();
// The layout with this id, or null. The ids of the eleven layouts Dafin had before the thirty
// ("rows3", "leftright", …) still find the matching one.
const SplitLayoutDef *splitScreenLayout(const QString &id);

// Entrance timing: cell i starts unfolding at i × stagger and is whole kSplitRevealUs later.
constexpr TimeUs kSplitStaggerUs = 120000;
constexpr TimeUs kSplitRevealUs = 480000;
// When the last of `cellCount` cells is whole.
TimeUs splitEntranceUs(int cellCount);

// The reveal a cell really uses: its own, or for Auto the canvas edge it shares the longest stretch
// of (measured on a 16:9 canvas; ties go left, right, top, bottom), or Centre when it touches none.
SplitReveal splitCellReveal(const SplitCellDef &cell);

// The cell in canvas pixels, every edge it shares with another cell pulled in by gapPx / 2 (edges on
// the layout's outline, `bounds` in canvas fractions, stay put), so neighbouring cells sit gapPx apart
// and the border shows between them.
QVector<QPointF> splitCellPolygonPx(const SplitCellDef &cell, const QRectF &bounds, double canvasW,
                                    double canvasH, double gapPx);

// The polygon squashed onto the line or point it unfolds from (in the polygon's own space).
QVector<QPointF> splitCollapsedPolygon(const QVector<QPointF> &polygon, SplitReveal reveal);

// Mask path keys that unfold `full` (any space) with an ease-out: squashed until delayUs, whole
// kSplitRevealUs later. Times are relative to the mask's start.
QMap<TimeUs, QVector<QPointF>> splitRevealKeys(const QVector<QPointF> &full, SplitReveal reveal, TimeUs delayUs);

// The cell under a point in canvas fractions, or -1.
int splitCellAt(const SplitLayoutDef &layout, const QPointF &canvasFraction);

// The box all cells fit in, in canvas fractions: the whole canvas, except for a letterboxed layout.
QRectF splitLayoutBounds(const SplitLayoutDef &layout);

} // namespace drift
