#include "engine/packages/linked/FilmoraTitleConverter.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QHash>
#include <QSet>
#include <QtMath>
#include <QXmlStreamReader>

#include <algorithm>
#include <cmath>

namespace dafin::linked {

namespace {

// Filmora times are in 100 ns units (a 5 s title ends at 50 000 000).
constexpr double kFilmoraTicksPerSecond = 10'000'000.0;

double num(const QJsonValue &v, double fallback = 0.0)
{
    if (v.isDouble())
        return v.toDouble();
    if (v.isBool())
        return v.toBool() ? 1.0 : 0.0;
    bool ok = false;
    const double d = v.toVariant().toString().toDouble(&ok);
    return ok ? d : fallback;
}

QString hex2(int v)
{
    return QStringLiteral("%1").arg(qBound(0, v, 255), 2, 16, QLatin1Char('0'));
}

int alphaOf(const QJsonObject &o, const QString &key = QStringLiteral("Alpha"))
{
    return qBound(0, int(std::lround(num(o.value(key), 255))), 255);
}

// The rgb of a "#aarrggbb" colour scaled towards black, keeping its alpha.
QString darker(const QString &argb, double factor)
{
    if (argb.size() != 9)
        return argb;
    auto channel = [&](int at) { return int(std::lround(argb.mid(at, 2).toInt(nullptr, 16) * factor)); };
    return QLatin1Char('#') + argb.mid(1, 2) + hex2(channel(3)) + hex2(channel(5)) + hex2(channel(7));
}

QJsonObject solid(const QString &color)
{
    return QJsonObject{{QStringLiteral("kind"), QStringLiteral("solid")}, {QStringLiteral("color"), color}};
}

// A Filmora gradient block ({"Enable", "Angle", "GradType", "ColorPos": [{"Color", "Pos"}]}) as a
// Dafin gradient paint, or an empty object when it is off or has fewer than two colours.
QJsonObject gradientPaint(const QJsonObject &g, int alpha)
{
    if (num(g.value(QStringLiteral("Enable"))) < 0.5)
        return {};
    QList<QPair<double, QString>> stops;
    for (const QJsonValue &v : g.value(QStringLiteral("ColorPos")).toArray()) {
        const QJsonObject s = v.toObject();
        stops.append({qBound(0.0, num(s.value(QStringLiteral("Pos"))) / 100.0, 1.0),
                      filmoraColor(qint64(num(s.value(QStringLiteral("Color")))), alpha)});
    }
    if (stops.size() < 2)
        return {};
    std::stable_sort(stops.begin(), stops.end(), [](const auto &a, const auto &b) { return a.first < b.first; });
    QJsonArray out;
    for (const auto &s : stops)
        out.append(QJsonObject{{QStringLiteral("pos"), s.first}, {QStringLiteral("color"), s.second}});
    const int type = int(num(g.value(QStringLiteral("GradType"))));
    return QJsonObject{
        {QStringLiteral("kind"), QStringLiteral("gradient")},
        {QStringLiteral("gradient"),
         QJsonObject{{QStringLiteral("kind"), type == 1 ? QStringLiteral("radial") : QStringLiteral("linear")},
                     {QStringLiteral("angle"), std::fmod(num(g.value(QStringLiteral("Angle"))), 360.0)},
                     {QStringLiteral("space"), QStringLiteral("line")},
                     {QStringLiteral("stops"), out}}}};
}

QJsonObject layer(const QString &id, const QString &kind, const QJsonObject &paint)
{
    return QJsonObject{{QStringLiteral("id"), id}, {QStringLiteral("kind"), kind}, {QStringLiteral("paint"), paint}};
}

// Every text clip of the title, in timeline order (a title can stack several text layers).
QList<QJsonObject> textClips(const QJsonObject &root)
{
    QList<QJsonObject> clips;
    if (root.contains(QStringLiteral("scriptBuf")))
        clips.append(root);
    for (const QJsonValue &tl : root.value(QStringLiteral("subTimeline")).toObject()
                                    .value(QStringLiteral("timelineInfos")).toArray()) {
        for (const QJsonValue &track : tl.toObject().value(QStringLiteral("trackInfos")).toArray()) {
            for (const QJsonValue &clip : track.toObject().value(QStringLiteral("clipList")).toArray()) {
                const QJsonObject c = clip.toObject();
                if (c.contains(QStringLiteral("scriptBuf")))
                    clips.append(c);
            }
        }
    }
    return clips;
}

int countOtherClips(const QJsonObject &root)
{
    int n = 0;
    for (const QJsonValue &tl : root.value(QStringLiteral("subTimeline")).toObject()
                                    .value(QStringLiteral("timelineInfos")).toArray()) {
        for (const QJsonValue &track : tl.toObject().value(QStringLiteral("trackInfos")).toArray()) {
            for (const QJsonValue &clip : track.toObject().value(QStringLiteral("clipList")).toArray())
                n += clip.toObject().contains(QStringLiteral("scriptBuf")) ? 0 : 1;
        }
    }
    return n;
}

QJsonObject parseScript(const QJsonObject &clip)
{
    const QJsonValue v = clip.value(QStringLiteral("scriptBuf"));
    if (v.isObject())
        return v.toObject();
    return QJsonDocument::fromJson(v.toString().toUtf8()).object();
}

// The Filmora resource behind an effect: its id and name from the effect's userData (key 12,
// base64 JSON).
struct EffectRef
{
    qint64 id = 0;
    int resType = 0;
};

EffectRef effectRef(const QJsonObject &effect)
{
    EffectRef ref;
    for (const QJsonValue &v : effect.value(QStringLiteral("userData")).toArray()) {
        const QJsonObject u = v.toObject();
        if (u.value(QStringLiteral("key")).toInt() != 12)
            continue;
        const QJsonObject info =
            QJsonDocument::fromJson(QByteArray::fromBase64(u.value(QStringLiteral("data")).toString().toLatin1())).object();
        ref.id = info.value(QStringLiteral("id")).toVariant().toLongLong();
        ref.resType = info.value(QStringLiteral("res_type")).toInt();
    }
    return ref;
}

QHash<QString, QJsonValue> paramsOf(const QJsonObject &effect)
{
    QHash<QString, QJsonValue> out;
    for (const QJsonValue &v : effect.value(QStringLiteral("paramList")).toArray()) {
        const QJsonObject p = v.toObject();
        out.insert(p.value(QStringLiteral("name")).toString(),
                   p.value(QStringLiteral("fxParam")).toObject().value(QStringLiteral("unValue")));
    }
    return out;
}

QString effectKey(const QString &display, QSet<QString> *taken)
{
    QString slug;
    for (const QChar c : display.toLower()) {
        if (c.isLetterOrNumber() && c.unicode() < 128)
            slug += c;
        else if (!slug.isEmpty() && !slug.endsWith(QLatin1Char('-')))
            slug += QLatin1Char('-');
    }
    while (slug.endsWith(QLatin1Char('-')))
        slug.chop(1);
    QString key = (QStringLiteral("fx-") + slug).left(28);
    while (key.endsWith(QLatin1Char('-')))
        key.chop(1);
    QString unique = key;
    for (int n = 2; taken->contains(unique); ++n)
        unique = key + QString::number(n);
    taken->insert(unique);
    return unique;
}

bool nameHas(const QString &name, std::initializer_list<const char *> words)
{
    for (const char *w : words) {
        if (name.contains(QLatin1String(w)))
            return true;
    }
    return false;
}

const QSet<QString> &inOnlyPresets()
{
    static const QSet<QString> s{QStringLiteral("snap-together"), QStringLiteral("color-wave"),
                                 QStringLiteral("drop"), QStringLiteral("stamp"), QStringLiteral("squash"),
                                 QStringLiteral("flicker-in")};
    return s;
}

const QSet<QString> &outOnlyPresets()
{
    static const QSet<QString> s{QStringLiteral("fade-shift"), QStringLiteral("fade-scale"),
                                 QStringLiteral("remove-by-word"), QStringLiteral("shrink"), QStringLiteral("fall")};
    return s;
}

} // namespace

QString filmoraColor(qint64 value, int alpha)
{
    // 0xRRGGBB, red in the high byte (not a Windows COLORREF): checked against the thumbnails of
    // 39 titles with flat coloured text (57 colours matched this order, none the reverse).
    const quint32 v = quint32(value) & 0xffffffu;
    return QLatin1Char('#') + hex2(alpha) + hex2(int((v >> 16) & 0xff)) + hex2(int((v >> 8) & 0xff))
           + hex2(int(v & 0xff));
}

TitleFont splitFontName(const QString &filmoraFontName)
{
    static const QList<QPair<QString, int>> kWeights = {
        {QStringLiteral("thin"), 100},       {QStringLiteral("hairline"), 100}, {QStringLiteral("extralight"), 200},
        {QStringLiteral("ultralight"), 200}, {QStringLiteral("light"), 300},    {QStringLiteral("regular"), 400},
        {QStringLiteral("normal"), 400},     {QStringLiteral("book"), 400},     {QStringLiteral("medium"), 500},
        {QStringLiteral("semibold"), 600},   {QStringLiteral("demibold"), 600}, {QStringLiteral("bold"), 700},
        {QStringLiteral("extrabold"), 800},  {QStringLiteral("ultrabold"), 800}, {QStringLiteral("heavy"), 900},
        {QStringLiteral("black"), 900},
    };
    TitleFont font;
    QStringList words = filmoraFontName.simplified().split(QLatin1Char(' '), Qt::SkipEmptyParts);
    // Style words at the end ("Bold Italic", "Extra Bold") come off the family name.
    while (words.size() > 1) {
        const QString last = words.last().toLower();
        if (last == QLatin1String("italic") || last == QLatin1String("oblique")) {
            font.italic = true;
            words.removeLast();
            continue;
        }
        // Two words first: "Extra Bold" is 800, not "Extra" + bold.
        const QString twoWords = words.size() > 2 ? (words.at(words.size() - 2) + words.last()).toLower() : QString();
        bool matched = false;
        for (const auto &w : kWeights) {
            if (!twoWords.isEmpty() && twoWords == w.first) {
                font.weight = w.second;
                words.removeLast();
                words.removeLast();
                matched = true;
                break;
            }
        }
        for (const auto &w : kWeights) {
            if (matched)
                break;
            if (last == w.first) {
                font.weight = w.second;
                words.removeLast();
                matched = true;
            }
        }
        if (!matched)
            break;
    }
    font.family = words.join(QLatin1Char(' '));
    return font;
}

QString dafinAnimationFor(const QString &filmoraName, const QString &slot, QJsonObject *params)
{
    const QString n = filmoraName.toLower();
    QString preset;
    if (slot == QLatin1String("loop")) {
        if (nameHas(n, {"swing", "pendulum"}))
            preset = QStringLiteral("swing");
        else if (nameHas(n, {"sway"}))
            preset = QStringLiteral("sway");
        else if (nameHas(n, {"wave"}))
            preset = QStringLiteral("wave");
        else if (nameHas(n, {"pulse", "heart", "beat"}))
            preset = QStringLiteral("pulse");
        else if (nameHas(n, {"breath"}))
            preset = QStringLiteral("breathe");
        else if (nameHas(n, {"float", "hover"}))
            preset = QStringLiteral("float");
        else if (nameHas(n, {"shake", "jitter", "vibrat", "tremble"}))
            preset = QStringLiteral("jitter");
        else if (nameHas(n, {"flicker", "blink", "neon"}))
            preset = QStringLiteral("flicker");
        else if (nameHas(n, {"zoom", "scale"}))
            preset = QStringLiteral("slow-zoom");
        return preset;
    }
    const bool exit = slot == QLatin1String("out");
    if (nameHas(n, {"typewriter", "typing", "type "}))
        preset = QStringLiteral("typewriter");
    else if (nameHas(n, {"glitch"}))
        preset = QStringLiteral("glitch-in");
    else if (nameHas(n, {"blur"}))
        preset = nameHas(n, {"word"}) ? QStringLiteral("blur-in-words") : QStringLiteral("blur-in");
    else if (nameHas(n, {"zoom out", "zoom-out", "shrink"}))
        preset = exit ? QStringLiteral("shrink") : QStringLiteral("zoom-out");
    else if (nameHas(n, {"zoom", "scale", "grow", "enlarge"}))
        preset = QStringLiteral("zoom-in");
    else if (nameHas(n, {"pop", "spring", "elastic", "jelly"}))
        preset = QStringLiteral("pop");
    else if (nameHas(n, {"bounce"}))
        preset = QStringLiteral("bounce");
    else if (nameHas(n, {"drop", "fall"}))
        preset = exit ? QStringLiteral("fall") : QStringLiteral("drop");
    else if (nameHas(n, {"rotat", "spin", "roll", "twist"}))
        preset = QStringLiteral("spin-in");
    else if (nameHas(n, {"flip"}))
        preset = QStringLiteral("flip");
    else if (nameHas(n, {"wipe", "reveal", "mask", "erase"}))
        preset = QStringLiteral("wipe");
    else if (nameHas(n, {"pixel", "evaporat"}))
        preset = QStringLiteral("blur-in");
    else if (nameHas(n, {"skew"}))
        preset = QStringLiteral("skew-slide");
    else if (nameHas(n, {"door"}))
        preset = QStringLiteral("flip");
    else if (nameHas(n, {"slide", "fly", "move", "push", "shift", "insert"})) {
        if (nameHas(n, {"left"}))
            preset = QStringLiteral("slide-left");
        else if (nameHas(n, {"right"}))
            preset = QStringLiteral("slide-right");
        else if (nameHas(n, {"down", "top"}))
            preset = QStringLiteral("slide-down");
        else
            preset = QStringLiteral("slide-up");
    } else if (nameHas(n, {"rise", " up", "ascend"}))
        preset = QStringLiteral("rise");
    else if (nameHas(n, {"flash", "flicker", "strobe"}))
        preset = exit ? QStringLiteral("fade") : QStringLiteral("flicker-in");
    else if (nameHas(n, {"wavy", "wave"})) {
        preset = QStringLiteral("leap-in");
        if (params) {
            params->insert(QStringLiteral("offsetX"), 0.0);
            params->insert(QStringLiteral("offsetY"), 0.4);
            params->insert(QStringLiteral("angle"), 0.0);
            params->insert(QStringLiteral("from"), 0.0);
        }
    } else if (nameHas(n, {"random"})) {
        preset = QStringLiteral("fade");
        if (params) {
            params->insert(QStringLiteral("unit"), QStringLiteral("character"));
            params->insert(QStringLiteral("order"), QStringLiteral("random"));
            params->insert(QStringLiteral("stagger"), 0.05);
        }
    } else if (nameHas(n, {"rain"}))
        preset = QStringLiteral("rain");
    else if (nameHas(n, {"converge", "gather", "assemble"}))
        preset = QStringLiteral("converge");
    else if (nameHas(n, {"stamp", "slam", "punch", "smash"}))
        preset = exit ? QStringLiteral("fade-scale") : QStringLiteral("stamp");
    else if (nameHas(n, {"word"}))
        preset = exit ? QStringLiteral("remove-by-word") : QStringLiteral("rise-by-word");
    else if (nameHas(n, {"verbatim"})) {
        // "Appearing Verbatim" / "Disappear Verbatim" (Filmora's MG_2d_09): letter by letter.
        preset = QStringLiteral("fade");
        if (params) {
            params->insert(QStringLiteral("unit"), QStringLiteral("character"));
            params->insert(QStringLiteral("stagger"), 0.05);
        }
    } else if (nameHas(n, {"letter", "char"}))
        preset = QStringLiteral("fade-up-char");
    else if (nameHas(n, {"center", "centre", "middle"})) {
        // "Center Gradually Appears": letters fade in from the middle outwards.
        preset = QStringLiteral("fade");
        if (params) {
            params->insert(QStringLiteral("unit"), QStringLiteral("character"));
            params->insert(QStringLiteral("order"), QStringLiteral("centerOut"));
            params->insert(QStringLiteral("stagger"), 0.04);
        }
    } else
        preset = QStringLiteral("fade"); // "Fade", "Gradually Appears", "Dissolve" and anything unknown

    if (exit && inOnlyPresets().contains(preset))
        preset = QStringLiteral("fade");
    if (!exit && outOnlyPresets().contains(preset))
        preset = QStringLiteral("fade");
    return preset;
}

QString dafinAnimationForId(const QString &filmoraId, const QString &slot, QJsonObject *params)
{
    // Filmora's text animation presets by their id (the title's "stringParam"), each as the Dafin
    // preset and settings that look closest. The same id serves the entrance and the exit (MG_2d_09
    // is "Appearing Verbatim" and "Disappear Verbatim"); the slot decides the direction. Matched
    // against the animation thumbnails in a linked library (their mid-animation frame).
    struct Entry
    {
        const char *id;
        const char *preset;
        QList<QPair<const char *, QJsonValue>> params;
    };
    static const QList<Entry> kTable = {
        // Letter by letter: each letter fades in, overlapping the next.
        {"MG_2d_01", "pop", {{"unit", "character"}, {"stagger", 0.06}, {"duration", 0.3}, {"from", 80}}},
        {"MG_2d_09", "fade", {{"unit", "character"}, {"stagger", 0.05}, {"duration", 0.35}}},
        // Letters jump up from below, small and tilted.
        {"MG_2d_02", "leap-in", {}},
        // Every letter grows from small at once.
        {"MG_2d_04", "pop", {{"unit", "character"}, {"stagger", 0.0}, {"from", 30}, {"duration", 0.5}}},
        // Typed: each letter pops from nothing.
        {"MG_2d_05", "pop", {{"unit", "character"}, {"stagger", 0.08}, {"from", 0}, {"duration", 0.15}}},
        // Letters switch on in a random order.
        {"MG_2d_06", "typewriter", {{"order", "random"}, {"stagger", 0.05}}},
        // Typed, each letter rising into place.
        {"MG_2d_08", "fade-up-char", {{"rise", 0.6}, {"stagger", 0.08}, {"duration", 0.25}}},
        // Letters turn into place from below / a corner / the side.
        {"MG_2d_10", "tumble-in", {{"offsetY", 0.8}, {"angle", -90}}},
        {"MG_2d_11", "tumble-in", {{"offsetX", -0.8}, {"offsetY", 0.8}, {"angle", -120}, {"from", 40}}},
        {"MG_2d_12", "tumble-in", {{"offsetX", 0.8}, {"offsetY", 0.0}, {"angle", 120}}},
        {"MG_2d_13", "tumble-in", {{"offsetY", 0.3}, {"angle", -180}, {"from", 60}}},
        {"MG_2d_15", "tumble-in", {{"offsetY", -1.0}, {"angle", 0}, {"spin", 90}, {"spread", 1.0}, {"from", 30}}},
        {"MG_2d_16", "tumble-in", {{"offsetX", -0.6}, {"offsetY", -0.6}, {"angle", -90}, {"from", 60}}},
        // Each letter pops up past its size and settles.
        {"MG_2d_20", "pop", {{"unit", "character"}, {"stagger", 0.07}, {"from", 0}, {"duration", 0.4}}},
        // Letters start bunched together and open out from the middle.
        {"MG_2d_21", "bunch-open", {}},
        // Each letter starts enlarged and shrinks into place.
        {"MG_2d_22", "pop", {{"unit", "character"}, {"stagger", 0.08}, {"from", 200}, {"duration", 0.4}, {"ease", "easeOut"}}},
        // Letters turn around their upright axis (squeezed flat, then open), the last letter
        // first: in Filmora's frame the first letter is the one still turning.
        {"MG_2d_23", "flip", {{"unit", "character"}, {"stagger", 0.06}, {"order", "backward"}}},
        {"MG_2d_42", "flip", {{"unit", "character"}, {"stagger", 0.05}, {"order", "backward"}}},
        {"MG_2d_43", "flip", {{"unit", "character"}, {"stagger", 0.08}}},
        // Letters bob at different heights and settle.
        {"MG_2d_25", "scatter-in", {{"spreadX", 0.0}, {"spreadY", 0.5}, {"spin", 0}, {"sizes", 0}, {"order", "forward"}, {"stagger", 0.04}, {"ease", "back"}}},
        {"MG_2d_32", "fade", {{"unit", "character"}, {"order", "random"}, {"stagger", 0.05}, {"duration", 0.3}}},
        {"MG_2d_33", "shake-in", {}},
        {"MG_2d_34", "scatter-in", {}},
        {"MG_2d_40", "leap-in", {{"offsetX", 0.0}, {"offsetY", 0.4}, {"angle", 0}, {"from", 0}, {"stagger", 0.05}}},
        // Spaced-out letters close in from the centre.
        {"MG_2d_41", "converge", {{"order", "centerOut"}, {"spread", 0.8}, {"blur", 0}, {"stagger", 0.0}}},
        // Typing with a cursor.
        {"Char_Cursor_01", "typewriter", {{"caret", true}}},
        {"Char_Cursor_02", "typewriter", {{"caret", true}}},
        // Filmora's "Dir Insert" moves (the whole text).
        {"C6163B42-A2C4-45E2-8D5F-347A0CCC2631", "slide-up", {}},
        {"09913F15-9C83-438F-A226-0EB26E8B8F7A", "slide-up", {}},
        {"8C3B822D-8C59-4A7B-AF96-56E280CFB7C0", "slide-down", {}},
        {"7A5B04FB-9AE0-46e7-B6CC-62BA64D8E16D", "slide-down", {}},
        {"0A6EE4AB-01BC-4D82-AA52-03FFA6F708DC", "slide-left", {}},
        {"13C68E5A-746D-4645-8A84-52CBBB7F113B", "slide-right", {}},
    };
    if (slot == QLatin1String("loop"))
        return {};
    for (const Entry &e : kTable) {
        if (filmoraId.compare(QLatin1String(e.id), Qt::CaseInsensitive) != 0)
            continue;
        if (params) {
            for (const auto &p : e.params)
                params->insert(QLatin1String(p.first), p.second);
        }
        return QLatin1String(e.preset);
    }
    return {};
}

namespace {

// "#aarrggbb" -> [r, g, b, a] (a Scene Title colour).
QJsonArray sceneRgba(const QString &argb)
{
    if (argb.size() != 9)
        return QJsonArray{255, 255, 255, 255};
    auto at = [&](int i) { return argb.mid(i, 2).toInt(nullptr, 16); };
    return QJsonArray{at(3), at(5), at(7), at(1)};
}

// A title of several text elements as a Scene Title: each element a centred text in a flattened
// group where Filmora placed it, its text a param (editable in the inspector), fading in and out.
// Scene texts are one flat colour, so outlines, shadows, gradients and effects are named in
// `leftOut`, not imitated.
QJsonObject sceneForTexts(const QList<QPair<QJsonObject, QJsonObject>> &texts, const QJsonObject &root,
                          const QString &id, const QString &label, const QString &category,
                          const TitleContext &context, QStringList *leftOut)
{
    auto note = [&](const QString &s) {
        if (!leftOut->contains(s))
            leftOut->append(s);
    };
    double seconds = num(root.value(QStringLiteral("tlEnd"))) / kFilmoraTicksPerSecond;
    if (seconds <= 0.0)
        seconds = num(texts.first().first.value(QStringLiteral("tlEnd"))) / kFilmoraTicksPerSecond;
    if (seconds < 1.0)
        seconds = 6.0;
    seconds = std::min(seconds, 60.0);
    const double fade = std::min(0.4, seconds / 6.0);

    QJsonObject fonts;
    QHash<QString, QString> fontKeys; // file|weight -> key
    QJsonArray params;
    QJsonArray actors;
    int n = 0;
    for (const auto &t : texts) {
        ++n;
        const QJsonObject &clip = t.first;
        const QJsonObject &script = t.second;
        const QJsonObject textData = script.value(QStringLiteral("TextData")).toArray().at(0).toObject();
        const QJsonObject basic = textData.value(QStringLiteral("Basic")).toObject();
        QString text = script.value(QStringLiteral("Text")).toString();
        if (text.trimmed().isEmpty())
            text = textData.value(QStringLiteral("CharData")).toString();
        text = text.remove(QLatin1Char('\r')).trimmed();
        if (text.contains(QLatin1Char('\n'))) {
            note(QStringLiteral("line breaks inside a text element (joined into one line)"));
            text = text.simplified();
        }

        double viewHeight = 1080.0;
        const QStringList view = script.value(QStringLiteral("ViewSize")).toString().split(QLatin1Char(':'));
        if (view.size() == 2 && view.at(1).toDouble() > 0)
            viewHeight = view.at(1).toDouble();
        const int size = int(std::lround(qBound(8.0, num(basic.value(QStringLiteral("FontSize")), 64.0) * 1080.0 / viewHeight, 600.0)));

        const QString fontName = basic.value(QStringLiteral("FontName")).toString().trimmed();
        TitleFont font = splitFontName(fontName);
        if (context.font) {
            if (const std::optional<TitleFont> known = context.font(fontName))
                font = *known;
        }
        if (num(basic.value(QStringLiteral("FontBold"))) > 0.5)
            font.weight = std::max(font.weight, 700);
        if (font.file.isEmpty()) {
            note(QStringLiteral("the font %1 (not in the folder; %2 is used)")
                     .arg(fontName.isEmpty() ? QStringLiteral("it names") : fontName, QLatin1String(kSceneFallbackFontFamily)));
            font = TitleFont{QLatin1String(kSceneFallbackFontFamily), 600, false, QLatin1String(kSceneFallbackFontFile)};
        }
        if (font.italic || num(basic.value(QStringLiteral("FontItalic"))) > 0.5)
            note(QStringLiteral("italic (Scene Titles draw the font file as it is)"));
        const QString fontId = font.file + QLatin1Char('|') + QString::number(font.weight);
        QString fontKey = fontKeys.value(fontId);
        if (fontKey.isEmpty()) {
            fontKey = QStringLiteral("f%1").arg(fontKeys.size() + 1);
            fontKeys.insert(fontId, fontKey);
            fonts.insert(fontKey, QJsonObject{{QStringLiteral("family"), font.family},
                                              {QStringLiteral("weight"), qBound(100, font.weight, 900)},
                                              {QStringLiteral("file"), font.file}});
        }

        const int alpha = alphaOf(basic, QStringLiteral("TextAlpha"));
        const QJsonArray textColors = basic.value(QStringLiteral("TextColor")).toArray();
        QString colour = filmoraColor(textColors.isEmpty() ? 0xffffff
                                                           : qint64(num(textColors.at(0).toObject().value(QStringLiteral("Color")), 0xffffff)),
                                      alpha);
        QJsonObject g = gradientPaint(script.value(QStringLiteral("Gradient")).toObject(), alpha);
        if (g.isEmpty())
            g = gradientPaint(basic.value(QStringLiteral("Gradient")).toObject(), alpha);
        if (!g.isEmpty()) {
            colour = g.value(QStringLiteral("gradient")).toObject().value(QStringLiteral("stops")).toArray().at(0)
                         .toObject().value(QStringLiteral("color")).toString(colour);
            note(QStringLiteral("gradient fills (the first colour is used)"));
        }
        const QJsonObject border = textData.value(QStringLiteral("Border")).toObject();
        if (num(border.value(QStringLiteral("Enable"))) > 0.5 && num(border.value(QStringLiteral("Size"))) > 0.0)
            note(QStringLiteral("outlines"));
        if (num(textData.value(QStringLiteral("Shadow")).toObject().value(QStringLiteral("Enable"))) > 0.5)
            note(QStringLiteral("shadows"));
        if (script.value(QStringLiteral("Texture")).toObject().value(QStringLiteral("Enable")).toInt() == 1)
            note(QStringLiteral("texture fills"));
        if (script.value(QStringLiteral("Background")).toObject().value(QStringLiteral("Enable")).toInt() == 1)
            note(QStringLiteral("background boxes"));

        double posX = 0.5;
        double posY = 0.5;
        for (const QJsonValue &chain : clip.value(QStringLiteral("effectChainList")).toArray()) {
            for (const QJsonValue &e : chain.toObject().value(QStringLiteral("effectList")).toArray()) {
                const QJsonObject eo = e.toObject();
                const QString effectId = eo.value(QStringLiteral("id")).toString();
                if (effectId == QLatin1String("video/effect/transform")) {
                    const auto p = paramsOf(eo);
                    posX = qBound(0.0, num(p.value(QStringLiteral("Position_x")), 0.5), 1.0);
                    posY = qBound(0.0, num(p.value(QStringLiteral("Position_y")), 0.5), 1.0);
                } else if (!effectId.startsWith(QLatin1String("video/effect/"))) {
                    note(QStringLiteral("effects on the text"));
                }
            }
        }
        for (const char *slot : {"inAnimation", "outAnimation", "loopAnimation"}) {
            if (!clip.value(QLatin1String(slot)).toObject().value(QStringLiteral("name")).toString().isEmpty())
                note(QStringLiteral("animations (every element fades in and out)"));
        }
        if (!clip.value(QStringLiteral("animation")).toObject().value(QStringLiteral("name")).toString().isEmpty())
            note(QStringLiteral("animations (every element fades in and out)"));

        const QString param = QStringLiteral("text%1").arg(n);
        params.append(QJsonObject{{QStringLiteral("id"), param},
                                  {QStringLiteral("type"), QStringLiteral("text")},
                                  {QStringLiteral("label"), QStringLiteral("Text %1").arg(n)},
                                  {QStringLiteral("default"), text}});
        const double cx = posX * 1920.0;
        const double cy = posY * 1080.0;
        const int h = int(std::ceil(size * 2.0));
        actors.append(QJsonObject{
            {QStringLiteral("id"), QStringLiteral("element%1").arg(n)},
            {QStringLiteral("role"), n == 1 ? QStringLiteral("title") : QStringLiteral("text")},
            {QStringLiteral("type"), QStringLiteral("group")},
            {QStringLiteral("flatten"), true},
            {QStringLiteral("at"), QJsonArray{std::round(cx - 960.0), std::round(cy - h / 2.0)}},
            {QStringLiteral("size"), QJsonArray{1920, h}},
            {QStringLiteral("in"), QJsonObject{{QStringLiteral("start"), 0.0},
                                               {QStringLiteral("dur"), fade},
                                               {QStringLiteral("from"), QJsonObject{{QStringLiteral("opacity"), 0}}}}},
            {QStringLiteral("out"), QJsonObject{{QStringLiteral("start"), seconds - fade},
                                                {QStringLiteral("dur"), fade},
                                                {QStringLiteral("to"), QJsonObject{{QStringLiteral("opacity"), 0}}}}},
            {QStringLiteral("children"),
             QJsonArray{QJsonObject{{QStringLiteral("id"), QStringLiteral("element%1Text").arg(n)},
                                    {QStringLiteral("role"), QStringLiteral("text")},
                                    {QStringLiteral("type"), QStringLiteral("text")},
                                    {QStringLiteral("text"), QLatin1Char('{') + param + QLatin1Char('}')},
                                    {QStringLiteral("font"), fontKey},
                                    {QStringLiteral("size"), size},
                                    {QStringLiteral("color"), sceneRgba(colour)},
                                    {QStringLiteral("align"), QJsonObject{{QStringLiteral("h"), QStringLiteral("center")},
                                                                          {QStringLiteral("v"), QStringLiteral("center")}}}}}},
        });
    }
    if (const int others = countOtherClips(root); others > 0)
        note(others == 1 ? QStringLiteral("1 shape or picture layer") : QStringLiteral("%1 shape or picture layers").arg(others));
    return QJsonObject{
        {QStringLiteral("format"), QStringLiteral("dafin.scene-title")},
        {QStringLiteral("version"), 1},
        {QStringLiteral("id"), id},
        {QStringLiteral("label"), label},
        {QStringLiteral("category"), category.isEmpty() ? QStringLiteral("Filmora") : category},
        {QStringLiteral("source"), QJsonObject{{QStringLiteral("origin"), QStringLiteral("linked Filmora folder")}}},
        {QStringLiteral("canvas"), QJsonObject{{QStringLiteral("width"), 1920}, {QStringLiteral("height"), 1080}}},
        {QStringLiteral("timing"), QJsonObject{{QStringLiteral("duration"), std::round(seconds * 100.0) / 100.0},
                                               {QStringLiteral("fps"), 25},
                                               {QStringLiteral("cadence"), QStringLiteral("match")}}},
        {QStringLiteral("render"), QJsonObject{{QStringLiteral("profile"), QStringLiteral("sprite2x")}}},
        {QStringLiteral("fonts"), fonts},
        {QStringLiteral("params"), params},
        {QStringLiteral("actors"), actors},
    };
}

} // namespace

TitleConversion convertFilmoraTitle(const QByteArray &dataJson, const QString &templateId,
                                    const QString &label, const QString &category,
                                    const TitleContext &context)
{
    TitleConversion out;
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(dataJson, &parseError);
    if (!doc.isObject()) {
        out.error = QStringLiteral("Its title file is damaged or could not be read");
        return out;
    }
    const QJsonObject root = doc.object();
    const QList<QJsonObject> clips = textClips(root);
    QList<QPair<QJsonObject, QJsonObject>> texts; // clip, its script
    for (const QJsonObject &clip : clips) {
        const QJsonObject script = parseScript(clip);
        QString text = script.value(QStringLiteral("Text")).toString();
        if (text.trimmed().isEmpty())
            text = script.value(QStringLiteral("TextData")).toArray().at(0).toObject()
                       .value(QStringLiteral("CharData")).toString();
        if (!text.trimmed().isEmpty())
            texts.append({clip, script});
    }
    if (texts.isEmpty()) {
        out.error = countOtherClips(root) > 0
                        ? QStringLiteral("Filmora title with no text in it (shapes or pictures only)")
                        : QStringLiteral("No text in this title");
        return out;
    }

    // The main text: the biggest one (a title with a small subtitle under it keeps the headline).
    auto fontSizeOf = [](const QJsonObject &script) {
        return num(script.value(QStringLiteral("TextData")).toArray().at(0).toObject()
                       .value(QStringLiteral("Basic")).toObject().value(QStringLiteral("FontSize")), 0.0);
    };
    int main = 0;
    for (int i = 1; i < texts.size(); ++i) {
        if (fontSizeOf(texts.at(i).second) > fontSizeOf(texts.at(main).second))
            main = i;
    }
    const QJsonObject clip = texts.at(main).first;
    const QJsonObject script = texts.at(main).second;
    const QJsonObject textData = script.value(QStringLiteral("TextData")).toArray().at(0).toObject();
    const QJsonObject basic = textData.value(QStringLiteral("Basic")).toObject();

    QStringList lines;
    for (const auto &t : texts) {
        QString text = t.second.value(QStringLiteral("Text")).toString();
        if (text.trimmed().isEmpty())
            text = t.second.value(QStringLiteral("TextData")).toArray().at(0).toObject()
                       .value(QStringLiteral("CharData")).toString();
        lines.append(text.remove(QLatin1Char('\r')).trimmed());
    }
    if (texts.size() > 1)
        out.leftOut.append(QStringLiteral("its %1 text layers are joined into one text").arg(texts.size()));
    const int others = countOtherClips(root);
    if (others > 0)
        out.leftOut.append(others == 1 ? QStringLiteral("1 shape or picture layer")
                                       : QStringLiteral("%1 shape or picture layers").arg(others));
    const QString sampleText = lines.join(QLatin1Char('\n'));

    // Sizes are pixels in Filmora's view (ViewSize "1920:1080"); Dafin's are pixels at 1080 p.
    double viewHeight = 1080.0;
    const QStringList view = script.value(QStringLiteral("ViewSize")).toString().split(QLatin1Char(':'));
    if (view.size() == 2 && view.at(1).toDouble() > 0)
        viewHeight = view.at(1).toDouble();
    const double scale = 1080.0 / viewHeight;

    QJsonObject style;
    const double pixelSize = qBound(4.0, num(basic.value(QStringLiteral("FontSize")), 64.0) * scale, 1000.0);
    // A whole number: Dafin reads pixelSize as an int and drops anything else.
    style.insert(QStringLiteral("pixelSize"), int(std::lround(pixelSize)));

    // Font: what the folder's own font files say, else the name split into family and style.
    const QString fontName = basic.value(QStringLiteral("FontName")).toString().trimmed();
    TitleFont font = splitFontName(fontName);
    if (context.font) {
        if (const std::optional<TitleFont> known = context.font(fontName))
            font = *known;
    }
    if (num(basic.value(QStringLiteral("FontBold"))) > 0.5)
        font.weight = std::max(font.weight, 700);
    if (num(basic.value(QStringLiteral("FontItalic"))) > 0.5)
        font.italic = true;
    if (!font.family.isEmpty())
        style.insert(QStringLiteral("fontFamily"), font.family);
    style.insert(QStringLiteral("fontWeight"), qBound(100, font.weight, 900));
    style.insert(QStringLiteral("italic"), font.italic);

    // Where it sits: Filmora places the title's box; Dafin aligns the text in the frame.
    double posX = 0.5;
    double posY = 0.5;
    for (const QJsonValue &chain : clip.value(QStringLiteral("effectChainList")).toArray()) {
        for (const QJsonValue &e : chain.toObject().value(QStringLiteral("effectList")).toArray()) {
            if (e.toObject().value(QStringLiteral("id")).toString() == QLatin1String("video/effect/transform")) {
                const auto p = paramsOf(e.toObject());
                posX = num(p.value(QStringLiteral("Position_x")), 0.5);
                posY = num(p.value(QStringLiteral("Position_y")), 0.5);
            }
        }
    }
    style.insert(QStringLiteral("align"), posX < 0.34 ? QStringLiteral("left")
                                          : posX > 0.66 ? QStringLiteral("right")
                                                        : QStringLiteral("center"));
    style.insert(QStringLiteral("valign"), posY < 0.34 ? QStringLiteral("top")
                                           : posY > 0.66 ? QStringLiteral("bottom")
                                                         : QStringLiteral("center"));

    const QJsonObject layout = script.value(QStringLiteral("Layout")).toObject();
    const double charSpace = num(textData.value(QStringLiteral("CharSpace")), num(layout.value(QStringLiteral("CharSpace"))));
    if (!qFuzzyIsNull(charSpace))
        style.insert(QStringLiteral("letterSpacing"), qBound(-50.0, charSpace * scale, 200.0));
    const double lineSpace = num(textData.value(QStringLiteral("LineSpace")), num(layout.value(QStringLiteral("LineSpace"))));
    if (!qFuzzyIsNull(lineSpace))
        style.insert(QStringLiteral("lineHeight"), qBound(0.6, 1.2 + lineSpace / 100.0, 3.0));
    if (std::abs(num(layout.value(QStringLiteral("Angle")))) > 0.5)
        out.leftOut.append(QStringLiteral("its tilt"));

    // Shading layers, bottom first: 3D depth or long shadow, shadow, glow, outline, fill.
    QJsonArray bottom; // extrudes
    QJsonArray middle; // shadows and glows
    QJsonArray top;    // outline and fill

    const int textAlpha = alphaOf(basic, QStringLiteral("TextAlpha"));
    QJsonObject fillPaint = gradientPaint(script.value(QStringLiteral("Gradient")).toObject(), textAlpha);
    if (fillPaint.isEmpty())
        fillPaint = gradientPaint(basic.value(QStringLiteral("Gradient")).toObject(), textAlpha);
    const QJsonArray textColors = basic.value(QStringLiteral("TextColor")).toArray();
    const QString fillColor =
        filmoraColor(textColors.isEmpty() ? 0xffffff : qint64(num(textColors.at(0).toObject().value(QStringLiteral("Color")), 0xffffff)),
                     textAlpha);
    if (fillPaint.isEmpty())
        fillPaint = solid(fillColor);
    if (textColors.size() > 1 && fillPaint.value(QStringLiteral("kind")).toString() == QLatin1String("solid"))
        out.leftOut.append(QStringLiteral("its per-letter colours (the first colour is used)"));
    if (script.value(QStringLiteral("Texture")).toObject().value(QStringLiteral("Enable")).toInt() == 1)
        out.leftOut.append(QStringLiteral("its texture fill"));

    const QJsonObject border = textData.value(QStringLiteral("Border")).toObject();
    if (num(border.value(QStringLiteral("Enable"))) > 0.5 && num(border.value(QStringLiteral("Size"))) > 0.0) {
        QJsonObject paint = gradientPaint(border.value(QStringLiteral("Gradient")).toObject(), alphaOf(border));
        if (paint.isEmpty())
            paint = solid(filmoraColor(qint64(num(border.value(QStringLiteral("Color")))), alphaOf(border)));
        QJsonObject stroke = layer(QStringLiteral("outline"), QStringLiteral("stroke"), paint);
        stroke.insert(QStringLiteral("width"), qBound(0.5, num(border.value(QStringLiteral("Size"))) * scale, 200.0));
        stroke.insert(QStringLiteral("strokeAlign"), QStringLiteral("outside"));
        if (num(border.value(QStringLiteral("BlurRadius"))) > 0)
            stroke.insert(QStringLiteral("blur"), qBound(0.0, num(border.value(QStringLiteral("BlurRadius"))) * scale, 100.0));
        top.append(stroke);
    }

    const QJsonObject shadow = textData.value(QStringLiteral("Shadow")).toObject();
    if (num(shadow.value(QStringLiteral("Enable"))) > 0.5) {
        // Direction is one of eight steps around the text, 3 = down-right (Filmora's default).
        const double angle = qDegreesToRadians(45.0 * num(shadow.value(QStringLiteral("Direction")), 3.0) - 90.0);
        const double distance = std::max(1.0, num(shadow.value(QStringLiteral("Distance")), 1.0)) * 4.0 * scale;
        QJsonObject s = layer(QStringLiteral("shadow"), QStringLiteral("shadow"),
                              solid(filmoraColor(qint64(num(shadow.value(QStringLiteral("Color")))), 255)));
        s.insert(QStringLiteral("opacity"), alphaOf(shadow) / 255.0);
        s.insert(QStringLiteral("offsetX"), std::round(std::cos(angle) * distance * 10.0) / 10.0);
        s.insert(QStringLiteral("offsetY"), std::round(std::sin(angle) * distance * 10.0) / 10.0);
        s.insert(QStringLiteral("blur"), qBound(0.0, num(shadow.value(QStringLiteral("BlurRadius"))) * scale, 100.0));
        middle.append(s);
    }

    // Effects on the text: converted Filmora filters become clip effects; Filmora's built-in
    // text looks are imitated with layers; the rest is named.
    QJsonArray effects;
    QSet<QString> keys;
    bool threeD = false;
    int threeDAnimation = 0;
    for (const QJsonValue &chain : clip.value(QStringLiteral("effectChainList")).toArray()) {
        for (const QJsonValue &value : chain.toObject().value(QStringLiteral("effectList")).toArray()) {
            const QJsonObject e = value.toObject();
            const QString id = e.value(QStringLiteral("id")).toString();
            if (id.startsWith(QLatin1String("video/effect/")))
                continue; // crop and transform: the clip's own framing
            const QString display = e.value(QStringLiteral("display")).toString();
            const EffectRef ref = effectRef(e);
            const auto params = paramsOf(e);

            if (ref.id > 0 && context.effect) {
                if (const std::optional<LinkedEffectInfo> info = context.effect(ref.id)) {
                    QJsonObject values;
                    for (const LinkedEffectInfo::Param &p : info->params) {
                        const QString filmoraName = p.identifier.mid(3); // "fm_" + name
                        if (!params.contains(filmoraName))
                            continue;
                        const double v = num(params.value(filmoraName));
                        if (p.type == QLatin1String("bool"))
                            values.insert(p.identifier, v > 0.5);
                        else
                            values.insert(p.identifier, qBound(p.min, v, p.max));
                    }
                    effects.append(QJsonObject{{QStringLiteral("key"), effectKey(display, &keys)},
                                               {QStringLiteral("effect"), info->dafinId},
                                               {QStringLiteral("parameters"), values}});
                    continue;
                }
            }
            if (display.compare(QLatin1String("Glow"), Qt::CaseInsensitive) == 0) {
                QJsonObject g = layer(QStringLiteral("glow"), QStringLiteral("glow"),
                                      solid(fillPaint.value(QStringLiteral("kind")).toString() == QLatin1String("solid")
                                                ? fillPaint.value(QStringLiteral("color")).toString()
                                                : fillColor));
                g.insert(QStringLiteral("blur"), qBound(2.0, num(params.value(QStringLiteral("radius")), 5.0) * 2.0 * scale + 4.0, 60.0));
                g.insert(QStringLiteral("opacity"), qBound(0.05, num(params.value(QStringLiteral("alpha")), 50.0) / 100.0, 1.0));
                middle.append(g);
                continue;
            }
            if (display.startsWith(QLatin1String("VectorTextShadow"), Qt::CaseInsensitive)) {
                // A long shadow: the letters stepped out at an angle in one flat colour.
                QJsonObject x = layer(QStringLiteral("longshadow"), QStringLiteral("extrude"),
                                      solid(filmoraColor(qint64(num(params.value(QStringLiteral("ShadowColor")), 0x191919)), 255)));
                x.insert(QStringLiteral("opacity"), qBound(0.0, num(params.value(QStringLiteral("ShadowAlpha")), 100.0) / 100.0, 1.0));
                x.insert(QStringLiteral("width"), qBound(2.0, num(params.value(QStringLiteral("Extrude")), 0.15) * pixelSize * 2.0, 400.0));
                x.insert(QStringLiteral("extrudeAngle"), num(params.value(QStringLiteral("angle")), 135.0));
                x.insert(QStringLiteral("extrudeSteps"), 32);
                x.insert(QStringLiteral("extrudeDarken"), 0.0);
                bottom.append(x);
                continue;
            }
            if (display.startsWith(QLatin1String("VectorText3D"), Qt::CaseInsensitive) || ref.resType == 43) {
                // 3D text: drawn as flat letters with a stepped depth under them.
                threeD = true;
                threeDAnimation = int(num(params.value(QStringLiteral("AnimationType"))));
                QJsonObject x = layer(QStringLiteral("depth"), QStringLiteral("extrude"), solid(darker(fillColor, 0.55)));
                x.insert(QStringLiteral("width"), qBound(3.0, num(params.value(QStringLiteral("Extrude")), 0.13) * pixelSize * 1.2, 200.0));
                x.insert(QStringLiteral("extrudeAngle"), 90.0);
                x.insert(QStringLiteral("extrudeSteps"), 12);
                x.insert(QStringLiteral("extrudeDarken"), 0.4);
                bottom.append(x);
                continue;
            }
            out.leftOut.append(QStringLiteral("the %1 effect").arg(display.isEmpty() ? QStringLiteral("unnamed") : display));
        }
    }
    if (threeD)
        out.leftOut.append(QStringLiteral("its 3D look (drawn as flat letters with depth)"));

    top.append(layer(QStringLiteral("fill"), QStringLiteral("fill"), fillPaint));
    QJsonArray layers;
    for (const QJsonArray *part : {&bottom, &middle, &top}) {
        for (const QJsonValue &v : *part)
            layers.append(v);
    }
    style.insert(QStringLiteral("layers"), layers);

    const QJsonObject background = script.value(QStringLiteral("Background")).toObject();
    if (num(background.value(QStringLiteral("Enable"))) > 0.5) {
        style.insert(QStringLiteral("boxEnabled"), true);
        style.insert(QStringLiteral("boxColor"),
                     filmoraColor(qint64(num(background.value(QStringLiteral("Color")))), alphaOf(background)));
        style.insert(QStringLiteral("boxPadding"), qBound(0.0, pixelSize * 0.2, 200.0));
        style.insert(QStringLiteral("boxRadius"), qBound(0.0, num(background.value(QStringLiteral("Radian"))) * scale, 200.0));
    }
    const bool underline = num(textData.value(QStringLiteral("Underline")), num(layout.value(QStringLiteral("Underline")))) > 0.5;
    if (underline) {
        const qint64 color = qint64(num(textData.value(QStringLiteral("UnderlineColor")), -1));
        style.insert(QStringLiteral("underlineEnabled"), true);
        style.insert(QStringLiteral("underlineColor"), color < 0 ? fillColor : filmoraColor(color, 255));
    }
    if (!effects.isEmpty())
        style.insert(QStringLiteral("effectPadding"), 48);

    // Animation: Filmora's in / out / loop animations, matched to Dafin's presets by Filmora's
    // preset id when it is one Dafin knows (dafinAnimationForId), else by name. An older title
    // keeps its animation in the clip's "animation" field instead, its length as a fraction of
    // the clip (inRatio / outRatio); a nameless one there is Filmora's "none".
    struct SlotAnim
    {
        QString name;
        QString id;
        double seconds = 0.0;
    };
    QHash<QString, SlotAnim> slotAnims;
    for (const QString &slot : {QStringLiteral("in"), QStringLiteral("out"), QStringLiteral("loop")}) {
        const QJsonObject a = clip.value(slot + QStringLiteral("Animation")).toObject();
        const QString name = a.value(QStringLiteral("name")).toString();
        if (!name.isEmpty())
            slotAnims.insert(slot, {name, a.value(QStringLiteral("stringParam")).toString(),
                                    num(a.value(QStringLiteral("duration"))) / kFilmoraTicksPerSecond});
    }
    const QJsonObject legacy = clip.value(QStringLiteral("animation")).toObject();
    const QString legacyName = legacy.value(QStringLiteral("name")).toString();
    if (!slotAnims.contains(QStringLiteral("in")) && !slotAnims.contains(QStringLiteral("out"))
        && !legacyName.isEmpty() && legacyName.compare(QLatin1String("No Animation"), Qt::CaseInsensitive) != 0) {
        const double clipSeconds = (num(clip.value(QStringLiteral("tlEnd"))) - num(clip.value(QStringLiteral("tlBegin"))))
                                   / kFilmoraTicksPerSecond;
        const QString id = legacy.value(QStringLiteral("stringParam")).toString();
        const double inRatio = num(legacy.value(QStringLiteral("inRatio")));
        const double outRatio = num(legacy.value(QStringLiteral("outRatio")));
        slotAnims.insert(QStringLiteral("in"), {legacyName, id, std::max(0.0, inRatio * clipSeconds)});
        if (outRatio > 0.0)
            slotAnims.insert(QStringLiteral("out"), {legacyName, id, outRatio * clipSeconds});
    }
    QJsonObject animation;
    for (const QString &slot : {QStringLiteral("in"), QStringLiteral("out"), QStringLiteral("loop")}) {
        if (!slotAnims.contains(slot))
            continue;
        const SlotAnim a = slotAnims.value(slot);
        QJsonObject params;
        QString preset = dafinAnimationForId(a.id, slot, &params);
        if (preset.isEmpty())
            preset = dafinAnimationFor(a.name, slot, &params);
        if (preset.isEmpty()) {
            out.leftOut.append(QStringLiteral("its \"%1\" animation").arg(a.name));
            continue;
        }
        if (slot != QLatin1String("loop")) {
            // Filmora's length is the whole animation. A letter-by-letter one keeps its own pace
            // per letter and is fitted whole into that length (timing.maxReadMs, the entrance);
            // a whole-text one simply takes it as its duration.
            const QString unit = params.value(QStringLiteral("unit")).toString();
            const bool perLetter = preset == QLatin1String("typewriter") || unit == QLatin1String("character")
                                   || unit == QLatin1String("word") || a.id.startsWith(QLatin1String("MG_2d"));
            const double seconds = qBound(0.0, std::round(a.seconds * 100.0) / 100.0, 5.0);
            if (perLetter) {
                if (slot == QLatin1String("in") && seconds >= 0.05)
                    animation.insert(QStringLiteral("timing"),
                                     QJsonObject{{QStringLiteral("maxReadMs"), std::round(seconds * 1000.0)}});
            } else {
                if (seconds >= 0.05)
                    params.insert(QStringLiteral("duration"), seconds);
                if (slot == QLatin1String("out"))
                    params.insert(QStringLiteral("ease"), QStringLiteral("easeIn"));
            }
        }
        animation.insert(slot, QJsonObject{{QStringLiteral("preset"), preset}, {QStringLiteral("params"), params}});
    }
    if (threeD && threeDAnimation != 0 && !animation.contains(QStringLiteral("in")))
        animation.insert(QStringLiteral("in"), QJsonObject{{QStringLiteral("preset"), QStringLiteral("pop")},
                                                           {QStringLiteral("params"), QJsonObject{}}});
    if (!animation.isEmpty())
        style.insert(QStringLiteral("animation"), animation);

    // Quick controls: the few things people change on a Filmora title.
    QJsonArray controls{
        QJsonObject{{QStringLiteral("id"), QStringLiteral("text")}, {QStringLiteral("label"), QStringLiteral("Text")},
                    {QStringLiteral("type"), QStringLiteral("text")}, {QStringLiteral("bind"), QStringLiteral("text")}},
        QJsonObject{{QStringLiteral("id"), QStringLiteral("font")}, {QStringLiteral("label"), QStringLiteral("Font")},
                    {QStringLiteral("type"), QStringLiteral("font")}, {QStringLiteral("bind"), QStringLiteral("font.family")}},
    };
    if (fillPaint.value(QStringLiteral("kind")).toString() == QLatin1String("solid"))
        controls.append(QJsonObject{{QStringLiteral("id"), QStringLiteral("color")},
                                    {QStringLiteral("label"), QStringLiteral("Text colour")},
                                    {QStringLiteral("type"), QStringLiteral("color")},
                                    {QStringLiteral("bind"), QStringLiteral("layer.fill.color")}});

    QJsonObject t{
        {QStringLiteral("format"), 1},
        {QStringLiteral("id"), templateId},
        {QStringLiteral("label"), label},
        {QStringLiteral("description"), QStringLiteral("From a linked Filmora folder.")},
        {QStringLiteral("category"), category.isEmpty() ? QStringLiteral("Filmora") : category},
        {QStringLiteral("tags"), QJsonArray{QStringLiteral("filmora")}},
        {QStringLiteral("sampleText"), sampleText},
        {QStringLiteral("style"), style},
        {QStringLiteral("controls"), controls},
    };
    if (!font.family.isEmpty())
        t.insert(QStringLiteral("requires"), QJsonObject{{QStringLiteral("fonts"), QJsonArray{font.family}}});
    if (!effects.isEmpty())
        t.insert(QStringLiteral("effects"), effects);
    // How long the title runs in Filmora, so a dropped title gets the same length.
    const double end = num(root.value(QStringLiteral("tlEnd")), num(clip.value(QStringLiteral("tlEnd"))));
    if (end > 0)
        t.insert(QStringLiteral("sourceDurationMs"), std::round(end / kFilmoraTicksPerSecond * 1000.0));
    out.templateJson = t;
    out.ok = true;
    if (texts.size() > 1)
        out.sceneJson = sceneForTexts(texts, root, templateId, label, category, context, &out.sceneLeftOut);
    return out;
}

// ── Older titles: Data/data.xml ──────────────────────────────────────────────────────────────

QString decodeFilmoraHexText(const QString &value)
{
    static const QRegularExpression kHex(QStringLiteral("^[0-9A-Fa-f]+$"));
    const QString s = value.trimmed();
    if (s.size() < 4 || s.size() % 4 != 0 || !kHex.match(s).hasMatch())
        return value;
    QString out;
    for (int i = 0; i < s.size(); i += 4) {
        const ushort code = ushort(s.mid(i, 4).toUInt(nullptr, 16));
        if (code == 0)
            break; // a terminating NUL
        if (code < 0x20 && code != '\n' && code != '\r' && code != '\t')
            return value; // not text: a plain value that happens to look like hex
        out.append(QChar(code));
    }
    return out.isEmpty() ? value : out;
}

namespace {

// Attributes of one element, looked up without regard to case.
struct XmlAttrs
{
    QHash<QString, QString> values; // lower-case name -> value

