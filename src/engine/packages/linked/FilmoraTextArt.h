#pragma once

#include <QByteArray>
#include <QHash>
#include <QJsonObject>
#include <QList>
#include <QPair>
#include <QString>
#include <QStringList>

#include <functional>
#include <optional>

#include "engine/packages/linked/FilmoraTitleConverter.h"

// Dafin: Filmora "Art titles" (TextArt): one XML file (<TextNode version="2.0">, a TextParam with
// the text's own Face / Border / Shadow / Gradient / Texture / Background_01, and an EffectParam
// whose ExternAttr holds extra Layer elements, each its own Face / Border / Shadow drawn at an
// offset) turned into an ordered stack of Dafin shading layers (TextShadingLayer, through a title
// template). Read only from the user's own linked folder; nothing from Filmora is shipped.
//
// Two steps, so the source stays auditable: readTextArtXml() keeps the file's values as written
// (signed -1 "not set" colours, alpha, offsets, stops, texture paths), convertTextArt() maps what
// Dafin can draw and names everything else. What is not established (the draw order of the extra
// layers, the colour channel order, blur and offset scale) is reported, never presented as a
// faithful copy. Textures are never invented: a missing one, or one Dafin titles cannot use yet,
// is named and left out.
namespace dafin::linked {

// One element's attributes as written, looked up without regard to case.
struct TextArtAttrs
{
    QHash<QString, QString> values; // lower-case name -> raw value
    bool present = false;

    bool has(const QString &name) const { return values.contains(name.toLower()); }
    QString str(const QString &name) const { return values.value(name.toLower()); }
    // The first of `names` that holds a number (true / false count as 1 / 0).
    std::optional<double> number(std::initializer_list<const char *> names) const;
    double number(std::initializer_list<const char *> names, double fallback) const;
    bool enabled() const; // Enable="1" / "true"
};

// A Face or Border, with the gradient and texture written inside it.
struct TextArtPaintPart
{
    TextArtAttrs attrs;
    TextArtAttrs gradient;
    QList<QPair<double, qint64>> stops; // ColorPos: Pos as written, Color as written
    TextArtAttrs texture;
};

struct TextArtLayer
{
    TextArtAttrs attrs; // id, OffsetX, OffsetY, ...
    TextArtPaintPart face;
    TextArtPaintPart border;
    TextArtAttrs shadow;
    QList<TextArtAttrs> colorList; // ColorList/Color, kept as written (semantics not established)
};

struct TextArtStyle
{
    QString encoding; // "utf-8", "utf-16", "gb18030"
    bool textUnreadable = false; // GB18030 text this build cannot decode (names kept as ASCII)
    TextArtAttrs root;       // TextNode: version, Color, FillMode
    TextArtAttrs font;       // FontName (may be UTF-16BE hex), Size, Bold, Italic
    TextArtAttrs textData;   // CharSpace, LineSpace, ...
    QString text;            // CharData: Title attribute or element text, hex decoded
    TextArtLayer base;       // the text's own Face / Border / Shadow (outside ExternAttr)
    TextArtAttrs gradient;   // a Gradient outside Face / Border (Color1..ColorN)
    QList<QPair<double, qint64>> gradientStops; // its ColorPos children, if it has any
    TextArtAttrs texture;    // a Texture outside Face / Border
    TextArtAttrs background; // Background_01 (or Background)
    QList<TextArtLayer> layers; // ExternAttr/Layer, in document order
    QStringList unknownEnabled; // element names with Enable="1" this reader does not know
};

// True when `xml` is a TextArt file (a <TextNode> root with a TextParam), by its first bytes.
bool looksLikeTextArtXml(const QByteArray &xml);

// Reads a TextArt XML (bounded; UTF-8 with or without BOM, UTF-16 with BOM, else GB18030). No DTD
// or entity declarations are accepted. Empty optional, with `error` set, when it is not one.
std::optional<TextArtStyle> readTextArtXml(const QByteArray &xml, QString *error = nullptr);

struct TextArtConversion
{
    bool ok = false;
    QJsonObject templateJson; // a complete Dafin title template (format 1)
    QStringList notes;        // what was approximated or left out, in plain words
    QStringList missingTextures; // file names asked for and not usable (D-022)
    QString fidelity;         // "faithful", "approximated" or "unsupported"
    QString error;            // why nothing could be made
};

// `textureExists(name)`: whether a texture file the style names (a safe relative name, any
// %CaptionResourcePath% prefix removed) is in the item's own folder.
TextArtConversion convertTextArt(const TextArtStyle &style, const QString &templateId, const QString &label,
                                 const QString &category, const TitleContext &context = {},
                                 const std::function<bool(const QString &)> &textureExists = {});

// The safe relative file name a texture path names (backslashes turned, a %…% variable prefix
// dropped), or empty when it is absolute, climbs out ("..") or is empty.
QString textArtTextureName(const QString &path);

} // namespace dafin::linked
