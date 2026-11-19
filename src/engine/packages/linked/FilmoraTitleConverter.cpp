#include "engine/packages/linked/FilmoraTitleConverter.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QHash>
#include <QSet>
#include <QtMath>

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
    else if (nameHas(n, {"slide", "fly", "move", "push", "shift"})) {
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
    else if (nameHas(n, {"rain"}))
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

    // Animation: Filmora's named in / out / loop animations, matched to Dafin's presets.
    QJsonObject animation;
    for (const QString &slot : {QStringLiteral("in"), QStringLiteral("out"), QStringLiteral("loop")}) {
        const QJsonObject a = clip.value(slot + QStringLiteral("Animation")).toObject();
        const QString name = a.value(QStringLiteral("name")).toString();
        if (name.isEmpty())
            continue;
        QJsonObject params;
        const QString preset = dafinAnimationFor(name, slot, &params);
        if (preset.isEmpty()) {
            out.leftOut.append(QStringLiteral("its \"%1\" animation").arg(name));
            continue;
        }
        if (slot != QLatin1String("loop") && preset != QLatin1String("typewriter")) {
            const double seconds = num(a.value(QStringLiteral("duration"))) / kFilmoraTicksPerSecond;
            if (seconds > 0.0)
                params.insert(QStringLiteral("duration"), qBound(0.05, std::round(seconds * 100.0) / 100.0, 5.0));
            if (slot == QLatin1String("out"))
                params.insert(QStringLiteral("ease"), QStringLiteral("easeIn"));
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
    return out;
}

} // namespace dafin::linked
