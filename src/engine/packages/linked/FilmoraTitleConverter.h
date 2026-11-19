#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>

#include <functional>
#include <optional>

// Dafin: a Filmora title (a title item's Data/data.json, a small Filmora project holding one or
// more text clips) as a Dafin title template (docs/template-authoring.md), drawn by Dafin's own
// text engine and fully editable.
//
// Carried over: the text, font (family, weight, italic, size), fill (solid or gradient), outline,
// shadow, background box, underline, letter and line spacing, where it sits in the frame, and the
// entrance / exit / loop animations, matched by name to Dafin's closest presets. Filmora filters
// on the title that the linked folder also holds (ColorGlitch, ...) become the title's clip
// effects with their values; Filmora's built-in Glow, VectorTextShadow (long shadow) and 3D text
// are imitated with glow and extrude layers. Anything else is named in `leftOut`.
namespace dafin::linked {

// A Filmora filter already converted into a Dafin effect package.
struct LinkedEffectInfo
{
    struct Param
    {
        QString identifier; // "fm_amplitude"
        QString type;       // "float", "int", "bool"
        double min = 0.0;
        double max = 1.0;
    };
    QString dafinId; // "filmora.1262650"
    QList<Param> params;
};

// The family a Filmora font name ("Paytone One Regular") is installed under.
struct TitleFont
{
    QString family;
    int weight = 400;
    bool italic = false;
};

struct TitleContext
{
    // The Dafin effect made from Filmora resource `filmoraId`, if there is one.
    std::function<std::optional<LinkedEffectInfo>(qint64 filmoraId)> effect;
    // The installed family for a Filmora font name, if known.
    std::function<std::optional<TitleFont>(const QString &filmoraFontName)> font;
};

struct TitleConversion
{
    bool ok = false;
    QJsonObject templateJson; // a complete Dafin title template (format 1)
    QStringList leftOut;      // what could not be carried over, in plain words
    QString error;            // why nothing could be made
};

TitleConversion convertFilmoraTitle(const QByteArray &dataJson, const QString &templateId,
                                    const QString &label, const QString &category,
                                    const TitleContext &context = {});

// Filmora's colour integers are Windows COLORREFs (0x00BBGGRR): "#aarrggbb".
QString filmoraColor(qint64 value, int alpha = 255);

// Dafin's closest animation preset for a Filmora animation name ("Center Gradually Appears"),
// for slot "in", "out" or "loop"; extra preset params go into `params`. Empty when nothing fits.
QString dafinAnimationFor(const QString &filmoraName, const QString &slot, QJsonObject *params);

// "Paytone One Regular" -> family "Paytone One", weight 400; "Montserrat Bold Italic" -> 700, italic.
TitleFont splitFontName(const QString &filmoraFontName);

} // namespace dafin::linked
