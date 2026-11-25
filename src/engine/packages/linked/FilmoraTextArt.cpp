#include "engine/packages/linked/FilmoraTextArt.h"

#include <QJsonArray>
#include <QRegularExpression>
#include <QSet>
#include <QStringConverter>
#include <QStringDecoder>
#include <QXmlStreamReader>
#include <QtMath>

#include <algorithm>
#include <cmath>

namespace dafin::linked {

namespace {

constexpr qint64 kMaxTextArtBytes = 4 * 1024 * 1024;
constexpr int kMaxDepth = 64;
constexpr int kMaxElements = 20000;

// TextArt colours are Windows COLORREFs (0x00BBGGRR), like the older data.xml titles that share
// this element set (Face / Border / Shadow / Background_01). Not yet checked against Filmora-rendered
// frames (work package D-021); kept in one place so a reference can change it.
qint64 rgbFromColorref(qint64 v)
{
    const quint32 c = quint32(v) & 0xffffffu;
    return qint64(((c & 0xffu) << 16) | (c & 0xff00u) | ((c >> 16) & 0xffu));
}

int alphaOf(const TextArtAttrs &a)
{
    double v = a.number({"Alpha"}, 255.0);
    if (v > 0.0 && v <= 1.0 && a.str(QStringLiteral("Alpha")).contains(QLatin1Char('.')))
        v *= 255.0; // written as 0–1
    return qBound(0, int(std::lround(v)), 255);
}

// "#aarrggbb" from a raw COLORREF; nothing for -1 ("not set") or a missing value.
std::optional<QString> colourOf(const TextArtAttrs &a, std::initializer_list<const char *> names, int alpha)
{
    const std::optional<double> v = a.number(names);
    if (!v || *v < 0.0)
        return std::nullopt;
    return filmoraColor(rgbFromColorref(qint64(*v)), alpha);
}

QJsonObject solid(const QString &colour)
{
    return QJsonObject{{QStringLiteral("kind"), QStringLiteral("solid")}, {QStringLiteral("color"), colour}};
}

QJsonObject shadingLayer(const QString &id, const QString &kind, const QJsonObject &paint)
{
    return QJsonObject{{QStringLiteral("id"), id}, {QStringLiteral("kind"), kind}, {QStringLiteral("paint"), paint}};
}

QString lower(const QStringView &s)
{
    return s.toString().toLower();
}

TextArtAttrs attrsOf(const QXmlStreamAttributes &attributes)
{
    TextArtAttrs a;
    a.present = true;
    for (const QXmlStreamAttribute &attr : attributes)
        a.values.insert(attr.name().toString().toLower(), attr.value().toString());
    return a;
}

// Decodes the file: a BOM decides; else strict UTF-8; else GB18030 (Filmora's Chinese presets).
QString decodeTextArt(const QByteArray &bytes, QString *encoding, bool *unreadable)
{
    *unreadable = false;
    if (const std::optional<QStringConverter::Encoding> bom = QStringConverter::encodingForData(bytes);
        bom && *bom != QStringConverter::Utf8 && *bom != QStringConverter::System) {
        QStringDecoder d(*bom);
        *encoding = QStringLiteral("utf-16");
        return d.decode(bytes);
    }
    {
        QStringDecoder utf8(QStringConverter::Utf8, QStringConverter::Flag::Stateless);
        QString s = utf8.decode(bytes);
        if (!utf8.hasError()) {
            *encoding = QStringLiteral("utf-8");
            if (s.startsWith(QChar(0xfeff)))
                s.remove(0, 1);
            return s;
        }
    }
    *encoding = QStringLiteral("gb18030");
    QStringDecoder gb("GB18030");
    if (gb.isValid()) {
        const QString s = gb.decode(bytes);
        if (!gb.hasError())
            return s;
    }
    // This Qt has no GB18030 decoder: the element and attribute names are ASCII, so the structure
    // still reads; the non-ASCII text (sample text, a Chinese font name) is dropped, not guessed.
    *unreadable = true;
    QString s;
    s.reserve(bytes.size());
    for (const char c : bytes) {
        if (uchar(c) < 0x80)
            s.append(QLatin1Char(c));
    }
    return s;
}

const QSet<QString> &knownElements()
{
    static const QSet<QString> k{
        QStringLiteral("textnode"),   QStringLiteral("textparam"), QStringLiteral("textdata"),
        QStringLiteral("chardata"),   QStringLiteral("font"),      QStringLiteral("face"),
        QStringLiteral("border"),     QStringLiteral("shadow"),    QStringLiteral("gradient"),
        QStringLiteral("texture"),    QStringLiteral("colorpos"),  QStringLiteral("colorlist"),
        QStringLiteral("color"),      QStringLiteral("layer"),     QStringLiteral("externattr"),
        QStringLiteral("effectparam"), QStringLiteral("background_01"), QStringLiteral("background"),
    };
    return k;
}

} // namespace

std::optional<double> TextArtAttrs::number(std::initializer_list<const char *> names) const
{
    for (const char *n : names) {
        const QString v = values.value(QString::fromLatin1(n).toLower()).trimmed();
        if (v.isEmpty())
            continue;
        bool ok = false;
        const double d = v.toDouble(&ok);
        if (ok)
            return d;
        if (v.compare(QLatin1String("true"), Qt::CaseInsensitive) == 0)
            return 1.0;
        if (v.compare(QLatin1String("false"), Qt::CaseInsensitive) == 0)
            return 0.0;
    }
    return std::nullopt;
}

double TextArtAttrs::number(std::initializer_list<const char *> names, double fallback) const
{
    return number(names).value_or(fallback);
}

bool TextArtAttrs::enabled() const
{
    return present && number({"Enable"}, 0.0) > 0.5;
}

bool looksLikeTextArtXml(const QByteArray &xml)
{
    const QByteArray head = xml.left(4096);
    return head.contains("<TextNode") && xml.contains("TextParam");
}

QString textArtTextureName(const QString &path)
{
    QString p = path.trimmed();
    p.replace(QLatin1Char('\\'), QLatin1Char('/'));
    // "%CaptionResourcePath%/texture-image-{…}.png": a folder Filmora fills in; only the name counts.
    static const QRegularExpression kVariable(QStringLiteral("^%[^%/]*%/*"));
    p.remove(kVariable);
    if (p.isEmpty() || p.contains(QChar(0)) || p.startsWith(QLatin1Char('/')) || p.contains(QLatin1Char(':')))
        return {};
    for (const QString &part : p.split(QLatin1Char('/'))) {
        if (part == QLatin1String(".."))
            return {};
    }
    return p.section(QLatin1Char('/'), -1);
}

std::optional<TextArtStyle> readTextArtXml(const QByteArray &xml, QString *error)
{
    auto fail = [&](const QString &why) -> std::optional<TextArtStyle> {
        if (error)
            *error = why;
        return std::nullopt;
    };
    if (xml.size() > kMaxTextArtBytes)
        return fail(QStringLiteral("its file is too large"));
    if (xml.contains("<!DOCTYPE") || xml.contains("<!ENTITY"))
        return fail(QStringLiteral("its file declares a DTD, which Dafin does not read"));

    TextArtStyle style;
    const QString text = decodeTextArt(xml, &style.encoding, &style.textUnreadable);

    QXmlStreamReader r(text);
    QStringList stack;      // lower-case element names
    int layerDepth = -1;    // depth of the open ExternAttr/Layer, -1 outside one
    TextArtLayer *layer = nullptr;
    TextArtPaintPart *part = nullptr; // the open Face / Border
    int partDepth = -1;
    bool gradientIsRoot = false;
    int elements = 0;
    bool sawParam = false;
    QSet<QString> unknown;

    while (!r.atEnd()) {
        r.readNext();
        if (r.isStartElement()) {
            if (++elements > kMaxElements || stack.size() >= kMaxDepth)
                return fail(QStringLiteral("its file is nested too deeply or too large"));
            const QString name = lower(r.name());
            const int depth = int(stack.size());
            stack.append(name);
            if (depth == 0) {
                if (name != QLatin1String("textnode"))
                    return fail(QStringLiteral("it is not a Filmora Art title (no TextNode)"));
                style.root = attrsOf(r.attributes());
                continue;
            }
            TextArtLayer &owner = layer ? *layer : style.base;
            if (name == QLatin1String("textparam") || name == QLatin1String("effectparam")) {
                sawParam = sawParam || name == QLatin1String("textparam");
            } else if (name == QLatin1String("layer") && stack.contains(QStringLiteral("externattr")) && !layer) {
                style.layers.append(TextArtLayer());
                layer = &style.layers.last();
                layer->attrs = attrsOf(r.attributes());
                layerDepth = depth;
            } else if (name == QLatin1String("face") || name == QLatin1String("border")) {
                part = name == QLatin1String("face") ? &owner.face : &owner.border;
                part->attrs = attrsOf(r.attributes());
                partDepth = depth;
            } else if (name == QLatin1String("shadow")) {
                owner.shadow = attrsOf(r.attributes());
            } else if (name == QLatin1String("gradient")) {
                if (part) {
                    part->gradient = attrsOf(r.attributes());
                    gradientIsRoot = false;
                } else if (!layer) {
                    style.gradient = attrsOf(r.attributes());
                    gradientIsRoot = true;
                } else if (!owner.face.gradient.present) {
                    owner.face.gradient = attrsOf(r.attributes()); // a layer's own gradient: its face
                    gradientIsRoot = false;
                }
            } else if (name == QLatin1String("colorpos")) {
                const TextArtAttrs a = attrsOf(r.attributes());
                const QPair<double, qint64> stop{a.number({"Pos", "Position"}, 0.0),
                                                 qint64(a.number({"Color", "Color1"}, -1.0))};
                if (gradientIsRoot && !part && !layer)
                    style.gradientStops.append(stop);
                else if (part)
                    part->stops.append(stop);
                else if (layer)
                    layer->face.stops.append(stop);
            } else if (name == QLatin1String("texture")) {
                if (part)
                    part->texture = attrsOf(r.attributes());
                else if (!layer)
                    style.texture = attrsOf(r.attributes());
                else if (!owner.face.texture.present)
                    owner.face.texture = attrsOf(r.attributes());
            } else if (name == QLatin1String("color") && depth >= 2 && stack.at(depth - 1) == QLatin1String("colorlist")) {
                owner.colorList.append(attrsOf(r.attributes()));
            } else if (!layer && (name == QLatin1String("background_01") || name == QLatin1String("background"))) {
                if (!style.background.present)
                    style.background = attrsOf(r.attributes());
            } else if (!layer && name == QLatin1String("font")) {
                style.font = attrsOf(r.attributes());
            } else if (!layer && name == QLatin1String("textdata")) {
                style.textData = attrsOf(r.attributes());
            } else if (!layer && name == QLatin1String("chardata")) {
                const TextArtAttrs a = attrsOf(r.attributes());
                QString t = a.str(QStringLiteral("Title"));
                if (t.isEmpty()) {
                    t = r.readElementText(QXmlStreamReader::IncludeChildElements);
                    stack.removeLast(); // readElementText consumed the end element
                }
                if (style.text.isEmpty())
                    style.text = decodeFilmoraHexText(t).remove(QLatin1Char('\r'));
            } else if (!knownElements().contains(name)) {
                const TextArtAttrs a = attrsOf(r.attributes());
                if (a.enabled())
                    unknown.insert(r.name().toString());
            }
        } else if (r.isEndElement()) {
            if (stack.isEmpty())
                break;
            const int depth = int(stack.size()) - 1;
            stack.removeLast();
            if (depth == partDepth) {
                part = nullptr;
                partDepth = -1;
            }
            if (depth == layerDepth) {
                layer = nullptr;
                layerDepth = -1;
                part = nullptr;
                partDepth = -1;
            }
        }
    }
    if (r.hasError())
        return fail(QStringLiteral("its file is damaged (%1)").arg(r.errorString()));
    if (!style.root.present || !sawParam)
        return fail(QStringLiteral("it is not a Filmora Art title (no TextParam)"));
    style.unknownEnabled = QStringList(unknown.cbegin(), unknown.cend());
    style.unknownEnabled.sort();
    return style;
}

namespace {

struct Builder
{
    const TextArtStyle &style;
    const std::function<bool(const QString &)> &textureExists;
    TextArtConversion &out;
    QString baseColour; // the text's own fill colour, "#aarrggbb"

