#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>

#include <atomic>
#include <functional>

// Dafin: linked effect libraries. A folder of effects downloaded by Filmora (its Filter, Overlay,
// transition, Element, resFont, title ... resource folders), or simply a folder of fonts,
// stickers and .cube looks, is read and turned into ordinary Dafin packages under
// <AppData>/linked-libraries/<key>/{effects,transitions,stickers,fonts,looks,text-styles}. Those
// folders are search roots of their kinds (PackageSearchPaths; TitleTemplateCatalog for
// text-styles), so the converted items show up in the Effects, Transitions, Stickers, Looks,
// Titles and font lists exactly like the built-in ones.
//
// A Filmora item is a folder with a resinfo.json (its id, type, English title and categories) and
// the content under Data/:
//   type 3 filter, 4 effect   GPU shader (filter.xml -> <name>.conf -> <name>.frag) -> effect
//                             (a .cube in Data/ -> look)
//   type 2 transition         GPU shader with two inputs                             -> transition
//   type 7 element            a video (alpha WebM) or picture + thumbnail.png        -> sticker
//   type 35 font              Data/*.ttf|otf                                         -> font
//   type 1 title              a Dafin title template (FilmoraTitleConverter): text, font,
//                             fill, outline, shadow, animation, its Filmora filters as clip
//                             effects; plus the fonts in its Font/ folder         -> text-styles
//                             (several text elements: a Scene Title               -> scene-titles)
//   Art title                 a TextArt XML (<TextNode>, Data/data.xml or data.xml on its own,
//                             with or without resinfo.json): ordered shading layers -> text-styles
// A folder with Data/filter.xml but no resinfo.json is read too (type from filter.xml and the
// folder names). Files outside any item are used by what they are: fonts (a "Fonts" folder,
// say), .cube looks, and stickers (videos, pictures, Lottie JSON), categorised by their folder.
//
// Failsafes: the same item twice (same Filmora id, or the same files) is used once and the copies
// listed as duplicates; the same font face twice is registered once; a damaged or oversized file
// skips only its own item; a shader that crashed the graphics driver while being checked is left
// out from then on; an empty folder (a drive not connected) keeps what was converted before.
namespace dafin::linked {

struct ImportedItem
{
    QString kind;       // "effect", "transition", "sticker", "font", "look", "title"; empty when skipped
    QString name;       // English title, or the file name
    QString category;   // the Dafin category id it went into
    QString source;     // the item's folder, or the loose file
    QString skipReason; // not imported (or only partly), and why, in plain words
    QString fidelity;   // titles only: "faithful", "approximated" or "unsupported" (titles-report.txt)
};

struct ImportReport
{
    int effects = 0;
    int transitions = 0;
    int stickers = 0;
    int fonts = 0; // font families (a family can bring several faces)
    int looks = 0;
    int titles = 0; // Filmora titles turned into Dafin title templates
    QList<ImportedItem> items;  // every item and usable loose file found
    QStringList changedKinds;   // package kinds ("effects", "stickers", ...) whose files changed
    bool cancelled = false;
    QString error;              // the folder itself could not be used (nothing was changed)
    QString note;               // worth saying but not an error (a size cap was reached)

    int imported() const { return effects + transitions + stickers + fonts + looks + titles; }
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
    QString error;  // why not, in plain words: every problem found, not only the first
    // Stable machine-readable reasons behind `error`, one per problem, in the order they are
    // named there: "no-function", "bad-param-name", "renderer-values", "extra-inputs",
    // "own-samplers". Empty when ok.
    QStringList codes;
    // Things that did not stop the conversion but may explain a wrong or failing result later
    // (a parameter type Dafin reads as a plain number, GLSL that #version 330 core rejects).
    QStringList notes;
};

// A Filmora GPU shader (the .frag file as stored, base64 when the conf says ENCRYPT, and its
// .conf) as a Dafin package shader: FUNCNAME/INPUT/PREFIX(...) mapped onto Dafin's uniforms and
// samplers, one uniform per parameter. Refuses, naming every cause, anything Dafin can't supply
// faithfully: an unknown PREFIX name, extra INPUTn textures, textures the shader declares itself.
// It never guesses: an unsupported shader is left out rather than drawn wrongly.
ShaderTranslation translateFilmoraShader(const QByteArray &fragFile, const QJsonObject &conf,
                                         bool transition);

// Everything known about an item that hints at its category, strongest first.
struct CategoryHints
{
    QStringList filmoraCategories; // Filmora's category names ("Blur", "Light", "Social Media")
    QStringList categorySlugs;     // Filmora's category slugs ("trans_Light2_en")
    QString title;                 // "Color Glitch"
    QStringList folderNames;       // the item's slug / folder name and its parent folder
    QStringList paramNames;        // shader parameters ("blurRadius")
    QString shader;                // the shader code, for the words in it
};

// The Dafin category for an effect ("effects") or transition ("transitions"): every hint is
// scored against keyword lists for Dafin's own categories (Glitch, Blurs, Retro, Impact, Color,
// Dreamy; Basic, Geometric, Liquid, Glitch, Cinematic), the strongest hints counting most. The
// best score wins; with no real match, the Filmora category name as it is ("Social Media"); with
// none, "filmora".
QString smartCategory(const QString &packageKind, const CategoryHints &hints);
// Shorthand: just a Filmora category and a title.
QString dafinCategoryFor(const QString &packageKind, const QString &filmoraCategory,
                         const QString &title);

// What a font file says about itself (its 'name', 'OS/2' and 'post' tables).
struct FontInfo
{
    bool ok = false;
    QString family;       // typographic family ("Advent Pro"): what titles ask for by name
    QString legacyFamily; // the older per-style family ("Advent Pro SemiExpanded"), used when
                          // two faces of `family` would otherwise share a weight
    QString style;        // "SemiExpanded ExtraBold"
    int weight = 400; // 100..900
    bool italic = false;
    // Dafin's font group: "impact", "clean", "editorial" or "playful", from the font's class
    // (serif, sans, script, decorative), its weight and width, and the words in its name.
    QString category;
};
FontInfo readFontInfo(const QString &path);

// Reads every item and loose file under sourceRoot and writes Dafin packages under outRoot.
// Items converted before from unchanged files are kept as they are; packages of items that are
// gone are removed. Big media (element videos, fonts) is referenced in place, not copied, so the
// source folder must stay where it is.
ImportReport importFilmoraFolder(const QString &sourceRoot, const QString &outRoot,
                                 const ShaderCheck &check = {},
                                 const std::atomic_bool *cancel = nullptr);

// <AppData>/linked-libraries.
QString linkedLibrariesRoot();
// The folder one linked source folder converts into: linkedLibrariesRoot()/<hash of the path>.
QString outputDirFor(const QString &sourceFolder);

} // namespace dafin::linked
