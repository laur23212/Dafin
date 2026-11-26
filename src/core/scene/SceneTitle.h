#pragma once

// Dafin: Scene Title v1 (docs/scene-titles.md) — loading, params, expressions, timing and
// constraints. No pixels and no Skia here: engine/render2d/scene/ScenePainter draws what this
// resolves. tools/scene-ref/scene_core.py is the reference this mirrors value for value.

#include <QHash>
#include <QJsonObject>
#include <QList>
#include <QPointF>
#include <QRectF>
#include <QString>
#include <QStringList>
#include <QVariant>

#include <functional>
#include <optional>

namespace drift::scene {

inline constexpr const char *kFormat = "dafin.scene-title";
inline constexpr int kVersion = 1;

enum class Profile { Sprite2x, Mask1x, Pil1x };

struct Rgba
{
    int r = 0, g = 0, b = 0, a = 255;
    bool operator==(const Rgba &o) const { return r == o.r && g == o.g && b == o.b && a == o.a; }
};

struct Ease
{
    enum Kind { Linear, Smoothstep, OutCubic, OutQuart, Smootherstep, Bezier };
    Kind kind = Smoothstep;
    double x1 = 0, y1 = 0, x2 = 1, y2 = 1;
    double operator()(double u) const;
};

// One "in" or "out" block. `values` holds from (in) or to (out), by channel name.
struct TimedBlock
{
    bool present = false;
    double start = 0.0;
    double dur = 0.0;
    Ease ease;
    QList<QPair<QString, double>> values;
};

struct Channels
{
    double opacity = 1.0, wipe = 1.0, progress = 1.0, scale = 1.0, dx = 0.0, dy = 0.0;
    double vscale = 1.0; // collapse to the vertical centre (pil1x accents and rules)
    bool operator==(const Channels &o) const
    {
        return opacity == o.opacity && wipe == o.wipe && progress == o.progress && scale == o.scale
            && dx == o.dx && dy == o.dy && vscale == o.vscale;
    }
};

struct Actor
{
    QString id, role, type; // type: text rect roundRect line route ellipse ring group
    QString parent;         // id of the enclosing group, empty at top level
    TimedBlock in, out;
    QList<TimedBlock> inMore, outMore; // "in"/"out" given as arrays: one curve per channel group
    QStringList dependsOn;
    // pil1x
    struct Condition { QString actor; QString channel; double gt = 0.0; }; // actor empty = self
    QList<Condition> visibleWhen;
    QString reveal;           // text: "x" (from the left) or "y" (from the bottom), driven by wipe
    bool wipeSnapInt1 = false; // rect: x1 = x0 + max(1, int(w·wipe)) (Stage05 A)
    bool collapseEnds = false; // line: vscale trims both ends of the rest line (Stage05 A)
    QList<double> segmentWeights; // route: share of progress per segment
    bool hasCenter = false;    // ellipse/ring given as center + radius; radius scales with "scale"
    QPointF center;
    double circleRadius = 0.0;
    bool pane = false;         // group: children drawn into one layer, composited once
    bool truncOffsets = false; // "offsetSnap": "trunc" — the Stage13 keycaps snapped dy with int()

    // text
    QString text, font;
    double size = 0.0;
    Rgba color;
    QPointF baseline;       // canonical anchor; legacy anchors are kept apart
    QString anchorKind = QStringLiteral("baseline"); // "baseline", "sprite" or "lt"
    QPointF anchorPoint;    // the legacy anchor's point when anchorKind != baseline
    double letterSpacing = 0.0;
    QString clipActor;      // clipTo
    double clipInset[4] = {0, 0, 0, 0};
    // clipRect (mask1x): a fixed window whose top rises with the actor's entrance:
    // top = box.y0 + snap((1 - in-phase) * rise), PIL-inclusive corners (Stage03b A/C name reveals)
    bool hasClipRect = false;
    double clipRect[4] = {0, 0, 0, 0};
    double clipRise = 0.0;
    bool clipRiseRound = false; // "riseSnap": "round" (half to even); default "trunc" (int())
    // flattened-group child alignment ("align": {"h":"center","v":"center","dy":..})
    bool centerInGroup = false;
    double centerDy = 0.0;
    bool alignTop = false;  // "v": "top": the ink top sits at centerTop (group px)
    double centerTop = 0.0;

