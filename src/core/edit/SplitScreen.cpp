#include "core/edit/SplitScreen.h"

#include <QHash>
#include <QPolygonF>

#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <limits>

namespace drift {

namespace {

using Points = QVector<QPointF>;

constexpr double kThird = 1.0 / 3.0;

Points rect(double x, double y, double w, double h)
{
    return {QPointF(x, y), QPointF(x + w, y), QPointF(x + w, y + h), QPointF(x, y + h)};
}

SplitCellDef cell(Points points, SplitReveal reveal = SplitReveal::Auto)
{
    SplitCellDef out;
    out.points = std::move(points);
    out.reveal = reveal;
    return out;
}

// Row-major grid, cells numbered left to right, then top to bottom.
QVector<SplitCellDef> grid(int columns, int rows, SplitReveal reveal)
{
    QVector<SplitCellDef> cells;
    for (int r = 0; r < rows; ++r) {
        for (int c = 0; c < columns; ++c)
            cells.append(cell(rect(double(c) / columns, double(r) / rows, 1.0 / columns, 1.0 / rows), reveal));
    }
    return cells;
}

// A slanted divider: where it meets the top and the bottom edge of the canvas.
struct Divider
{
    double top;
    double bottom;
};

double dividerAt(const Divider &d, double y)
{
    return d.top + (d.bottom - d.top) * y;
}

// Columns between slanted dividers, left to right. A column whose `split` entry is true is cut in
// two at mid height (top cell first).
QVector<SplitCellDef> slantedColumns(QVector<Divider> dividers, const QVector<bool> &split, SplitReveal reveal)
{
    dividers.prepend({0.0, 0.0});
    dividers.append({1.0, 1.0});
    QVector<SplitCellDef> cells;
    for (int c = 0; c + 1 < dividers.size(); ++c) {
        const Divider &l = dividers.at(c);
        const Divider &r = dividers.at(c + 1);
        if (c < split.size() && split.at(c)) {
            cells.append(cell({QPointF(l.top, 0), QPointF(r.top, 0), QPointF(dividerAt(r, 0.5), 0.5),
                               QPointF(dividerAt(l, 0.5), 0.5)},
                              reveal));
            cells.append(cell({QPointF(dividerAt(l, 0.5), 0.5), QPointF(dividerAt(r, 0.5), 0.5),
                               QPointF(r.bottom, 1), QPointF(l.bottom, 1)},
                              reveal));
        } else {
            cells.append(cell({QPointF(l.top, 0), QPointF(r.top, 0), QPointF(r.bottom, 1), QPointF(l.bottom, 1)},
                              reveal));
        }
    }
    return cells;
}

// A circle cutting the canvas from the right: it meets the top and bottom edges at xEnd and reaches
// xMid at mid height. Worked out on a 1920 × 1080 canvas so it is round there.
constexpr double kRefW = 1920.0;
constexpr double kRefH = 1080.0;

struct Arc
{
    double cx = 0.0; // centre, reference px (at mid height)
    double r = 0.0;
};

Arc arcThrough(double xEnd, double xMid)
{
    const double e = xEnd * kRefW;
    const double m = xMid * kRefW;
    const double h = kRefH / 2.0;
    Arc arc;
    arc.cx = (e * e + h * h - m * m) / (2.0 * (e - m));
    arc.r = arc.cx - m;
    return arc;
}

double arcX(const Arc &arc, double y)
{
    const double dy = (y - 0.5) * kRefH;
    return (arc.cx - std::sqrt(std::max(0.0, arc.r * arc.r - dy * dy))) / kRefW;
}

// Points on the arc strictly between heights y0 and y1, `steps` segments.
void appendArc(Points &points, const Arc &arc, double y0, double y1, int steps)
{
    for (int i = 1; i < steps; ++i) {
        const double y = y0 + (y1 - y0) * double(i) / steps;
        points.append(QPointF(arcX(arc, y), y));
    }
}

// Layout 25: 1 top left, 2 the disc on the right, 3 bottom left.
QVector<SplitCellDef> discOnTheRight(double xEnd, double xMid)
{
    const Arc arc = arcThrough(xEnd, xMid);
    const double top = arcX(arc, 0.0);
    const double mid = arcX(arc, 0.5);
    const double bottom = arcX(arc, 1.0);

    Points one{QPointF(0, 0), QPointF(top, 0)};
    appendArc(one, arc, 0.0, 0.5, 8);
    one.append(QPointF(mid, 0.5));
    one.append(QPointF(0, 0.5));

    Points two{QPointF(top, 0), QPointF(1, 0), QPointF(1, 1), QPointF(bottom, 1)};
    appendArc(two, arc, 1.0, 0.0, 16);

    Points three{QPointF(0, 0.5), QPointF(mid, 0.5)};
    appendArc(three, arc, 0.5, 1.0, 8);
    three.append(QPointF(bottom, 1));
    three.append(QPointF(0, 1));

    return {cell(one), cell(two), cell(three)};
}

SplitCellDef mirrored(const SplitCellDef &source)
{
    SplitCellDef out = source;
    for (QPointF &p : out.points)
        p.setX(1.0 - p.x());
    switch (source.reveal) {
    case SplitReveal::Left:
        out.reveal = SplitReveal::Right;
        break;
    case SplitReveal::Right:
        out.reveal = SplitReveal::Left;
        break;
    default:
        break;
    }
    return out;
}

QVector<SplitLayoutDef> buildLayouts()
{
    using R = SplitReveal;
    QVector<SplitLayoutDef> out;
    const auto add = [&out](int number, QVector<SplitCellDef> cells, const QString &label = QString(),
                            const QString &id = QString()) {
        SplitLayoutDef layout;
        layout.number = number;
        layout.id = id.isEmpty() ? QStringLiteral("split%1").arg(number) : id;
        layout.label = label;
        layout.cells = std::move(cells);
        out.append(layout);
    };

    // Left and right; 2 is letterboxed in a 2.4:1 band.
    add(1, {cell(rect(0, 0, 0.5, 1)), cell(rect(0.5, 0, 0.5, 1))});
    add(2, {cell(rect(0, 0.125, 0.5, 0.75)), cell(rect(0.5, 0.125, 0.5, 0.75))});
    // Top and bottom.
    add(3, {cell(rect(0, 0, 1, 0.5)), cell(rect(0, 0.5, 1, 0.5))});
    // Diagonal.
    add(4, slantedColumns({{0.56, 0.44}}, {}, R::Auto));
    // Three columns, three rows: the middle one unfolds from its centre line.
    add(5, {cell(rect(0, 0, kThird, 1)), cell(rect(kThird, 0, kThird, 1), R::Vertical),
            cell(rect(2 * kThird, 0, kThird, 1))});
    add(6, {cell(rect(0, 0, 1, kThird)), cell(rect(0, kThird, 1, kThird), R::Horizontal),
            cell(rect(0, 2 * kThird, 1, kThird))});
    // Two stacked on the left, one on the right.
    add(7, {cell(rect(0, 0, 0.5, 0.5)), cell(rect(0, 0.5, 0.5, 0.5)), cell(rect(0.5, 0, 0.5, 1))});
    // One on top, two below (8 with a taller top).
    add(8, {cell(rect(0, 0, 1, 0.65)), cell(rect(0, 0.65, 0.5, 0.35)), cell(rect(0.5, 0.65, 0.5, 0.35))});
    add(9, {cell(rect(0, 0, 1, 0.5)), cell(rect(0, 0.5, 0.5, 0.5)), cell(rect(0.5, 0.5, 0.5, 0.5))});
    // Three slanted columns: leaning right, leaning left, narrow-topped middle, wide-topped middle.
    add(10, slantedColumns({{0.40, 0.27}, {0.73, 0.60}}, {}, R::Horizontal));
    add(11, slantedColumns({{0.27, 0.40}, {0.60, 0.73}}, {}, R::Horizontal));
    add(12, slantedColumns({{0.40, 0.28}, {0.60, 0.72}}, {}, R::Horizontal));
    add(13, slantedColumns({{0.28, 0.40}, {0.72, 0.60}}, {}, R::Horizontal));
    // One on the left, three stacked on the right.
    add(14, {cell(rect(0, 0, 0.55, 1)), cell(rect(0.55, 0, 0.45, kThird), R::Right),
             cell(rect(0.55, kThird, 0.45, kThird), R::Right), cell(rect(0.55, 2 * kThird, 0.45, kThird), R::Right)});
    // One over three, three over one.
    add(15, {cell(rect(0, 0, 1, 2 * kThird)), cell(rect(0, 2 * kThird, kThird, kThird)),
             cell(rect(kThird, 2 * kThird, kThird, kThird)), cell(rect(2 * kThird, 2 * kThird, kThird, kThird))});
    add(16, {cell(rect(0, 0, kThird, kThird)), cell(rect(kThird, 0, kThird, kThird)),
             cell(rect(2 * kThird, 0, kThird, kThird)), cell(rect(0, kThird, 1, 2 * kThird))});
    // Four columns, each unfolding from its centre line.
    add(17, grid(4, 1, R::Vertical));
    // 2 × 2.
    add(18, grid(2, 2, R::Auto));
    // Three stacked on the left, one on the right.
    add(19, {cell(rect(0, 0, 0.45, kThird)), cell(rect(0, kThird, 0.45, kThird), R::Left),
             cell(rect(0, 2 * kThird, 0.45, kThird)), cell(rect(0.45, 0, 0.55, 1))});
    // 3 × 2, each cell growing from its centre.
    add(20, grid(3, 2, R::Centre));
    // An arrow pointing right, three stacked beside it.
    {
        const double base = 0.44;
        const double tip = 0.55;
        const auto edge = [base, tip](double y) {
            return y <= 0.5 ? base + (tip - base) * y / 0.5 : tip - (tip - base) * (y - 0.5) / 0.5;
        };
        add(21, {cell({QPointF(0, 0), QPointF(base, 0), QPointF(tip, 0.5), QPointF(base, 1), QPointF(0, 1)}),
                 cell({QPointF(base, 0), QPointF(1, 0), QPointF(1, kThird), QPointF(edge(kThird), kThird)}, R::Right),
                 cell({QPointF(edge(kThird), kThird), QPointF(1, kThird), QPointF(1, 2 * kThird),
                       QPointF(edge(2 * kThird), 2 * kThird), QPointF(tip, 0.5)},
                      R::Right),
                 cell({QPointF(edge(2 * kThird), 2 * kThird), QPointF(1, 2 * kThird), QPointF(1, 1), QPointF(base, 1)},
                      R::Right)});
    }
    // Top and bottom with a slanted edge.
    add(22, {cell({QPointF(0, 0), QPointF(1, 0), QPointF(1, 0.38), QPointF(0, 0.62)}),
             cell({QPointF(0, 0.62), QPointF(1, 0.38), QPointF(1, 1), QPointF(0, 1)})});
    // A V: one above, two below.
    add(23, {cell({QPointF(0, 0), QPointF(1, 0), QPointF(1, 0.48), QPointF(0.5, 0.82), QPointF(0, 0.48)}),
             cell({QPointF(0, 0.48), QPointF(0.5, 0.82), QPointF(0.5, 1), QPointF(0, 1)}),
             cell({QPointF(0.5, 0.82), QPointF(1, 0.48), QPointF(1, 1), QPointF(0.5, 1)})});
    // Six in slanted columns of two.
    add(24, slantedColumns({{0.38, 0.28}, {0.72, 0.62}}, {true, true, true}, R::Auto));
    // A disc on the right, two on the left.
    add(25, discOnTheRight(0.62, 0.52));
    // Two, a slanted middle column, two.
    add(26, slantedColumns({{0.42, 0.26}, {0.74, 0.58}}, {true, false, true}, R::Auto));
    // Two, one across, two.
    add(27, {cell(rect(0, 0, 0.5, kThird)), cell(rect(0.5, 0, 0.5, kThird)),
             cell(rect(0, kThird, 1, kThird), R::Horizontal), cell(rect(0, 2 * kThird, 0.5, kThird)),
             cell(rect(0.5, 2 * kThird, 0.5, kThird))});
    // A pinwheel of five around a middle cell, each growing from its centre.
    add(28, {cell(rect(0, 0, 0.35, 0.67), R::Centre), cell(rect(0.35, 0, 0.65, 0.3), R::Centre),
             cell(rect(0.35, 0.3, 0.3, 0.37), R::Centre), cell(rect(0.65, 0.3, 0.35, 0.7), R::Centre),
             cell(rect(0, 0.67, 0.65, 0.33), R::Centre)});
    // A disc on the left, two on the right (25 mirrored, renumbered).
    {
        const QVector<SplitCellDef> right = discOnTheRight(0.62, 0.52);
        add(29, {mirrored(right.at(1)), mirrored(right.at(0)), mirrored(right.at(2))});
    }
    // An X: top, bottom, left, right.
    add(30, {cell({QPointF(0, 0), QPointF(1, 0), QPointF(0.5, 0.5)}),
             cell({QPointF(0, 1), QPointF(0.5, 0.5), QPointF(1, 1)}),
             cell({QPointF(0, 0), QPointF(0.5, 0.5), QPointF(0, 1)}),
             cell({QPointF(1, 0), QPointF(1, 1), QPointF(0.5, 0.5)})});

    // Dafin's own extras, kept from before the thirty.
    add(0, {cell(rect(0, 0, 0.5, 1)), cell(rect(0.5, 0, 0.5, 0.5)), cell(rect(0.5, 0.5, 0.5, 0.5))},
        QStringLiteral("1 and 2"), QStringLiteral("left1right2"));
    add(0, grid(3, 3, R::Auto), QStringLiteral("3 x 3"), QStringLiteral("grid3x3"));
    return out;
}

bool onLine(double a, double b, double value)
{
    return std::abs(a - value) < 1e-6 && std::abs(b - value) < 1e-6;
}

// Where the polygon's outline crosses the horizontal line at y (vertical: at x), as the lowest
// and highest crossing. Falls back to the point itself when nothing crosses.
std::pair<double, double> spanAt(const QVector<QPointF> &polygon, double at, bool horizontalLine, double fallback)
{
    double lo = std::numeric_limits<double>::max();
    double hi = std::numeric_limits<double>::lowest();
    const int n = polygon.size();
    for (int i = 0; i < n; ++i) {
        const QPointF a = polygon.at(i);
        const QPointF b = polygon.at((i + 1) % n);
        const double a0 = horizontalLine ? a.y() : a.x();
        const double b0 = horizontalLine ? b.y() : b.x();
        const double a1 = horizontalLine ? a.x() : a.y();
        const double b1 = horizontalLine ? b.x() : b.y();
        if (at < std::min(a0, b0) - 1e-9 || at > std::max(a0, b0) + 1e-9)
            continue;
        if (std::abs(b0 - a0) < 1e-12) {
            lo = std::min({lo, a1, b1});
            hi = std::max({hi, a1, b1});
            continue;
        }
        const double v = a1 + (b1 - a1) * (at - a0) / (b0 - a0);
        lo = std::min(lo, v);
        hi = std::max(hi, v);
    }
    if (lo > hi)
        return {fallback, fallback};
    return {lo, hi};
}

} // namespace

const QVector<SplitLayoutDef> &splitScreenLayoutDefs()
{
    static const QVector<SplitLayoutDef> layouts = buildLayouts();
    return layouts;
}

const SplitLayoutDef *splitScreenLayout(const QString &id)
{
    static const QHash<QString, QString> before = {
        {QStringLiteral("rows3"), QStringLiteral("split6")},       {QStringLiteral("diagonal2"), QStringLiteral("split4")},
        {QStringLiteral("grid2x2"), QStringLiteral("split18")},    {QStringLiteral("cols4"), QStringLiteral("split17")},
        {QStringLiteral("top3bottom1"), QStringLiteral("split16")}, {QStringLiteral("top1bottom3"), QStringLiteral("split15")},
        {QStringLiteral("leftright"), QStringLiteral("split1")},   {QStringLiteral("topbottom"), QStringLiteral("split3")},
        {QStringLiteral("cols3"), QStringLiteral("split5")}};
    const QString wanted = before.value(id, id);
    for (const SplitLayoutDef &layout : splitScreenLayoutDefs()) {
        if (layout.id == wanted)
            return &layout;
    }
    return nullptr;
}

TimeUs splitEntranceUs(int cellCount)
{
    return cellCount <= 0 ? 0 : TimeUs(cellCount - 1) * kSplitStaggerUs + kSplitRevealUs;
}

SplitReveal splitCellReveal(const SplitCellDef &cell)
{
    if (cell.reveal != SplitReveal::Auto)
        return cell.reveal;
    double left = 0.0;
    double right = 0.0;
    double top = 0.0;
    double bottom = 0.0;
    const int n = cell.points.size();
    for (int i = 0; i < n; ++i) {
        const QPointF a = cell.points.at(i);
        const QPointF b = cell.points.at((i + 1) % n);
        // Lengths on a 16:9 canvas, so a wide top edge outweighs a short side.
        const double across = std::abs(b.x() - a.x()) * 16.0;
        const double down = std::abs(b.y() - a.y()) * 9.0;
        if (onLine(a.x(), b.x(), 0.0))
            left += down;
        if (onLine(a.x(), b.x(), 1.0))
            right += down;
        if (onLine(a.y(), b.y(), 0.0))
            top += across;
        if (onLine(a.y(), b.y(), 1.0))
            bottom += across;
    }
    const double best = std::max({left, right, top, bottom});
    if (best <= 1e-9)
        return SplitReveal::Centre;
    if (left >= best - 1e-9)
        return SplitReveal::Left;
    if (right >= best - 1e-9)
        return SplitReveal::Right;
    if (top >= best - 1e-9)
        return SplitReveal::Top;
    return SplitReveal::Bottom;
}

QVector<QPointF> splitCellPolygonPx(const SplitCellDef &cell, const QRectF &bounds, double canvasW,
                                    double canvasH, double gapPx)
{
    const int n = cell.points.size();
    QVector<QPointF> px;
    px.reserve(n);
    for (const QPointF &p : cell.points)
        px.append(QPointF(p.x() * canvasW, p.y() * canvasH));
    const double inset = gapPx / 2.0;
    if (n < 3 || inset <= 0.0)
        return px;

    // Each edge's line, moved inwards unless the edge is on the layout's outline.
    const QPolygonF polygon(px);
    QVector<QPointF> origins(n);
    QVector<QPointF> directions(n);
    for (int i = 0; i < n; ++i) {
        const QPointF a = px.at(i);
        const QPointF b = px.at((i + 1) % n);
        const QPointF fa = cell.points.at(i);
        const QPointF fb = cell.points.at((i + 1) % n);
        const bool outline = onLine(fa.x(), fb.x(), bounds.left()) || onLine(fa.x(), fb.x(), bounds.right())
                             || onLine(fa.y(), fb.y(), bounds.top()) || onLine(fa.y(), fb.y(), bounds.bottom());
        const QPointF d = b - a;
        directions[i] = d;
        const double length = std::hypot(d.x(), d.y());
        if (outline || length <= 1e-9) {
            origins[i] = a;
            continue;
        }
        QPointF normal(-d.y() / length, d.x() / length);
        if (!polygon.containsPoint((a + b) / 2.0 + normal * 0.5, Qt::OddEvenFill))
            normal = -normal;
        origins[i] = a + normal * inset;
    }

    // Each corner is where the moved lines of its two edges cross.
    QVector<QPointF> out;
    out.reserve(n);
    for (int i = 0; i < n; ++i) {
        const int prev = (i + n - 1) % n;
        const QPointF p = origins.at(prev);
        const QPointF r = directions.at(prev);
        const QPointF q = origins.at(i);
        const QPointF s = directions.at(i);
        const double cross = r.x() * s.y() - r.y() * s.x();
        const double scale = std::max(1.0, std::hypot(r.x(), r.y()) * std::hypot(s.x(), s.y()));
        if (std::abs(cross) < 1e-9 * scale) {
            out.append(q);
            continue;
        }
        const QPointF qp = q - p;
        const double t = (qp.x() * s.y() - qp.y() * s.x()) / cross;
        out.append(p + r * t);
    }
    return out;
}

QVector<QPointF> splitCollapsedPolygon(const QVector<QPointF> &polygon, SplitReveal reveal)
{
    if (polygon.isEmpty())
        return polygon;
    double minX = polygon.first().x();
    double maxX = minX;
    double minY = polygon.first().y();
    double maxY = minY;
    for (const QPointF &p : polygon) {
        minX = std::min(minX, p.x());
        maxX = std::max(maxX, p.x());
        minY = std::min(minY, p.y());
        maxY = std::max(maxY, p.y());
    }
    const QPointF centre((minX + maxX) / 2.0, (minY + maxY) / 2.0);

    QVector<QPointF> out;
    out.reserve(polygon.size());
    for (const QPointF &p : polygon) {
        switch (reveal) {
        case SplitReveal::Left:
            out.append(QPointF(spanAt(polygon, p.y(), true, p.x()).first, p.y()));
            break;
        case SplitReveal::Right:
            out.append(QPointF(spanAt(polygon, p.y(), true, p.x()).second, p.y()));
            break;
        case SplitReveal::Top:
            out.append(QPointF(p.x(), spanAt(polygon, p.x(), false, p.y()).first));
            break;
        case SplitReveal::Bottom:
            out.append(QPointF(p.x(), spanAt(polygon, p.x(), false, p.y()).second));
            break;
        case SplitReveal::Horizontal: {
            const auto span = spanAt(polygon, p.y(), true, p.x());
            out.append(QPointF((span.first + span.second) / 2.0, p.y()));
            break;
        }
        case SplitReveal::Vertical: {
            const auto span = spanAt(polygon, p.x(), false, p.y());
            out.append(QPointF(p.x(), (span.first + span.second) / 2.0));
            break;
        }
        case SplitReveal::Auto:
        case SplitReveal::Centre:
            out.append(centre);
            break;
        }
    }
    return out;
}

QMap<TimeUs, QVector<QPointF>> splitRevealKeys(const QVector<QPointF> &full, SplitReveal reveal, TimeUs delayUs)
{
    QMap<TimeUs, QVector<QPointF>> keys;
    if (full.size() < 3)
        return keys;
    const QVector<QPointF> start = splitCollapsedPolygon(full, reveal);
    delayUs = std::max<TimeUs>(0, delayUs);
    keys.insert(0, start);
    keys.insert(delayUs, start);
    // Mask paths move linearly between keys; four steps of a cubic ease-out read as one smooth move.
    for (const double f : {0.25, 0.5, 0.75, 1.0}) {
        const double eased = 1.0 - std::pow(1.0 - f, 3.0);
        QVector<QPointF> shape;
        shape.reserve(full.size());
        for (int i = 0; i < full.size(); ++i)
            shape.append(start.at(i) + (full.at(i) - start.at(i)) * eased);
        keys.insert(delayUs + TimeUs(std::llround(double(kSplitRevealUs) * f)), shape);
    }
    return keys;
}

int splitCellAt(const SplitLayoutDef &layout, const QPointF &canvasFraction)
{
    for (int i = 0; i < layout.cells.size(); ++i) {
        if (QPolygonF(layout.cells.at(i).points).containsPoint(canvasFraction, Qt::OddEvenFill))
            return i;
    }
    return -1;
}

QRectF splitLayoutBounds(const SplitLayoutDef &layout)
{
    QPolygonF all;
    for (const SplitCellDef &cell : layout.cells)
        all += QPolygonF(cell.points);
    return all.isEmpty() ? QRectF(0, 0, 1, 1) : all.boundingRect();
}

} // namespace drift