    void note(const QString &s)
    {
        if (!out.notes.contains(s))
            out.notes.append(s);
    }

    // A gradient paint from ColorPos stops or from Color1..ColorN; empty when off or < 2 colours.
    QJsonObject gradient(const TextArtAttrs &g, const QList<QPair<double, qint64>> &rawStops, int alpha,
                         const QString &where)
    {
        if (!g.enabled())
            return {};
        QList<QPair<double, qint64>> stops;
        for (const auto &s : rawStops) {
            if (s.second >= 0)
                stops.append(s);
        }
        if (stops.isEmpty()) {
            QList<qint64> colours;
            for (int i = 1; i <= 8; ++i) {
                const QByteArray key = "Color" + QByteArray::number(i);
                const std::optional<double> c = g.number({key.constData()});
                if (c && *c >= 0)
                    colours.append(qint64(*c));
            }
            for (int i = 0; i < colours.size(); ++i)
                stops.append({colours.size() > 1 ? 100.0 * i / (colours.size() - 1) : 0.0, colours.at(i)});
        }
        if (stops.size() < 2)
            return {};
        // Pos is 0–100 in Filmora's JSON titles; a file written with 0–1 is read as such.
        double maxPos = 0.0;
        for (const auto &s : stops)
            maxPos = std::max(maxPos, s.first);
        const double div = maxPos <= 1.0 ? 1.0 : 100.0;
        std::stable_sort(stops.begin(), stops.end(), [](const auto &a, const auto &b) { return a.first < b.first; });
        QJsonArray js;
        for (const auto &s : stops)
            js.append(QJsonObject{{QStringLiteral("pos"), qBound(0.0, s.first / div, 1.0)},
                                  {QStringLiteral("color"), filmoraColor(rgbFromColorref(s.second), alpha)}});
        const int type = int(g.number({"GradType", "Type"}, 0.0));
        if (type != 0 && type != 1)
            note(QStringLiteral("%1 gradient of type %2 (drawn as a straight gradient)").arg(where).arg(type));
        return QJsonObject{
            {QStringLiteral("kind"), QStringLiteral("gradient")},
            {QStringLiteral("gradient"),
             QJsonObject{{QStringLiteral("kind"), type == 1 ? QStringLiteral("radial") : QStringLiteral("linear")},
                         {QStringLiteral("angle"), std::fmod(g.number({"Angle"}, 90.0), 360.0)},
                         {QStringLiteral("space"), QStringLiteral("line")},
                         {QStringLiteral("stops"), js}}}};
    }