    // box shapes
    bool hasBox = false;
    double box[4] = {0, 0, 0, 0}; // PIL inclusive corners
    double radius = 0.0;
    bool hasFill = false, hasStroke = false;
    Rgba fill, strokeColor;
    double stroke = 0.0;
    QString fitActor;
    double fitPad = 0.0, fitMin = 0.0;
    double fitMax = -1.0;   // fitTo.max: cap on the fitted width (< 0 = none)
    bool wipeDown = false;  // "wipeDir": "down": the wipe grows the height instead of the width
    QStringList widthOf;
    double restWidth = -1.0; // resolved by resolveConstraints; < 0 = box width

    // line / route
    QPointF from, to;
    QList<QPointF> points;
    double width = 1.0;
    bool mixWith = false;

    // group
    QPointF groupAt;
    double groupW = 0.0, groupH = 0.0;
    bool flatten = false;

    bool isShapeBox() const { return hasBox; }
    double boxWidth() const { return restWidth >= 0.0 ? restWidth : box[2] - box[0]; }
};

struct FontSpec
{
    QString key, family, file, package, sha256;
    int weight = 400;
};

struct ParamSpec
{
    QString id, type, label;
    QVariant defaultValue;
    QVariant min, max; // number or expression string; invalid = none
    bool clamp = false;
    QStringList options;
};

struct Scene
{
    QJsonObject raw;
    QString id, label, category;
    int width = 1920, height = 1080;
    double duration = 6.0, fps = 25.0;
    double cadence = 0.0; // 0 = "match"
    Profile profile = Profile::Sprite2x;
    Rgba mixColor{9, 12, 17, 255};
    QHash<QString, FontSpec> fonts;
    QList<ParamSpec> params;
    QVariantMap values;    // resolved param values
    QList<Actor> actors;   // flat, draw order, groups before their children
    double holdStart = 0.0, holdEnd = 0.0;
    // exitWipe: the whole title is cut from the right as it closes (pil1x Stage05)
    TimedBlock exitWipe;
    double exitWipeLeft = 0.0, exitWipeRight = 0.0;

    const Actor *actor(const QString &id) const;
    int indexOf(const QString &id) const;
};

// Parses and resolves a scene with the given param overrides (param id -> JSON value). Unknown
// top-level keys are ignored; every error the spec names stops the load with a message.
std::optional<Scene> loadScene(const QJsonObject &doc, const QJsonObject &overrides = {},
                               QString *error = nullptr);

// True when `doc` declares itself a Scene Title (what install and drop routing check first).
bool isSceneDocument(const QJsonObject &doc);

// The animated channels of one actor at design time t (seconds), groups not folded in.
Channels channels(const Actor &actor, double t);
// The channels the actor is drawn with: its own folded with every plain enclosing group's
// (offsets include the groups' "at"). `skip` is set when a flattened group draws it instead.
Channels drawChannels(const Scene &scene, const Actor &actor, double t, bool *skip);

// Clip time -> design time for a clip of `clipDuration` s (docs: Stretching to the clip length).
double timeMap(const Scene &scene, double clipDuration, double t);
// The cadence step: floor(t*c)/c when the scene steps slower than the project.
double cadenceTime(const Scene &scene, double t, double projectFps);
// Both, in the reference renderer's order (cadence first).
inline double designTime(const Scene &scene, double clipDuration, double projectFps, double t)
{
    return timeMap(scene, clipDuration, cadenceTime(scene, t, projectFps));
}

// Ink box of a text actor at rest, in design px (left, top, right, bottom of the drawn mask).
using InkFn = std::function<std::optional<QRectF>(const Actor &text)>;
// fitTo / widthOf: fills Actor::restWidth for every rect that has either. Must run again after
// any text change (the painter does, whenever it is built for new values).
bool resolveConstraints(Scene &scene, const InkFn &ink, QString *error = nullptr);
// A box actor's box at time t: its rest width scaled by the wipe (the mask1x "current box").
QRectF currentBox(const Actor &actor, const Channels &ch);

// Expressions and text slots, exposed for tests. Values are Python-like: qlonglong, double,
// bool, QString or QVariantList (a colour).
QVariant evalExpression(const QString &src, const QVariantMap &env, QString *error = nullptr);
QString formatText(const QString &tmpl, const QVariantMap &env, QString *error = nullptr);
QString formatValue(const QVariant &value, const QString &spec, QString *error = nullptr);
std::optional<Rgba> parseColor(const QVariant &value, const QVariantMap &palette);
QVariant jsonToValue(const QJsonValue &v);

// A stable key for a document + overrides, to cache resolved scenes and their painters.
quint64 sceneKey(const QJsonObject &doc, const QJsonObject &overrides);

// Inspector rows: id, type, label, value, default, min, max (resolved), options.
QVariantList paramRows(const Scene &scene);

} // namespace drift::scene