    bool has(const QString &name) const { return values.contains(name.toLower()); }
    QString str(const QString &name, const QString &fallback = QString()) const
    {
        return values.value(name.toLower(), fallback);
    }
    double number(std::initializer_list<const char *> names, double fallback = 0.0) const
    {
        for (const char *n : names) {
            bool ok = false;
            const double d = values.value(QString::fromLatin1(n).toLower()).toDouble(&ok);
            if (ok)
                return d;
            const QString v = values.value(QString::fromLatin1(n).toLower()).trimmed().toLower();
            if (v == QLatin1String("true"))
                return 1.0;
            if (v == QLatin1String("false"))
                return 0.0;
        }
        return fallback;
    }
};

XmlAttrs attrsOf(const QXmlStreamAttributes &attributes)
{
    XmlAttrs a;
    for (const QXmlStreamAttribute &attr : attributes)
        a.values.insert(attr.name().toString().toLower(), attr.value().toString());
    return a;
}

// A COLORREF (0x00BBGGRR) as Filmora's JSON colour order (0xRRGGBB, what filmoraColor reads).
qint64 rgbFromColorref(double value)
{
    const quint32 v = quint32(qint64(value)) & 0xffffffu;
    return qint64(((v & 0xffu) << 16) | (v & 0xff00u) | ((v >> 16) & 0xffu));
}

// An alpha that may be written as 0–1 or 0–255.
double alpha255(double a)
{
    return a > 0.0 && a <= 1.0 ? a * 255.0 : a;
}

QList<double> numbersIn(const QString &s)
{
    static const QRegularExpression kNum(QStringLiteral("-?\\d+(?:\\.\\d+)?"));
    QList<double> out;
    for (auto it = kNum.globalMatch(s); it.hasNext();)
        out.append(it.next().captured(0).toDouble());
    return out;
}

QString unescapeXmlText(QString s)
{
    for (int pass = 0; pass < 2 && s.contains(QLatin1String("&lt;")); ++pass) {
        s.replace(QLatin1String("&lt;"), QLatin1String("<"))
            .replace(QLatin1String("&gt;"), QLatin1String(">"))
            .replace(QLatin1String("&quot;"), QLatin1String("\""))
            .replace(QLatin1String("&apos;"), QLatin1String("'"))
            .replace(QLatin1String("&amp;"), QLatin1String("&"));
    }
    return s;
}

// The outer <TimeLine> as flat "key -> value" pairs, whichever way Filmora wrote a property:
// <Property Name="Render.Framerate" Value="25/1"/>, <Render Framerate="25/1"/> or
// <Render><Framerate>25/1</Framerate></Render>.
QHash<QString, QString> flattenTimeline(const QByteArray &xml, QString *error)
{
    QHash<QString, QString> flat;
    QXmlStreamReader r(xml);
    QStringList path;
    while (!r.atEnd()) {
        r.readNext();
        if (r.isStartElement()) {
            path.append(r.name().toString());
            const XmlAttrs a = attrsOf(r.attributes());
            const QString key = a.str(QStringLiteral("name"), a.str(QStringLiteral("key")));
            const bool hasValue = a.has(QStringLiteral("value"));
            if (!key.isEmpty() && hasValue)
                flat.insert(key.toLower(), a.str(QStringLiteral("value")));
            for (auto it = a.values.cbegin(); it != a.values.cend(); ++it)
                flat.insert((path.last() + QLatin1Char('.') + it.key()).toLower(), it.value());
            static const QSet<QString> kPropertyElements{QStringLiteral("property"), QStringLiteral("prop"),
                                                         QStringLiteral("param"), QStringLiteral("parameter"),
                                                         QStringLiteral("item"), QStringLiteral("attribute")};
            if (!key.isEmpty() && !hasValue && kPropertyElements.contains(path.last().toLower())) {
                const QString text = r.readElementText(QXmlStreamReader::IncludeChildElements);
                flat.insert(key.toLower(), text);
                path.removeLast();
            }
        } else if (r.isCharacters() && !r.isWhitespace() && !path.isEmpty()) {
            flat[path.mid(std::max(0, int(path.size()) - 2)).join(QLatin1Char('.')).toLower()] += r.text().toString();
        } else if (r.isEndElement() && !path.isEmpty()) {
            path.removeLast();
        }
    }
    if (r.hasError() && flat.isEmpty() && error)
        *error = QStringLiteral("Its title file is damaged or could not be read");
    return flat;
}

// The value whose key is `name` or ends in ".name".
QString lookup(const QHash<QString, QString> &flat, const QString &name)
{
    const QString n = name.toLower();
    if (flat.contains(n))
        return flat.value(n);
    for (auto it = flat.cbegin(); it != flat.cend(); ++it) {
        if (it.key().endsWith(QLatin1Char('.') + n))
            return it.value();
    }
    return {};
}

} // namespace

QByteArray filmoraXmlTitleAsJson(const QByteArray &dataXml, QString *error)
{
    auto fail = [&](const QString &why) {
        if (error)
            *error = why;
        return QByteArray();
    };
    QString parseError;
    const QHash<QString, QString> flat = flattenTimeline(dataXml, &parseError);
    if (!parseError.isEmpty())
        return fail(parseError);
    const QString caption = unescapeXmlText(lookup(flat, QStringLiteral("CaptionData")));
    if (caption.trimmed().isEmpty())
        return fail(QStringLiteral("No text in this title (its data.xml has no caption)"));

    // The caption: one group of elements per title in the array (<NLETitleArray><NLETitle>…),
    // or one group for a caption that is a single title.
    QList<QHash<QString, XmlAttrs>> groups;
    {
        QXmlStreamReader r(caption);
        int depth = 0;
        bool isArray = false;
        while (!r.atEnd()) {
            r.readNext();
            if (r.isStartElement()) {
                ++depth;
                const QString name = r.name().toString().toLower();
                if (depth == 1) {
                    isArray = name.endsWith(QLatin1String("array"));
                    groups.append(QHash<QString, XmlAttrs>());
                    continue;
                }
                if (isArray && depth == 2 && !groups.last().isEmpty())
                    groups.append(QHash<QString, XmlAttrs>());
                if (!groups.last().contains(name))
                    groups.last().insert(name, attrsOf(r.attributes()));
                if (name == QLatin1String("chardata") && !groups.last().value(name).has(QStringLiteral("title"))) {
                    // Text written as the element's content instead of a Title attribute.
                    const QString text = r.readElementText(QXmlStreamReader::IncludeChildElements);
                    groups.last()[name].values.insert(QStringLiteral("title"), text);
                    --depth;
                }
            } else if (r.isEndElement()) {
                --depth;
            }
        }
        if (r.hasError() && groups.isEmpty())
            return fail(QStringLiteral("Its title text could not be read"));
    }

    // Frame size, length, placement and scale of the whole title, from the timeline.
    double viewW = 1920.0;
    double viewH = 1080.0;
    for (const QString &k : {QStringLiteral("Render.Width"), QStringLiteral("Width")}) {
        if (const double w = lookup(flat, k).toDouble(); w > 0) {
            viewW = w;
            break;
        }
    }
    for (const QString &k : {QStringLiteral("Render.Height"), QStringLiteral("Height")}) {
        if (const double h = lookup(flat, k).toDouble(); h > 0) {
            viewH = h;
            break;
        }
    }
    double fps = 25.0;
    if (const QList<double> f = numbersIn(lookup(flat, QStringLiteral("Framerate"))); !f.isEmpty() && f.at(0) > 0)
        fps = f.size() >= 2 && f.at(1) > 0 ? f.at(0) / f.at(1) : f.at(0);
    const double frames = lookup(flat, QStringLiteral("TotalFrameCount")).toDouble();
    const double seconds = frames > 0 ? frames / fps : 0.0;

    double posX = 0.5;
    double posY = 0.5;
    if (const QList<double> c = numbersIn(lookup(flat, QStringLiteral("CenterPoint"))); c.size() >= 2) {
        double x = c.at(0);
        double y = c.at(1);
        if (std::abs(x) > 1.5 || std::abs(y) > 1.5) {
            x /= viewW; // pixels
            y /= viewH;
        } else if (x < 0.0 || y < 0.0) {
            x += 0.5; // centred on 0
            y += 0.5;
        }
        posX = qBound(0.0, x, 1.0);
        posY = qBound(0.0, y, 1.0);
    }
    double scale = 1.0;
    if (const QList<double> s = numbersIn(lookup(flat, QStringLiteral("ResizeScale"))); !s.isEmpty() && s.at(0) > 0.05 && s.at(0) < 20.0)
        scale = s.at(0);

    auto param = [](const QString &name, double v) {
        return QJsonObject{{QStringLiteral("name"), name}, {QStringLiteral("fxParam"), QJsonObject{{QStringLiteral("unValue"), v}}}};
    };
    const QJsonArray chain{QJsonObject{
        {QStringLiteral("effectList"),
         QJsonArray{QJsonObject{{QStringLiteral("id"), QStringLiteral("video/effect/transform")},
                                {QStringLiteral("paramList"),
                                 QJsonArray{param(QStringLiteral("Position_x"), posX),
                                            param(QStringLiteral("Position_y"), posY)}}}}}}};
    const double end = seconds * kFilmoraTicksPerSecond;

    QJsonArray clipList;
    for (const auto &g : std::as_const(groups)) {
        const QString text = decodeFilmoraHexText(g.value(QStringLiteral("chardata")).str(QStringLiteral("title")))
                                 .remove(QLatin1Char('\r'));
        if (text.trimmed().isEmpty())
            continue;
        const XmlAttrs font = g.value(QStringLiteral("font"));
        const XmlAttrs face = g.value(QStringLiteral("face"));
        const XmlAttrs border = g.value(QStringLiteral("border"));
        const XmlAttrs shadow = g.value(QStringLiteral("shadow"));
        const XmlAttrs textData = g.value(QStringLiteral("textdata"));

        QJsonObject basic{
            {QStringLiteral("FontName"), decodeFilmoraHexText(font.str(QStringLiteral("FontName")))},
            {QStringLiteral("FontSize"), font.number({"Size", "FontSize"}, 64.0) * scale},
            {QStringLiteral("FontBold"), font.number({"Bold"})},
            {QStringLiteral("FontItalic"), font.number({"Italic"})},
            {QStringLiteral("TextColor"),
             QJsonArray{QJsonObject{{QStringLiteral("Color"), rgbFromColorref(face.number({"Color1", "Color"}, 0xffffff))}}}},
        };
        if (face.has(QStringLiteral("Alpha")))
            basic.insert(QStringLiteral("TextAlpha"), alpha255(face.number({"Alpha"}, 255)));

        QJsonObject td{{QStringLiteral("CharData"), text}, {QStringLiteral("Basic"), basic}};
        if (!border.values.isEmpty()) {
            QJsonObject b{{QStringLiteral("Enable"), border.number({"Enable"})},
                          {QStringLiteral("Size"), border.number({"Size", "Width"}) * scale},
                          {QStringLiteral("Color"), rgbFromColorref(border.number({"Color1", "Color"}))}};
            if (border.has(QStringLiteral("Alpha")))
                b.insert(QStringLiteral("Alpha"), alpha255(border.number({"Alpha"}, 255)));
            if (border.has(QStringLiteral("BlurRadius")))
                b.insert(QStringLiteral("BlurRadius"), border.number({"BlurRadius"}));
            td.insert(QStringLiteral("Border"), b);
        }
        if (!shadow.values.isEmpty()) {
            double direction = shadow.number({"Direction"}, 3.0);
            if (direction > 7.0)
                direction = (direction + 90.0) / 45.0; // degrees, not one of eight steps
            td.insert(QStringLiteral("Shadow"),
                      QJsonObject{{QStringLiteral("Enable"), shadow.number({"Enable"})},
                                  {QStringLiteral("Color"), rgbFromColorref(shadow.number({"Color", "Color1"}))},
                                  {QStringLiteral("Alpha"), alpha255(shadow.number({"Alpha"}, 255))},
                                  {QStringLiteral("BlurRadius"), shadow.number({"BlurRadius"})},
                                  {QStringLiteral("Distance"), shadow.number({"Distance"}, 1.0)},
                                  {QStringLiteral("Direction"), direction}});
        }
        if (textData.has(QStringLiteral("CharSpace")))
            td.insert(QStringLiteral("CharSpace"), textData.number({"CharSpace"}));
        if (textData.has(QStringLiteral("LineSpace")))
            td.insert(QStringLiteral("LineSpace"), textData.number({"LineSpace"}));

        QJsonObject script{{QStringLiteral("Text"), text},
                           {QStringLiteral("ViewSize"), QStringLiteral("%1:%2").arg(viewW).arg(viewH)},
                           {QStringLiteral("TextData"), QJsonArray{td}}};

        // Fill gradient: Color1, Color2 (… Color4) spread evenly, at Angle.
        const XmlAttrs gradient = g.value(QStringLiteral("gradient"));
        if (gradient.number({"Enable"}) > 0.5) {
            QJsonArray stops;
            QList<qint64> colors;
            for (const char *k : {"Color1", "Color2", "Color3", "Color4"}) {
                if (gradient.has(QString::fromLatin1(k)))
                    colors.append(rgbFromColorref(gradient.number({k})));
            }
            for (int i = 0; i < colors.size(); ++i)
                stops.append(QJsonObject{{QStringLiteral("Color"), colors.at(i)},
                                         {QStringLiteral("Pos"), colors.size() > 1 ? 100.0 * i / (colors.size() - 1) : 0.0}});
            script.insert(QStringLiteral("Gradient"),
                          QJsonObject{{QStringLiteral("Enable"), 1},
                                      {QStringLiteral("Angle"), gradient.number({"Angle"}, 90.0)},
                                      {QStringLiteral("GradType"), gradient.number({"GradType", "Type"})},
                                      {QStringLiteral("ColorPos"), stops}});
        }
        if (g.value(QStringLiteral("texture")).number({"Enable"}) > 0.5)
            script.insert(QStringLiteral("Texture"), QJsonObject{{QStringLiteral("Enable"), 1}});
        XmlAttrs box = g.value(QStringLiteral("background_01"));
        if (box.values.isEmpty())
            box = g.value(QStringLiteral("background"));
        if (box.number({"Enable"}) > 0.5) {
            QJsonObject b{{QStringLiteral("Enable"), 1},
                          {QStringLiteral("Color"), rgbFromColorref(box.number({"Color1", "Color"}))},
                          {QStringLiteral("Radian"), box.number({"Radian", "Radius"})}};
            if (box.has(QStringLiteral("Alpha")))
                b.insert(QStringLiteral("Alpha"), alpha255(box.number({"Alpha"}, 255)));
            script.insert(QStringLiteral("Background"), b);
        }

        QJsonObject clip{{QStringLiteral("scriptBuf"), script}, {QStringLiteral("effectChainList"), chain}};
        if (end > 0) {
            clip.insert(QStringLiteral("tlBegin"), 0);
            clip.insert(QStringLiteral("tlEnd"), end);
        }
        clipList.append(clip);
    }
    if (clipList.isEmpty())
        return fail(QStringLiteral("No text in this title"));

    QJsonObject root{{QStringLiteral("subTimeline"),
                      QJsonObject{{QStringLiteral("timelineInfos"),
                                   QJsonArray{QJsonObject{{QStringLiteral("trackInfos"),
                                                           QJsonArray{QJsonObject{{QStringLiteral("clipList"), clipList}}}}}}}}}};
    if (end > 0)
        root.insert(QStringLiteral("tlEnd"), end);
    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

} // namespace dafin::linked
