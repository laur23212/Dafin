#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>

#include <atomic>
#include <functional>

// Dafin: linked effect libraries. A folder of effects downloaded by Filmora (its "Dafin extra
// effects"-style resource folders: Filter, Overlay, transition, Element, resFont, title, ...) is
// read once and turned into ordinary Dafin packages under
// <AppData>/linked-libraries/<key>/{effects,transitions,stickers,fonts,looks}. Those folders are
// search roots of their kinds (PackageSearchPaths), so the converted items show up in the
// Effects, Transitions, Stickers and font lists exactly like the built-in ones.
//
// Each Filmora item is a folder with a resinfo.json (its id, type, English title and categories)
// and the content under Data/:
//   type 3 filter, 4 effect   GPU shader (filter.xml -> <name>.conf -> <name>.frag) -> effect
//                             (a .cube in Data/ -> look)
//   type 2 transition         GPU shader with two inputs                             -> transition
//   type 7 element            a video (alpha WebM) or picture + thumbnail.png        -> sticker
//   type 35 font              Data/*.ttf|otf                                         -> font
//   type 1 title              only the fonts in its Font/ folder; the title itself needs
//                             Filmora's title engine and is listed as not supported
// Everything else (text animations, split screens, AI templates, ...) is listed with the reason.
namespace dafin::linked {

struct ImportedItem
{
    QString kind;       // "effect", "transition", "sticker", "font", "look"; empty when skipped
    QString name;       // English title
    QString category;   // the Dafin category id it went into
    QString source;     // the item's folder
    QString skipReason; // not imported, and why, in plain words
};

struct ImportReport
{
    int effects = 0;
    int transitions = 0;
    int stickers = 0;
    int fonts = 0;
    int looks = 0;
    QList<ImportedItem> items;  // every item found, imported or not
    QStringList changedKinds;   // package kinds ("effects", "stickers", ...) whose files changed
    bool cancelled = false;
    QString error;              // the folder itself could not be read

    int imported() const { return effects + transitions + stickers + fonts + looks; }
    int skipped() const;
};

// Compiles a finished fragment shader in a real GL context. Optional: without it every shader
// that translates is kept; with it, one that does not compile is listed as skipped instead of
// appearing as an effect that draws nothing.
using ShaderCheck = std::function<bool(const QString &fragmentSource, QString *error)>;

struct ShaderTranslation
{
    bool ok = false;
    QString source; // the whole Dafin fragment shader
    QString error;  // why not, in plain words
};

// A Filmora GPU shader (the .frag file as stored, base64 when the conf says ENCRYPT, and its
// .conf) as a Dafin package shader: FUNCNAME/INPUT/PREFIX(...) mapped onto Dafin's uniforms and
// samplers, one uniform per parameter. Fails, with the reason, on anything that needs Filmora's
// own render passes (an unknown PREFIX name, extra INPUTn textures).
ShaderTranslation translateFilmoraShader(const QByteArray &fragFile, const QJsonObject &conf,
                                         bool transition);

// The Dafin category for a Filmora item: one of Dafin's own when the Filmora category, or failing
// that the title, clearly matches ("Color Glitch" -> glitch); else the Filmora category name as it
// is ("Social Media"); else "filmora".
QString dafinCategoryFor(const QString &packageKind, const QString &filmoraCategory,
                         const QString &title);

// Reads every Filmora item under sourceRoot (any folder holding a resinfo.json, any depth) and
// writes Dafin packages under outRoot. Items converted before from unchanged files are kept as
// they are; packages of items that are gone are removed. Big media (element videos, fonts) is
// referenced in place, not copied, so the source folder must stay where it is.
ImportReport importFilmoraFolder(const QString &sourceRoot, const QString &outRoot,
                                 const ShaderCheck &check = {},
                                 const std::atomic_bool *cancel = nullptr);

// <AppData>/linked-libraries.
QString linkedLibrariesRoot();
// The folder one linked source folder converts into: linkedLibrariesRoot()/<hash of the path>.
QString outputDirFor(const QString &sourceFolder);

} // namespace dafin::linked