    void texture(const TextArtAttrs &t, const QString &where)
    {
        if (!t.enabled())
            return; // a texture switched off is ignored, whatever path it names
        const QString raw = t.str(QStringLiteral("TextureImagePath")).isEmpty() ? t.str(QStringLiteral("Path"))
                                                                               : t.str(QStringLiteral("TextureImagePath"));
        const QString name = textArtTextureName(raw);
        if (name.isEmpty()) {
            note(QStringLiteral("%1 texture (its file is not named safely; drawn without it)").arg(where));
            out.missingTextures.append(raw.isEmpty() ? QStringLiteral("(no file)") : QStringLiteral("(unsafe path)"));
            return;
        }
        out.missingTextures.append(name);
        if (textureExists && textureExists(name))
            note(QStringLiteral("%1 texture %2 (Dafin titles cannot use pictures yet; drawn without it)").arg(where, name));
        else
            note(QStringLiteral("%1 texture %2 is not in your folder (drawn without it)").arg(where, name));
    }

    // A Face or Border's paint: its gradient, else its colour, else `fallback`.
    QJsonObject paintOf(const TextArtPaintPart &p, const QString &fallback, const QString &where, bool *usedFallback)
    {
        const int alpha = alphaOf(p.attrs);
        texture(p.texture, where);
        QJsonObject g = gradient(p.gradient, p.stops, alpha, where);
        if (!g.isEmpty())
            return g;
        if (const std::optional<QString> c = colourOf(p.attrs, {"Color1", "Color"}, alpha))
            return solid(*c);
        if (usedFallback)
            *usedFallback = true;
        // The fallback keeps its rgb; the part's own alpha applies.
        return solid(QLatin1Char('#') + QStringLiteral("%1").arg(alpha, 2, 16, QLatin1Char('0')) + fallback.mid(3));
    }

    // One part (the text itself, or an extra layer) as shading layers, back to front:
    // shadow, outline, fill.
    void append(const TextArtLayer &l, const QString &prefix, double dx, double dy, const QString &where,
                bool isBase, QJsonArray *layers)
    {
        if (l.shadow.enabled()) {
            double direction = l.shadow.number({"Direction"}, 3.0);
            if (direction > 7.0)
                direction = (direction + 90.0) / 45.0; // degrees, not one of eight steps
            const double angle = qDegreesToRadians(45.0 * direction - 90.0);
            const double distance = std::max(0.0, l.shadow.number({"Distance"}, 1.0)) * 4.0;
            const QString colour = colourOf(l.shadow, {"Color", "Color1"}, 255).value_or(QStringLiteral("#ff000000"));
            QJsonObject s = shadingLayer(prefix + QStringLiteral("shadow"), QStringLiteral("shadow"), solid(colour));
            s.insert(QStringLiteral("opacity"), alphaOf(l.shadow) / 255.0);
            s.insert(QStringLiteral("offsetX"), dx + std::round(std::cos(angle) * distance * 10.0) / 10.0);
            s.insert(QStringLiteral("offsetY"), dy + std::round(std::sin(angle) * distance * 10.0) / 10.0);
            s.insert(QStringLiteral("blur"), qBound(0.0, l.shadow.number({"BlurRadius"}, 0.0), 100.0));
            layers->append(s);
        }
        const double borderSize = l.border.attrs.number({"Size", "Width"}, 0.0);
        if (l.border.attrs.enabled() && borderSize > 0.0) {
            QJsonObject s = shadingLayer(prefix + QStringLiteral("outline"), QStringLiteral("stroke"),
                                         paintOf(l.border, baseColour, where + QStringLiteral(" outline"), nullptr));
            s.insert(QStringLiteral("width"), qBound(0.5, borderSize, 200.0));
            s.insert(QStringLiteral("strokeAlign"), QStringLiteral("outside"));
            if (const double blur = l.border.attrs.number({"BlurRadius"}, 0.0); blur > 0.0)
                s.insert(QStringLiteral("blur"), qBound(0.0, blur, 100.0));
            if (!qFuzzyIsNull(dx) || !qFuzzyIsNull(dy)) {
                s.insert(QStringLiteral("offsetX"), dx);
                s.insert(QStringLiteral("offsetY"), dy);
            }
            layers->append(s);
        } else if (l.border.attrs.enabled()) {
            texture(l.border.texture, where + QStringLiteral(" outline"));
        }
        if (l.face.attrs.enabled()) {
            QJsonObject paint;
            bool fallback = false;
            if (isBase) {
                // A Gradient outside Face / Border is the text's own fill (as in older titles).
                texture(style.texture, QStringLiteral("its"));
                paint = gradient(style.gradient, style.gradientStops, alphaOf(l.face.attrs), QStringLiteral("its"));
                if (paint.isEmpty())
                    paint = paintOf(l.face, baseColour, QStringLiteral("its"), &fallback);
                else
                    texture(l.face.texture, QStringLiteral("its"));
            } else {
                // A layer's face colour: its own, else (not established) its colour list, else the text's.
                if (!colourOf(l.face.attrs, {"Color1", "Color"}, 255) && !l.face.gradient.enabled()) {
                    for (const TextArtAttrs &c : l.colorList) {
                        if (const std::optional<QString> v = colourOf(c, {"Color", "Value", "Color1", "RGB"}, alphaOf(l.face.attrs))) {
                            texture(l.face.texture, where);
                            paint = solid(*v);
                            note(QStringLiteral("%1 colour taken from its colour list (not confirmed)").arg(where));
                            break;
                        }
                    }
                }
                if (paint.isEmpty())
                    paint = paintOf(l.face, baseColour, where, &fallback);
                if (fallback)
                    note(QStringLiteral("%1 has no colour of its own (drawn in the text's colour)").arg(where));
            }
            QJsonObject f = shadingLayer(prefix + QStringLiteral("fill"), QStringLiteral("fill"), paint);
            if (const double blur = l.face.attrs.number({"BlurRadius"}, 0.0); blur > 0.0)
                f.insert(QStringLiteral("blur"), qBound(0.0, blur, 100.0));
            if (!qFuzzyIsNull(dx) || !qFuzzyIsNull(dy)) {
                f.insert(QStringLiteral("offsetX"), dx);
                f.insert(QStringLiteral("offsetY"), dy);
            }
            layers->append(f);
        } else {
            texture(l.face.texture, where);
        }
    }
};

} // namespace

TextArtConversion convertTextArt(const TextArtStyle &style, const QString &templateId, const QString &label,
                                 const QString &category, const TitleContext &context,
                                 const std::function<bool(const QString &)> &textureExists)
{
    TextArtConversion out;
    Builder b{style, textureExists, out, {}};

    if (style.textUnreadable)
        b.note(QStringLiteral("its text in GB18030, which this Dafin build cannot read (its name is shown instead)"));

    // The text's own colour: Face Color1; -1 means "not set", then the TextNode's Color.
    if (const std::optional<QString> c = colourOf(style.base.face.attrs, {"Color1", "Color"}, 255)) {
        b.baseColour = *c;
    } else if (const std::optional<QString> root = colourOf(style.root, {"Color"}, 255)) {
        b.baseColour = *root;
        if (style.base.face.attrs.has(QStringLiteral("Color1")))
            b.note(QStringLiteral("its text colour is written as -1, \"not set\" (drawn in its main colour)"));
    } else {
        b.baseColour = QStringLiteral("#ffffffff");
        b.note(QStringLiteral("its text colour is not set (drawn white)"));
    }

    QJsonObject styleJson;
    const double pixelSize = qBound(4.0, style.font.number({"Size", "FontSize"}, 64.0), 1000.0);
    styleJson.insert(QStringLiteral("pixelSize"), int(std::lround(pixelSize)));
    const QString fontName = decodeFilmoraHexText(style.font.str(QStringLiteral("FontName"))).trimmed();
    TitleFont font = splitFontName(fontName);
    if (context.font) {
        if (const std::optional<TitleFont> known = context.font(fontName))
            font = *known;
    }
    if (style.font.number({"Bold"}, 0.0) > 0.5)
        font.weight = std::max(font.weight, 700);
    if (style.font.number({"Italic"}, 0.0) > 0.5)
        font.italic = true;
    if (!font.family.isEmpty())
        styleJson.insert(QStringLiteral("fontFamily"), font.family);
    styleJson.insert(QStringLiteral("fontWeight"), qBound(100, font.weight, 900));
    styleJson.insert(QStringLiteral("italic"), font.italic);
    styleJson.insert(QStringLiteral("align"), QStringLiteral("center"));
    styleJson.insert(QStringLiteral("valign"), QStringLiteral("center"));
    if (const std::optional<double> cs = style.textData.number({"CharSpace"}); cs && !qFuzzyIsNull(*cs))
        styleJson.insert(QStringLiteral("letterSpacing"), qBound(-50.0, *cs, 200.0));
    if (const std::optional<double> ls = style.textData.number({"LineSpace"}); ls && !qFuzzyIsNull(*ls))
        styleJson.insert(QStringLiteral("lineHeight"), qBound(0.6, 1.2 + *ls / 100.0, 3.0));

    // Back to front: the extra layers in the order the file lists them, then the text itself.
    // Filmora's real order is not established (D-021), so a style with extra layers says so.
    QJsonArray layers;
    for (int i = 0; i < style.layers.size(); ++i) {
        const TextArtLayer &l = style.layers.at(i);
        b.append(l, QStringLiteral("x%1-").arg(i + 1), l.attrs.number({"OffsetX"}, 0.0), l.attrs.number({"OffsetY"}, 0.0),
                 QStringLiteral("layer %1").arg(i + 1), false, &layers);
    }
    b.append(style.base, QString(), 0.0, 0.0, QStringLiteral("its"), true, &layers);
    if (!style.layers.isEmpty())
        b.note(style.layers.size() == 1
                   ? QStringLiteral("the order of its extra layer is not confirmed against Filmora (drawn behind the text)")
                   : QStringLiteral("the order of its %1 extra layers is not confirmed against Filmora (drawn behind the text, "
                                    "the first at the back)")
                         .arg(style.layers.size()));
    if (layers.isEmpty()) {
        layers.append(shadingLayer(QStringLiteral("fill"), QStringLiteral("fill"), solid(b.baseColour)));
        b.note(QStringLiteral("no part of it is switched on (drawn as plain text in its main colour)"));
    }
    styleJson.insert(QStringLiteral("layers"), layers);

    if (style.background.enabled()) {
        styleJson.insert(QStringLiteral("boxEnabled"), true);
        styleJson.insert(QStringLiteral("boxColor"),
                         colourOf(style.background, {"Color1", "Color"}, alphaOf(style.background)).value_or(QStringLiteral("#ff000000")));
        styleJson.insert(QStringLiteral("boxPadding"), qBound(0.0, pixelSize * 0.2, 200.0));
        styleJson.insert(QStringLiteral("boxRadius"), qBound(0.0, style.background.number({"Radian", "Radius"}, 0.0), 200.0));
    }
    for (const QString &u : style.unknownEnabled)
        b.note(QStringLiteral("its %1 setting (not understood)").arg(u));

    QJsonArray controls{
        QJsonObject{{QStringLiteral("id"), QStringLiteral("text")}, {QStringLiteral("label"), QStringLiteral("Text")},
                    {QStringLiteral("type"), QStringLiteral("text")}, {QStringLiteral("bind"), QStringLiteral("text")}},
        QJsonObject{{QStringLiteral("id"), QStringLiteral("font")}, {QStringLiteral("label"), QStringLiteral("Font")},
                    {QStringLiteral("type"), QStringLiteral("font")}, {QStringLiteral("bind"), QStringLiteral("font.family")}},
    };
    for (const QJsonValue &v : std::as_const(layers)) {
        const QJsonObject l = v.toObject();
        if (l.value(QStringLiteral("id")).toString() == QLatin1String("fill")
            && l.value(QStringLiteral("paint")).toObject().value(QStringLiteral("kind")).toString() == QLatin1String("solid"))
            controls.append(QJsonObject{{QStringLiteral("id"), QStringLiteral("color")},
                                        {QStringLiteral("label"), QStringLiteral("Text colour")},
                                        {QStringLiteral("type"), QStringLiteral("color")},
                                        {QStringLiteral("bind"), QStringLiteral("layer.fill.color")}});
    }

    const QString sample = style.text.trimmed().isEmpty() ? label : style.text.trimmed();
    out.fidelity = out.notes.isEmpty() ? QStringLiteral("faithful") : QStringLiteral("approximated");
    QJsonObject t{
        {QStringLiteral("format"), 1},
        {QStringLiteral("id"), templateId},
        {QStringLiteral("label"), label},
        {QStringLiteral("description"),
         out.notes.isEmpty() ? QStringLiteral("Filmora Art title from a linked folder.")
                             : QStringLiteral("Filmora Art title from a linked folder, approximated: ") + out.notes.join(QStringLiteral("; "))
                                   + QLatin1Char('.')},
        {QStringLiteral("category"), category.isEmpty() ? QStringLiteral("Filmora") : category},
        {QStringLiteral("tags"), QJsonArray{QStringLiteral("filmora"), QStringLiteral("art-title"), out.fidelity}},
        {QStringLiteral("sampleText"), sample},
        {QStringLiteral("style"), styleJson},
        {QStringLiteral("controls"), controls},
    };
    if (!font.family.isEmpty())
        t.insert(QStringLiteral("requires"), QJsonObject{{QStringLiteral("fonts"), QJsonArray{font.family}}});
    out.templateJson = t;
    out.ok = true;
    return out;
}

} // namespace dafin::linked
