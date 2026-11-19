#include "engine/packages/linked/FilmoraLibraryImporter.h"

#include "engine/packages/linked/FilmoraTitleConverter.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMap>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>

#include <functional>

#include <exception>

namespace dafin::linked {

namespace {

// Bump when the conversion changes, so every item is converted again.
// 3: variant folders ("All", "9" ...) inside an item are part of it, not items named "All".
// 4: Filmora titles become Dafin title templates (text-styles).
// 5: title colours read as 0xRRGGBB (they were read as COLORREFs, red and blue swapped).
constexpr int kImporterVersion = 5;

// Failsafe caps: a folder that is not what it seems (a whole drive, a damaged file) must not
// stall or exhaust the import.
constexpr qint64 kMaxSettingsFile = 2 * 1024 * 1024; // resinfo, conf, filter.xml, shader
constexpr qint64 kMaxFontFile = 64 * 1024 * 1024;
constexpr qint64 kMaxLottieFile = 20 * 1024 * 1024;
constexpr int kMaxFilesPerItem = 2000;
constexpr int kMaxFiles = 200000;

const QString kEffects = QStringLiteral("effects");
const QString kTransitions = QStringLiteral("transitions");
const QString kStickers = QStringLiteral("stickers");
const QString kFonts = QStringLiteral("fonts");
const QString kLooks = QStringLiteral("looks");
const QString kTitles = QStringLiteral("text-styles"); // TitleTemplateCatalog reads <root>/text-styles/*.json

// Filmora's resource types (resinfo.json "type").
enum FilmoraType {
    TypeUnknown = 0,
    TypeTitle = 1,
    TypeTransition = 2,
    TypeFilter = 3,
    TypeEffect = 4,
    TypeSplitScreen = 5,
    TypeMusic = 6,
    TypeElement = 7,
    TypeTextAnimation = 27,
    TypeFont = 35,
};

// ── files ────────────────────────────────────────────────────────────────────────────────────

QByteArray readSmall(const QString &path, qint64 maxBytes = kMaxSettingsFile)
{
    QFile file(path);
    if (file.size() > maxBytes || !file.open(QIODevice::ReadOnly))
        return {};
    return file.readAll();
}

QJsonObject readObject(const QString &path)
{
    return QJsonDocument::fromJson(readSmall(path)).object();
}

bool writeBytes(const QString &path, const QByteArray &bytes)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return false;
    if (file.write(bytes) != bytes.size()) {
        file.cancelWriting();
        return false;
    }
    return file.commit();
}

// An item's output: a package folder, or a single file (a title template).
void removeOutput(const QString &outRoot, const QString &rel)
{
    const QString path = QDir(outRoot).filePath(rel);
    if (QFileInfo(path).isDir())
        QDir(path).removeRecursively();
    else
        QFile::remove(path);
}

bool writeJson(const QString &path, const QJsonObject &object)
{
    return writeBytes(path, QJsonDocument(object).toJson(QJsonDocument::Indented));
}

QString hashOf(const QByteArray &bytes, int length)
{
    return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha1).toHex())
        .left(length);
}

QString hashOf(const QString &text, int length)
{
    return hashOf(text.toUtf8(), length);
}

// What a file is, independent of its name and place: size and the first 64 KB.
QString contentKey(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return QStringLiteral("unreadable:") + path;
    return QString::number(file.size()) + QLatin1Char(':') + hashOf(file.read(64 * 1024), 16);
}

const QStringList kVideoSuffixes = {QStringLiteral("webm"), QStringLiteral("mov"),
                                    QStringLiteral("mp4"), QStringLiteral("m4v"),
                                    QStringLiteral("mkv"), QStringLiteral("gif")};
const QStringList kImageSuffixes = {QStringLiteral("png"), QStringLiteral("webp"),
                                    QStringLiteral("apng"), QStringLiteral("svg"),
                                    QStringLiteral("jpg"), QStringLiteral("jpeg")};
// Loose pictures: no jpg, which in a folder of effects is far more often a preview than a sticker.
const QStringList kLooseImageSuffixes = {QStringLiteral("png"), QStringLiteral("webp"),
                                         QStringLiteral("apng"), QStringLiteral("svg")};
const QStringList kFontSuffixes = {QStringLiteral("ttf"), QStringLiteral("otf"),
                                   QStringLiteral("ttc"), QStringLiteral("otc")};

QStringList filesWithSuffix(const QString &dir, const QStringList &suffixes, bool recursive)
{
    QStringList out;
    QDirIterator it(dir, QDir::Files,
                    recursive ? QDirIterator::Subdirectories : QDirIterator::NoIteratorFlags);
    while (it.hasNext() && out.size() < kMaxFilesPerItem) {
        const QString path = it.next();
        if (suffixes.contains(QFileInfo(path).suffix().toLower()))
            out.append(path);
    }
    out.sort();
    return out;
}

bool looksLikeLottie(const QString &path)
{
    QFile file(path);
    if (file.size() > kMaxLottieFile || !file.open(QIODevice::ReadOnly))
        return false;
    const QByteArray head = file.read(64 * 1024);
    return head.trimmed().startsWith('{') && head.contains("\"layers\"") && head.contains("\"fr\"")
           && head.contains("\"ip\"");
}

// ── names ────────────────────────────────────────────────────────────────────────────────────

// Filmora numbers its folders: "2_2_Beam_Transition_3_3" -> "Beam_Transition_3_3".
QString withoutNumberPrefix(QString name)
{
    static const QRegularExpression kNumberPrefix(QStringLiteral("^(\\d+_)+"));
    return name.remove(kNumberPrefix);
}

QString humanName(const QString &folderOrFile)
{
    return withoutNumberPrefix(folderOrFile).replace(QLatin1Char('_'), QLatin1Char(' ')).simplified();
}

// A folder name that says nothing about the item: Filmora's variant and data folders ("All",
// "Data", "9", an aspect ratio "16_9"). An item found in one is named after the folder above.
bool isGenericFolderName(const QString &name)
{
    static const QRegularExpression kGeneric(
        QStringLiteral("^(all|data|default|\\d+|\\d+_\\d+)$"), QRegularExpression::CaseInsensitiveOption);
    return name.trimmed().isEmpty() || kGeneric.match(name.trimmed()).hasMatch();
}

// The folder that names an item: the item's own, or, while that has a generic name, the folder
// above it (at most three levels, never the drive root).
QString namingDir(const QString &itemDir)
{
    QString dir = QDir::cleanPath(itemDir);
    for (int depth = 0; depth < 3 && isGenericFolderName(QFileInfo(dir).fileName()); ++depth) {
        const QString parent = QFileInfo(dir).absolutePath();
        if (parent == dir || QDir(parent).isRoot())
            break;
        dir = parent;
    }
    return dir;
}

QString englishTitle(const QJsonObject &info, const QString &itemDir)
{
    const QJsonObject titles = info.value(QStringLiteral("titles")).toObject();
    QString title = titles.value(QStringLiteral("en")).toString().trimmed();
    if (title.isEmpty() && !titles.isEmpty())
        title = titles.begin().value().toString().trimmed();
    if (title.isEmpty())
        title = humanName(info.value(QStringLiteral("slug")).toString());
    if (title.isEmpty())
        title = humanName(QFileInfo(namingDir(itemDir)).fileName());
    return title;
}

// The item's card picture: its thumbnail.png, or that of the folder that names it.
QString thumbnailFor(const QString &itemDir)
{
    for (const QString &dir : {itemDir, namingDir(itemDir)}) {
        const QString thumb = QDir(dir).filePath(QStringLiteral("thumbnail.png"));
        if (QFileInfo(thumb).isFile())
            return thumb;
    }
    return {};
}

// A Data folder that holds a Filmora shader effect (filter.xml or a .conf).
bool hasShaderFiles(const QString &dataDir)
{
    const QDir data(dataDir);
    return QFileInfo(data.filePath(QStringLiteral("filter.xml"))).isFile()
           || !data.entryList({QStringLiteral("*.conf")}, QDir::Files).isEmpty();
}

// "colorInterval" -> "Color interval", "uRadius" -> "Radius", "glow_color" -> "Glow color".
QString prettyParamName(const QString &name)
{
    QString n = name;
    if (n.size() > 1 && n.at(0) == QLatin1Char('u') && n.at(1).isUpper())
        n = n.mid(1);
    QString out;
    for (int i = 0; i < n.size(); ++i) {
        const QChar c = n.at(i);
        if (c == QLatin1Char('_')) {
            out += QLatin1Char(' ');
            continue;
        }
        if (i > 0 && c.isUpper() && !n.at(i - 1).isUpper() && n.at(i - 1) != QLatin1Char('_'))
            out += QLatin1Char(' ');
        out += out.isEmpty() ? c.toUpper() : c.toLower();
    }
    return out.simplified();
}

QString slugOf(const QString &text)
{
    QString slug;
    for (const QChar c : text.toLower()) {
        if (c.isLetterOrNumber())
            slug += c;
        else if (!slug.endsWith(QLatin1Char('-')) && !slug.isEmpty())
            slug += QLatin1Char('-');
    }
    while (slug.endsWith(QLatin1Char('-')))
        slug.chop(1);
    return slug.isEmpty() ? QStringLiteral("x") : slug;
}

// Words of a name or of code: split at anything that is not a letter and where lower case turns
// to upper ("Blur2Clear" -> blur, clear; "trans_Light2_en" -> trans, light, en).
QStringList wordsOf(const QString &text)
{
    QStringList words;
    QString current;
    auto flush = [&] {
        if (current.size() >= 2)
            words.append(current.toLower());
        current.clear();
    };
    for (int i = 0; i < text.size(); ++i) {
        const QChar c = text.at(i);
        if (!c.isLetter()) {
            flush();
            continue;
        }
        if (c.isUpper() && i > 0 && text.at(i - 1).isLower())
            flush();
        current += c;
    }
    flush();
    return words;
}

// ── shaders ──────────────────────────────────────────────────────────────────────────────────

// Strips // and /* */ comments, so a commented-out PREFIX(...) is not taken for a real one.
QString withoutComments(const QString &source)
{
    QString out;
    out.reserve(source.size());
    for (int i = 0; i < source.size(); ++i) {
        if (source.at(i) == QLatin1Char('/') && i + 1 < source.size()) {
            if (source.at(i + 1) == QLatin1Char('/')) {
                while (i < source.size() && source.at(i) != QLatin1Char('\n'))
                    ++i;
                out += QLatin1Char('\n');
                continue;
            }
            if (source.at(i + 1) == QLatin1Char('*')) {
                const int end = source.indexOf(QStringLiteral("*/"), i + 2);
                // Keep the line count, so compiler errors still point at the right line.
                const QString skipped = source.mid(i, end < 0 ? -1 : end + 2 - i);
                out += QString(skipped.count(QLatin1Char('\n')), QLatin1Char('\n'));
                if (end < 0)
                    break;
                i = end + 1;
                continue;
            }
        }
        out += source.at(i);
    }
    return out;
}

QString firstLine(const QString &text)
{
    const QString trimmed = text.trimmed();
    const int nl = trimmed.indexOf(QLatin1Char('\n'));
    return (nl < 0 ? trimmed : trimmed.left(nl)).left(160);
}

// ── fonts ────────────────────────────────────────────────────────────────────────────────────

class FontBytes
{
public:
    explicit FontBytes(const QByteArray &data) : m_data(data) {}
    bool has(qint64 offset, qint64 length) const
    {
        return offset >= 0 && length >= 0 && offset + length <= m_data.size();
    }
    quint32 u8(qint64 o) const { return has(o, 1) ? quint8(m_data.at(o)) : 0; }
    quint32 u16(qint64 o) const { return (u8(o) << 8) | u8(o + 1); }
    quint32 u32(qint64 o) const { return (u16(o) << 16) | u16(o + 2); }
    QByteArray bytes(qint64 o, qint64 n) const { return has(o, n) ? m_data.mid(o, n) : QByteArray(); }

private:
    const QByteArray &m_data;
};

constexpr quint32 tag(char a, char b, char c, char d)
{
    return (quint32(quint8(a)) << 24) | (quint32(quint8(b)) << 16) | (quint32(quint8(c)) << 8)
           | quint32(quint8(d));
}

QString fontCategoryFor(const FontInfo &info, int familyClass, int panoseFamily, int panoseSerif,
                        int widthClass, bool monospace)
{
    // Most fonts leave their class and PANOSE fields empty, so the name is a signal too: words that
    // describe a style, and well-known families of each kind.
    const QString words = (info.family + QLatin1Char(' ') + info.style).toLower();
    auto has = [&](std::initializer_list<const char *> list) {
        for (const char *w : list) {
            if (words.contains(QLatin1String(w)))
                return true;
        }
        return false;
    };
    if (monospace || has({"mono", "code", "console"}))
        return QStringLiteral("clean");
    // Script, hand-written, decorative and rounded "fun" faces.
    if (panoseFamily == 3 || panoseFamily == 4 || panoseFamily == 5 || familyClass == 9
        || familyClass == 10 || familyClass == 12
        || has({"script", "hand", "brush", "marker", "comic", "callig", "graffiti", "doodle", "chalk",
                "cartoon", "bubble", "baloo", "fredoka", "lobster", "pacifico", "chewy", "kids", "candy"}))
        return QStringLiteral("playful");
    const bool serif = (familyClass >= 1 && familyClass <= 5) || familyClass == 7
                       || (panoseFamily == 2 && panoseSerif >= 2 && panoseSerif <= 10)
                       || (has({"serif"}) && !has({"sans"}))
                       || has({"slab", "garamond", "didot", "bodoni", "caslon", "baskerville", "times",
                               "georgia", "merriweather", "playfair", "lora", "charis", "gentium", "amiri",
                               "crimson", "cormorant", "spectral", "domine", "bitter", "arvo", "zilla",
                               "cardo", "prata", "vollkorn", "literata", "alegreya", "libre bask",
                               "old standard", "roman", "book "});
    if (serif)
        return QStringLiteral("editorial");
    const bool heavy = info.weight >= 800 || widthClass <= 3
                       || (has({"display"}) && info.weight >= 700)
                       || has({"black", "heavy", "condensed", "compressed", "narrow", "impact", "poster",
                               "ultra", "bangers", "anton", "bebas", "oswald"});
    return heavy ? QStringLiteral("impact") : QStringLiteral("clean");
}

// ── categories ───────────────────────────────────────────────────────────────────────────────

struct CategoryRule
{
    QString id;
    QStringList words;  // matched against names (categories, titles, folders)
    QStringList strong; // matched against code (parameter names, shader): only telling words
};

const QList<CategoryRule> &effectRules()
{
    static const QList<CategoryRule> rules = {
        {QStringLiteral("glitch"),
         {QStringLiteral("glitch"), QStringLiteral("distort"), QStringLiteral("digital"),
          QStringLiteral("chromatic"), QStringLiteral("rgb"), QStringLiteral("datamosh"),
          QStringLiteral("jitter"), QStringLiteral("pixel"), QStringLiteral("displace")},
         {QStringLiteral("glitch"), QStringLiteral("chromatic"), QStringLiteral("rgbsplit"),
          QStringLiteral("datamosh"), QStringLiteral("jitter"), QStringLiteral("displace")}},
        {QStringLiteral("blurs"),
         {QStringLiteral("blur"), QStringLiteral("defocus"), QStringLiteral("gaussian"),
          QStringLiteral("bokeh"), QStringLiteral("focus"), QStringLiteral("mosaic")},
         {QStringLiteral("blur"), QStringLiteral("gaussian"), QStringLiteral("kawase"),
          QStringLiteral("bokeh"), QStringLiteral("defocus")}},
        {QStringLiteral("retro"),
         {QStringLiteral("retro"), QStringLiteral("vintage"), QStringLiteral("vhs"), QStringLiteral("film"),
          QStringLiteral("grain"), QStringLiteral("crt"), QStringLiteral("scanline"), QStringLiteral("sepia"),
          QStringLiteral("analog"), QStringLiteral("nostalgi"), QStringLiteral("polaroid")},
         {QStringLiteral("grain"), QStringLiteral("scanline"), QStringLiteral("vhs"), QStringLiteral("sepia"),
          QStringLiteral("vignette"), QStringLiteral("crt")}},
        {QStringLiteral("impact"),
         {QStringLiteral("shake"), QStringLiteral("impact"), QStringLiteral("punch"), QStringLiteral("zoom"),
          QStringLiteral("pulse"), QStringLiteral("strobe"), QStringLiteral("flash"), QStringLiteral("beat"),
          QStringLiteral("quake")},
         {QStringLiteral("shake"), QStringLiteral("quake"), QStringLiteral("strobe")}},
        {QStringLiteral("color"),
         {QStringLiteral("color"), QStringLiteral("colour"), QStringLiteral("tone"), QStringLiteral("lut"),
          QStringLiteral("hue"), QStringLiteral("saturat"), QStringLiteral("tint"), QStringLiteral("grade"),
          QStringLiteral("contrast"), QStringLiteral("temperature"), QStringLiteral("monochrom")},
         {QStringLiteral("hue"), QStringLiteral("saturation"), QStringLiteral("lut"), QStringLiteral("tint")}},
        {QStringLiteral("dreamy"),
         {QStringLiteral("glow"), QStringLiteral("bloom"), QStringLiteral("light"), QStringLiteral("flare"),
          QStringLiteral("leak"), QStringLiteral("dream"), QStringLiteral("haze"), QStringLiteral("sparkl"),
          QStringLiteral("shine"), QStringLiteral("star"), QStringLiteral("magic")},
         {QStringLiteral("glow"), QStringLiteral("bloom"), QStringLiteral("flare"), QStringLiteral("sparkle")}},
    };
    return rules;
}

const QList<CategoryRule> &transitionRules()
{
    static const QList<CategoryRule> rules = {
        {QStringLiteral("basic"),
         {QStringLiteral("basic"), QStringLiteral("dissolve"), QStringLiteral("fade"), QStringLiteral("crossfade"),
          QStringLiteral("cross"), QStringLiteral("mix"), QStringLiteral("blend")},
         {QStringLiteral("dissolve"), QStringLiteral("crossfade")}},
        {QStringLiteral("glitch"),
         {QStringLiteral("glitch"), QStringLiteral("digital"), QStringLiteral("rgb"), QStringLiteral("noise"),
          QStringLiteral("pixel"), QStringLiteral("datamosh"), QStringLiteral("mosaic")},
         {QStringLiteral("glitch"), QStringLiteral("datamosh"), QStringLiteral("chromatic")}},
        {QStringLiteral("liquid"),
         {QStringLiteral("ink"), QStringLiteral("liquid"), QStringLiteral("particle"), QStringLiteral("smoke"),
          QStringLiteral("water"), QStringLiteral("drop"), QStringLiteral("melt"), QStringLiteral("paint"),
          QStringLiteral("splash"), QStringLiteral("fluid"), QStringLiteral("wave"), QStringLiteral("burn")},
         {QStringLiteral("ink"), QStringLiteral("fluid"), QStringLiteral("particle"), QStringLiteral("smoke")}},
        {QStringLiteral("geometric"),
         {QStringLiteral("wipe"), QStringLiteral("slide"), QStringLiteral("push"), QStringLiteral("grid"),
          QStringLiteral("shape"), QStringLiteral("split"), QStringLiteral("box"), QStringLiteral("circle"),
          QStringLiteral("diamond"), QStringLiteral("iris"), QStringLiteral("cube"), QStringLiteral("page"),
          QStringLiteral("door"), QStringLiteral("stripe"), QStringLiteral("blind"), QStringLiteral("square"),
          QStringLiteral("clock"), QStringLiteral("radial"), QStringLiteral("geometr")},
         {QStringLiteral("wipe"), QStringLiteral("stripe"), QStringLiteral("grid")}},
        {QStringLiteral("cinematic"),
         {QStringLiteral("light"), QStringLiteral("flare"), QStringLiteral("beam"), QStringLiteral("zoom"),
          QStringLiteral("blur"), QStringLiteral("lens"), QStringLiteral("film"), QStringLiteral("warp"),
          QStringLiteral("spin"), QStringLiteral("whip"), QStringLiteral("leak"), QStringLiteral("flash"),
          QStringLiteral("speed"), QStringLiteral("cinema"), QStringLiteral("camera")},
         {QStringLiteral("flare"), QStringLiteral("lens"), QStringLiteral("whip"), QStringLiteral("zoom")}},
    };
    return rules;
}

// Words that say nothing about the category however often they appear.
bool isNoiseWord(const QString &word)
{
    static const QSet<QString> kNoise = {
        QStringLiteral("filmora"), QStringLiteral("wondershare"), QStringLiteral("en"),
        QStringLiteral("trans"), QStringLiteral("transition"), QStringLiteral("transitions"),
        QStringLiteral("effect"), QStringLiteral("effects"), QStringLiteral("element"),
        QStringLiteral("elements"), QStringLiteral("filter"), QStringLiteral("filters"),
        QStringLiteral("overlay"), QStringLiteral("overlays"), QStringLiteral("pack"),
        QStringLiteral("data"), QStringLiteral("res"),
    };
    return kNoise.contains(word);
}

bool wordMatches(const QString &word, const QString &keyword)
{
    return word == keyword || (keyword.size() >= 4 && word.startsWith(keyword));
}

// How many of the rule's keywords appear among the words (each keyword once).
int keywordHits(const QStringList &words, const QStringList &keywords)
{
    int hits = 0;
    for (const QString &keyword : keywords) {
        for (const QString &word : words) {
            if (!isNoiseWord(word) && wordMatches(word, keyword)) {
                ++hits;
                break;
            }
        }
    }
    return hits;
}

// ── records ──────────────────────────────────────────────────────────────────────────────────

struct Record
{
    QString stamp;
    QString kind;     // "effect", ... or empty when skipped
    QString name;
    QString category;
    QString skipReason;
    QStringList outputs; // package folders, relative to outRoot
    QJsonObject sticker; // a sticker's pack.json entry
    QJsonArray fonts;    // font faces it brings: [{file, family, legacyFamily, style, weight, italic, category}]

    QJsonObject toJson() const
    {
        QJsonObject o{{QStringLiteral("stamp"), stamp},
                      {QStringLiteral("kind"), kind},
                      {QStringLiteral("name"), name},
                      {QStringLiteral("category"), category},
                      {QStringLiteral("skipReason"), skipReason},
                      {QStringLiteral("outputs"), QJsonArray::fromStringList(outputs)}};
        if (!sticker.isEmpty())
            o.insert(QStringLiteral("sticker"), sticker);
        if (!fonts.isEmpty())
            o.insert(QStringLiteral("fonts"), fonts);
        return o;
    }

    static Record fromJson(const QJsonObject &o)
    {
        Record r;
        r.stamp = o.value(QStringLiteral("stamp")).toString();
        r.kind = o.value(QStringLiteral("kind")).toString();
        r.name = o.value(QStringLiteral("name")).toString();
        r.category = o.value(QStringLiteral("category")).toString();
        r.skipReason = o.value(QStringLiteral("skipReason")).toString();
        for (const QJsonValue &v : o.value(QStringLiteral("outputs")).toArray())
            r.outputs.append(v.toString());
        r.sticker = o.value(QStringLiteral("sticker")).toObject();
        r.fonts = o.value(QStringLiteral("fonts")).toArray();
        return r;
    }
};

// A stamp that changes when the item's files do.
QString stampFor(const QString &itemOrFile)
{
    QStringList parts{QString::number(kImporterVersion)};
    const QFileInfo root(itemOrFile);
    if (root.isFile()) {
        parts.append(QStringLiteral("%1:%2").arg(root.size()).arg(root.lastModified().toMSecsSinceEpoch()));
        return hashOf(parts.join(QLatin1Char('|')), 20);
    }
    QDirIterator it(itemOrFile, QDir::Files, QDirIterator::Subdirectories);
    QStringList files;
    while (it.hasNext() && files.size() < kMaxFilesPerItem) {
        const QString path = it.next();
        const QFileInfo fi(path);
        files.append(QStringLiteral("%1:%2:%3")
                         .arg(QDir(itemOrFile).relativeFilePath(path))
                         .arg(fi.size())
                         .arg(fi.lastModified().toMSecsSinceEpoch()));
    }
    files.sort();
    parts += files;
    return hashOf(parts.join(QLatin1Char('|')), 20);
}

// Who an item is, wherever it sits: its Filmora id, or (without one) what its files are. The same
// item copied twice into the folder has the same identity, and only the first is used.
QString identityFor(const QJsonObject &info, int type, const QString &itemDir)
{
    const qint64 id = info.value(QStringLiteral("id")).toVariant().toLongLong();
    if (id > 0)
        return QStringLiteral("fm:%1:%2").arg(type).arg(id);
    QStringList files;
    QDirIterator it(QDir(itemDir).filePath(QStringLiteral("Data")), QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext() && files.size() < kMaxFilesPerItem) {
        const QString path = it.next();
        files.append(QDir(itemDir).relativeFilePath(path) + QLatin1Char(':')
                     + QString::number(QFileInfo(path).size()));
    }
    files.sort();
    return QStringLiteral("content:%1:%2").arg(type).arg(hashOf(files.join(QLatin1Char('|')), 16));
}

// The item's output folder name: its Filmora id when it has one (stable across renames).
QString packageNameFor(const QJsonObject &info, const QString &itemDir)
{
    const qint64 id = info.value(QStringLiteral("id")).toVariant().toLongLong();
    if (id > 0)
        return QStringLiteral("fm-%1").arg(id);
    return QStringLiteral("fm-") + hashOf(QDir::cleanPath(itemDir), 12);
}

QString kindDir(const QString &kind)
{
    if (kind == QLatin1String("effect"))
        return kEffects;
    if (kind == QLatin1String("transition"))
        return kTransitions;
    if (kind == QLatin1String("sticker"))
        return kStickers;
    if (kind == QLatin1String("font"))
        return kFonts;
    if (kind == QLatin1String("look"))
        return kLooks;
    if (kind == QLatin1String("title"))
        return kTitles;
    return {};
}

QJsonObject fontToJson(const QString &file, const FontInfo &info)
{
    return QJsonObject{{QStringLiteral("file"), file},
                       {QStringLiteral("family"), info.family},
                       {QStringLiteral("legacyFamily"), info.legacyFamily},
                       {QStringLiteral("style"), info.style},
                       {QStringLiteral("weight"), info.weight},
                       {QStringLiteral("italic"), info.italic},
                       {QStringLiteral("category"), info.category}};
}

const QStringList kAudioSuffixes = {QStringLiteral("mp3"), QStringLiteral("wav"), QStringLiteral("m4a"),
                                    QStringLiteral("aac"), QStringLiteral("flac"), QStringLiteral("ogg")};

// What an item is by what is in it, whatever its resinfo.json says and wherever it sits: a
// scrambled library (a split screen beside a title and a transition, items in the wrong
// folders, a resinfo.json lost) still sorts. TypeUnknown when nothing tells.
// `dataDir` is where the item's shader files are when not in its own Data/ (a variant folder).
int typeFromContent(const QString &itemDir, const QString &dataDir = {})
{
    const QDir item(itemDir);
    const QDir data(dataDir.isEmpty() ? item.filePath(QStringLiteral("Data")) : dataDir);
    const QByteArray filterXml = readSmall(data.filePath(QStringLiteral("filter.xml")));
    const QStringList confs = data.entryList({QStringLiteral("*.conf")}, QDir::Files);
    if (!filterXml.isEmpty() || !confs.isEmpty()) {
        if (filterXml.contains("<Transition>1</Transition>"))
            return TypeTransition;
        for (const QString &conf : confs) {
            const QJsonObject o = QJsonDocument::fromJson(readSmall(data.filePath(conf))).object();
            if (o.value(QStringLiteral("TRANSITION")).toVariant().toInt() == 1)
                return TypeTransition;
        }
        return TypeEffect;
    }
    const QByteArray dataJson = readSmall(data.filePath(QStringLiteral("data.json")));
    const QByteArray configJson = readSmall(data.filePath(QStringLiteral("config.json")));
    if (dataJson.contains("alg_code") || configJson.contains("alg_code"))
        return 106; // an AI template
    if (dataJson.contains("TlbTitleClip"))
        return TypeTitle;
    if (dataJson.contains("MotionAnimation") || dataJson.contains("effectChain"))
        return TypeTextAnimation;
    // Split screens come as one folder per aspect ratio (16_9, 9_16, ...).
    static const QRegularExpression kAspect(QStringLiteral("^\\d+_\\d+$"));
    for (const QString &sub : item.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        if (kAspect.match(sub).hasMatch() && QFileInfo(item.filePath(sub + QStringLiteral("/Data"))).isDir())
            return TypeSplitScreen;
    }
    if (!filesWithSuffix(data.absolutePath(), {QStringLiteral("cube")}, true).isEmpty())
        return TypeFilter;
    if (!filesWithSuffix(data.absolutePath(), kFontSuffixes, true).isEmpty())
        return TypeFont;
    if (!filesWithSuffix(data.absolutePath(), kVideoSuffixes, true).isEmpty()
        || !filesWithSuffix(data.absolutePath(), kImageSuffixes, true).isEmpty())
        return TypeElement;
    for (const QString &json : filesWithSuffix(data.absolutePath(), {QStringLiteral("json")}, true)) {
        if (looksLikeLottie(json))
            return TypeElement;
    }
    if (!filesWithSuffix(data.absolutePath(), kAudioSuffixes, true).isEmpty())
        return TypeMusic;
    return TypeUnknown;
}

// Types Dafin can turn into something.
bool isConvertibleType(int type)
{
    return type == TypeTransition || type == TypeFilter || type == TypeEffect || type == TypeElement
           || type == TypeFont || type == TypeTitle;
}

// Last resort, for a folder whose content says nothing: the names of the folders it sits in
// ("transition", "Filter", "Element", ...).
int typeFromFolders(const QString &itemDir)
{
    const QByteArray filterXml = readSmall(QDir(itemDir).filePath(QStringLiteral("Data/filter.xml")));
    if (filterXml.contains("<Transition>1</Transition>"))
        return TypeTransition;
    QDir dir(itemDir);
    for (int depth = 0; depth < 3 && dir.cdUp(); ++depth) {
        const QString name = dir.dirName().toLower();
        if (name.contains(QLatin1String("transition")))
            return TypeTransition;
        if (name.contains(QLatin1String("filter")))
            return TypeFilter;
        if (name.contains(QLatin1String("overlay")) || name.contains(QLatin1String("effect")))
            return TypeEffect;
        if (name.contains(QLatin1String("element")) || name.contains(QLatin1String("sticker")))
            return TypeElement;
        if (name.contains(QLatin1String("font")))
            return TypeFont;
        if (name == QLatin1String("title") || name == QLatin1String("titles"))
            return TypeTitle;
    }
    return filterXml.isEmpty() ? TypeUnknown : TypeEffect;
}

// ── conversion ───────────────────────────────────────────────────────────────────────────────

class Converter
{
public:
    Converter(const QString &outRoot, const ShaderCheck &check, const QString &crashedKey,
              const QSet<QString> &itemDirs, const QHash<QString, QStringList> &variantData)
        : m_outRoot(outRoot), m_check(check), m_crashedKey(crashedKey), m_itemDirs(itemDirs),
          m_variantData(variantData)
    {
    }

    // Where the item's shader files are: its own Data/, or, when that has none, the Data/ of one
    // of its variant folders (Filmora keeps some effects' files only under All/Data).
    QString dataDirFor(const QString &itemDir) const
    {
        const QString own = QDir(itemDir).filePath(QStringLiteral("Data"));
        if (hasShaderFiles(own))
            return own;
        for (const QString &variant : m_variantData.value(itemDir)) {
            if (hasShaderFiles(variant))
                return variant;
        }
        return own;
    }

    static QString markerPath(const QString &outRoot)
    {
        return QDir(outRoot).filePath(QStringLiteral("checking.txt"));
    }

    Record convertItem(const QString &key, const QString &itemDir, const QJsonObject &info, int type)
    {
        Record r;
        r.name = englishTitle(info, itemDir);
        if (info.value(QStringLiteral("is_encriped")).toInt() == 1) {
            r.skipReason = QStringLiteral("Encrypted by Filmora");
            return r;
        }
        if (key == m_crashedKey) {
            r.skipReason = QStringLiteral("Dafin closed while checking it last time, so it is left out");
            return r;
        }
        switch (type) {
        case TypeFilter:
        case TypeEffect:
            convertShaderOrLook(key, itemDir, info, false, &r);
            break;
        case TypeTransition:
            convertShaderOrLook(key, itemDir, info, true, &r);
            break;
        case TypeElement:
            convertElement(itemDir, info, &r);
            break;
        case TypeFont: {
            // Data/ holds the font; some packs repeat it under All/Data, which the face
            // de-duplication then drops.
            QStringList files =
                filesWithSuffix(QDir(itemDir).filePath(QStringLiteral("Data")), kFontSuffixes, false);
            if (files.isEmpty())
                files = ownFiles(itemDir, itemDir, kFontSuffixes, true);
            addFonts(files, &r);
            if (r.kind.isEmpty() && r.skipReason.isEmpty())
                r.skipReason = QStringLiteral("No usable font file in the folder");
            break;
        }
        case TypeTitle: {
            // Its own fonts first (the title asks for them by name), then the title itself.
            addFonts(filesWithSuffix(QDir(itemDir).filePath(QStringLiteral("Font")), kFontSuffixes, false), &r);
            convertTitle(itemDir, info, &r);
            break;
        }
        case TypeTextAnimation:
            r.skipReason = QStringLiteral("Text animation: uses an animation built into Filmora, not in the folder");
            break;
        case TypeSplitScreen: {
            static const QRegularExpression kBasic(QStringLiteral("^Split_Screen_(\\d+)"),
                                                   QRegularExpression::CaseInsensitiveOption);
            const QRegularExpressionMatch m = kBasic.match(info.value(QStringLiteral("slug")).toString());
            r.skipReason = m.hasMatch() && m.captured(1).toInt() <= 30
                               ? QStringLiteral("Split screen: already built into Dafin (Templates › Split Screen)")
                               : QStringLiteral("Split screen: not supported yet");
            break;
        }
        case TypeMusic:
            r.skipReason = QStringLiteral("Music: add audio files through Media instead");
            break;
        default:
            if (type >= 100 || !info.value(QStringLiteral("extension")).toObject()
                                    .value(QStringLiteral("extra_cfg")).toString().isEmpty())
                r.skipReason = QStringLiteral("AI template: runs on Wondershare's servers");
            else if (type == TypeUnknown)
                r.skipReason = QStringLiteral("Could not tell what kind of item this is");
            else
                r.skipReason = QStringLiteral("Filmora item type %1: not supported yet").arg(type);
            break;
        }
        if (r.kind.isEmpty() && r.skipReason.isEmpty())
            r.skipReason = QStringLiteral("Nothing usable in the folder");
        return r;
    }

    // A file outside any item: used by what it is.
    Record convertLooseFile(const QString &path, const QString &libraryName)
    {
        Record r;
        const QFileInfo fi(path);
        const QString suffix = fi.suffix().toLower();
        r.name = humanName(fi.completeBaseName());
        // Its folder names the category ("Arrows/arrow-1.webm" -> Arrows); at the top, the library.
        const QString parent = fi.absoluteDir().dirName();
        const QString category = humanName(parent).isEmpty() ? libraryName : humanName(parent);
        if (kFontSuffixes.contains(suffix)) {
            addFonts({path}, &r);
            if (r.kind.isEmpty())
                r.skipReason = QStringLiteral("Not a font Dafin can read");
        } else if (suffix == QLatin1String("cube")) {
            convertLook(QStringLiteral("lf-") + hashOf(QDir::cleanPath(path), 12), path, r.name,
                        category, &r);
        } else {
            r.kind = QStringLiteral("sticker");
            r.category = category;
            r.sticker = QJsonObject{
                {QStringLiteral("id"), QStringLiteral("lf-") + hashOf(QDir::cleanPath(path), 12)},
                {QStringLiteral("label"), r.name},
                {QStringLiteral("category"), slugOf(category)},
                {QStringLiteral("categoryLabel"), category},
                {QStringLiteral("file"), fi.absoluteFilePath()},
            };
        }
        return r;
    }

private:
    void convertShaderOrLook(const QString &key, const QString &itemDir, const QJsonObject &info,
                             bool transition, Record *r)
    {
        const QDir data(dataDirFor(itemDir));
        QString confName;
        static const QRegularExpression kConfig(QStringLiteral("<ConfigFile>\\s*([^<]+?)\\s*</ConfigFile>"));
        const QRegularExpressionMatch m =
            kConfig.match(QString::fromUtf8(readSmall(data.filePath(QStringLiteral("filter.xml")))));
        if (m.hasMatch())
            confName = QFileInfo(m.captured(1)).fileName(); // never a path out of Data/
        if (confName.isEmpty())
            confName = data.entryList({QStringLiteral("*.conf")}, QDir::Files, QDir::Name).value(0);

        if (confName.isEmpty()) {
            if (!transition) {
                const QString cube = ownFiles(itemDir, data.absolutePath(), {QStringLiteral("cube")}, true).value(0);
                if (!cube.isEmpty()) {
                    const QString category = categoryNames(info).value(0);
                    convertLook(packageNameFor(info, itemDir), cube, r->name,
                                category.isEmpty() ? QStringLiteral("Filmora") : category, r);
                    return;
                }
            }
            r->skipReason = QStringLiteral("Not a shader effect (made with Filmora's own effect engine)");
            return;
        }

        const QJsonObject conf = QJsonDocument::fromJson(readSmall(data.filePath(confName))).object();
        if (conf.isEmpty()) {
            r->skipReason = QStringLiteral("Its settings file is damaged or could not be read");
            return;
        }
        if (conf.value(QStringLiteral("TRANSITION")).toVariant().toInt() == 1)
            transition = true;
        const QString fragName = QFileInfo(conf.value(QStringLiteral("OPENGL")).toString()).fileName();
        if (fragName.isEmpty()) {
            r->skipReason = QStringLiteral("No OpenGL shader in it (OpenCL only)");
            return;
        }
        const QByteArray frag = readSmall(data.filePath(fragName));
        if (frag.isEmpty()) {
            r->skipReason = QStringLiteral("Its shader file is missing or too large");
            return;
        }
        const ShaderTranslation shader = translateFilmoraShader(frag, conf, transition);
        if (!shader.ok) {
            r->skipReason = shader.error;
            return;
        }
        if (m_check) {
            // If the graphics driver takes Dafin down while compiling this, the marker is still
            // there on the next start, and this item is left out from then on.
            writeBytes(markerPath(m_outRoot), key.toUtf8());
            QString error;
            const bool compiled = m_check(shader.source, &error);
            QFile::remove(markerPath(m_outRoot));
            if (!compiled) {
                r->skipReason = QStringLiteral("Its shader does not compile here: %1").arg(firstLine(error));
                return;
            }
        }

        const QString kind = transition ? QStringLiteral("transition") : QStringLiteral("effect");
        const QString rel = kindDir(kind) + QLatin1Char('/') + packageNameFor(info, itemDir);
        QDir out(QDir(m_outRoot).filePath(rel));
        out.removeRecursively();
        if (!QDir().mkpath(out.absolutePath())) {
            r->skipReason = QStringLiteral("Could not save the converted copy (is the disk full?)");
            return;
        }

        QJsonArray params;
        QStringList paramNames;
        for (const QJsonValue &value : conf.value(QStringLiteral("PARAMS")).toArray()) {
            const QJsonObject p = value.toObject();
            const QString name = p.value(QStringLiteral("NAME")).toString();
            if (name.isEmpty())
                continue;
            paramNames.append(name);
            const QString type = p.value(QStringLiteral("TYPE")).toString().toLower();
            QJsonObject param{
                {QStringLiteral("identifier"), QStringLiteral("fm_") + name},
                {QStringLiteral("displayName"), prettyParamName(name)},
                {QStringLiteral("type"), type == QLatin1String("int") ? QStringLiteral("int")
                                         : type == QLatin1String("bool") ? QStringLiteral("bool")
                                                                         : QStringLiteral("float")},
                {QStringLiteral("group"), QStringLiteral("Controls")},
            };
            if (type == QLatin1String("bool")) {
                param.insert(QStringLiteral("defaultValue"),
                             p.value(QStringLiteral("DEFAULT")).toVariant().toDouble() > 0.5);
            } else {
                double lo = p.value(QStringLiteral("MIN")).toVariant().toDouble();
                double hi = p.value(QStringLiteral("MAX")).toVariant().toDouble();
                if (hi <= lo) // a damaged range must not make an unusable slider
                    hi = lo + 1.0;
                const double def = qBound(lo, p.value(QStringLiteral("DEFAULT")).toVariant().toDouble(), hi);
                param.insert(QStringLiteral("minValue"), lo);
                param.insert(QStringLiteral("maxValue"), hi);
                param.insert(QStringLiteral("defaultValue"), def);
            }
            params.append(param);
        }

        CategoryHints hints;
        hints.filmoraCategories = categoryNames(info);
        for (const QJsonValue &c : info.value(QStringLiteral("categories")).toArray())
            hints.categorySlugs.append(c.toObject().value(QStringLiteral("slug")).toString());
        hints.title = r->name;
        hints.folderNames = {info.value(QStringLiteral("slug")).toString(),
                             QFileInfo(namingDir(itemDir)).fileName()};
        hints.paramNames = paramNames;
        hints.shader = shader.source;
        const QString category = smartCategory(kindDir(kind), hints);

        QJsonArray inputs;
        if (transition) {
            inputs = QJsonArray{QJsonObject{{QStringLiteral("type"), QStringLiteral("source_texture")},
                                            {QStringLiteral("index"), 0}},
                                QJsonObject{{QStringLiteral("type"), QStringLiteral("source_texture")},
                                            {QStringLiteral("index"), 1}}};
        } else {
            inputs = QJsonArray{QJsonObject{{QStringLiteral("type"), QStringLiteral("source_texture")}}};
        }
        QJsonObject pkg{
            {QStringLiteral("id"), QStringLiteral("filmora.%1").arg(packageNameFor(info, itemDir).mid(3))},
            {QStringLiteral("displayName"), r->name},
            {QStringLiteral("category"), category},
            {QStringLiteral("order"), 90000},
            {QStringLiteral("parameters"), params},
            {QStringLiteral("pipeline"),
             QJsonObject{{QStringLiteral("intermediateBuffers"), QJsonArray{}},
                         {QStringLiteral("passes"),
                          QJsonArray{QJsonObject{{QStringLiteral("passIndex"), 0},
                                                 {QStringLiteral("fragmentShader"), QStringLiteral("main.frag")},
                                                 {QStringLiteral("inputs"), inputs},
                                                 {QStringLiteral("output"),
                                                  QJsonObject{{QStringLiteral("type"), QStringLiteral("canvas")}}}}}}}},
        };
        if (!transition)
            pkg.insert(QStringLiteral("backend"), QStringLiteral("gpu"));

        // A small picture for the card: the item's own thumbnail, copied (a few KB).
        const QString thumb = thumbnailFor(itemDir);
        if (!thumb.isEmpty() && QFileInfo(thumb).size() < kMaxSettingsFile
            && QFile::copy(thumb, out.filePath(QStringLiteral("thumbnail.png")))) {
            if (transition) {
                pkg.insert(QStringLiteral("previewStrip"), QStringLiteral("thumbnail.png"));
                pkg.insert(QStringLiteral("previewFrames"), 1);
            } else {
                pkg.insert(QStringLiteral("thumbnail"), QStringLiteral("thumbnail.png"));
            }
        }

        if (!writeBytes(out.filePath(QStringLiteral("main.frag")), shader.source.toUtf8())
            || !writeJson(out.filePath(transition ? QStringLiteral("transition.json") : QStringLiteral("effect.json")), pkg)) {
            out.removeRecursively();
            r->skipReason = QStringLiteral("Could not save the converted copy (is the disk full?)");
            return;
        }
        r->kind = kind;
        r->category = category;
        r->outputs.append(rel);
    }

    void convertLook(const QString &packageName, const QString &cubePath, const QString &name,
                     const QString &category, Record *r)
    {
        const QString rel = kLooks + QLatin1Char('/') + packageName;
        QDir out(QDir(m_outRoot).filePath(rel));
        out.removeRecursively();
        QDir().mkpath(out.absolutePath());
        if (!QFile::copy(cubePath, out.filePath(QStringLiteral("look.cube")))
            || !writeJson(out.filePath(QStringLiteral("look.json")),
                          QJsonObject{{QStringLiteral("name"), name},
                                      {QStringLiteral("category"), category},
                                      {QStringLiteral("description"), QStringLiteral("From a linked folder.")},
                                      {QStringLiteral("order"), 90000}})) {
            out.removeRecursively();
            r->skipReason = QStringLiteral("Its colour table could not be copied");
            return;
        }
        r->kind = QStringLiteral("look");
        r->category = category;
        r->outputs.append(rel);
    }

    void convertElement(const QString &itemDir, const QJsonObject &info, Record *r)
    {
        const QString data = QDir(itemDir).filePath(QStringLiteral("Data"));
        QString media = ownFiles(itemDir, data, kVideoSuffixes, true).value(0);
        if (media.isEmpty())
            media = ownFiles(itemDir, data, kImageSuffixes, true).value(0);
        if (media.isEmpty()) {
            for (const QString &json : ownFiles(itemDir, data, {QStringLiteral("json")}, true)) {
                if (looksLikeLottie(json)) {
                    media = json;
                    break;
                }
            }
        }
        if (media.isEmpty()) {
            r->skipReason = QFileInfo::exists(QDir(data).filePath(QStringLiteral("data.xml")))
                                ? QStringLiteral("Element made with Filmora's own template engine")
                                : QStringLiteral("No video, picture or Lottie in the folder");
            return;
        }
        QString thumbnail = thumbnailFor(itemDir);
        if (thumbnail.isEmpty())
            thumbnail = kImageSuffixes.contains(QFileInfo(media).suffix().toLower()) ? media : QString();

        QString categoryName = categoryNames(info).value(0);
        if (categoryName.isEmpty())
            categoryName = QStringLiteral("Filmora");
        r->kind = QStringLiteral("sticker");
        r->category = categoryName;
        r->sticker = QJsonObject{
            {QStringLiteral("id"), packageNameFor(info, itemDir)},
            {QStringLiteral("label"), r->name},
            {QStringLiteral("category"), slugOf(categoryName)},
            {QStringLiteral("categoryLabel"), categoryName},
            // Absolute: the sticker plays straight from the linked folder, nothing is copied.
            {QStringLiteral("file"), QFileInfo(media).absoluteFilePath()},
            {QStringLiteral("thumbnail"), thumbnail.isEmpty() ? QString() : QFileInfo(thumbnail).absoluteFilePath()},
        };
    }

    // A Filmora title as a Dafin title template: <outRoot>/text-styles/fm-<id>.json.
    void convertTitle(const QString &itemDir, const QJsonObject &info, Record *r)
    {
        const QString name = englishTitle(info, itemDir);
        const bool hasFonts = !r->fonts.isEmpty();
        auto fail = [&](const QString &reason) {
            r->name = name;
            r->skipReason = hasFonts ? reason + QStringLiteral(" (its fonts were added)") : reason;
        };
        const QString dataPath = QDir(itemDir).filePath(QStringLiteral("Data/data.json"));
        const QByteArray data = readSmall(dataPath, 4 * kMaxSettingsFile);
        if (data.isEmpty()) {
            fail(QFileInfo::exists(dataPath) ? QStringLiteral("Filmora title: its file is too large or unreadable")
                                             : QStringLiteral("Filmora title: Data/data.json is missing"));
            return;
        }

        TitleContext context;
        context.effect = m_effectLookup;
        // The title names its font as Filmora shows it ("Paytone One Regular"); the files in its
        // Font/ folder say what family that is installed under.
        const QJsonArray fonts = r->fonts;
        context.font = [fonts](const QString &fontName) -> std::optional<TitleFont> {
            const QString wanted = fontName.simplified();
            std::optional<TitleFont> prefix;
            for (const QJsonValue &v : fonts) {
                const QJsonObject f = v.toObject();
                const QString family = f.value(QStringLiteral("family")).toString();
                const QString legacy = f.value(QStringLiteral("legacyFamily")).toString();
                const QString style = f.value(QStringLiteral("style")).toString();
                const TitleFont face{family, f.value(QStringLiteral("weight")).toInt(400),
                                     f.value(QStringLiteral("italic")).toBool()};
                for (const QString &candidate : {family + QLatin1Char(' ') + style, legacy + QLatin1Char(' ') + style,
                                                 family, legacy}) {
                    if (!candidate.trimmed().isEmpty()
                        && candidate.simplified().compare(wanted, Qt::CaseInsensitive) == 0)
                        return face;
                }
                if (!prefix && !family.isEmpty() && wanted.startsWith(family, Qt::CaseInsensitive))
                    prefix = TitleFont{family, splitFontName(wanted).weight, splitFontName(wanted).italic};
            }
            return prefix;
        };

        const QString packageName = packageNameFor(info, itemDir);
        const TitleConversion title = convertFilmoraTitle(
            data, QStringLiteral("filmora.") + packageName.mid(3), name, categoryNames(info).value(0), context);
        if (!title.ok) {
            fail(QStringLiteral("Filmora title: ") + title.error);
            return;
        }
        const QString rel = kTitles + QLatin1Char('/') + packageName + QStringLiteral(".json");
        if (!writeJson(QDir(m_outRoot).filePath(rel), title.templateJson)) {
            fail(QStringLiteral("Could not save the converted copy (is the disk full?)"));
            return;
        }
        r->kind = QStringLiteral("title");
        r->name = name;
        r->category = title.templateJson.value(QStringLiteral("category")).toString();
        r->outputs.append(rel);
        r->skipReason = title.leftOut.isEmpty()
                            ? QString()
                            : QStringLiteral("Title added without %1").arg(title.leftOut.join(QStringLiteral(", ")));
    }

public:
    void setEffectLookup(const std::function<std::optional<LinkedEffectInfo>(qint64)> &lookup)
    {
        m_effectLookup = lookup;
    }

private:
    // Fonts are only read here; the families are written once for the whole folder.
    void addFonts(const QStringList &files, Record *r)
    {
        for (const QString &file : files) {
            const FontInfo info = readFontInfo(file);
            if (!info.ok)
                continue;
            r->fonts.append(fontToJson(QFileInfo(file).absoluteFilePath(), info));
            if (r->kind.isEmpty()) {
                r->kind = QStringLiteral("font");
                r->category = info.category;
                r->name = info.family;
            }
        }
    }

    static QStringList categoryNames(const QJsonObject &info)
    {
        QStringList names;
        for (const QJsonValue &value : info.value(QStringLiteral("categories")).toArray()) {
            const QString name = value.toObject().value(QStringLiteral("name")).toString().trimmed();
            if (!name.isEmpty() && !names.contains(name))
                names.append(name);
        }
        return names;
    }

    // Files of `itemDir`, without those of other items that ended up inside it.
    QStringList ownFiles(const QString &itemDir, const QString &dir, const QStringList &suffixes,
                         bool recursive) const
    {
        QStringList out;
        const QString own = QDir::cleanPath(itemDir);
        for (const QString &path : filesWithSuffix(dir, suffixes, recursive)) {
            bool foreign = false;
            for (QString parent = QFileInfo(path).absolutePath(); parent.size() > own.size();
                 parent = QFileInfo(parent).absolutePath()) {
                if (m_itemDirs.contains(parent)) {
                    foreign = true;
                    break;
                }
            }
            if (!foreign)
                out.append(path);
        }
        return out;
    }

    QString m_outRoot;
    const ShaderCheck &m_check;
    QString m_crashedKey;
    const QSet<QString> &m_itemDirs;
    const QHash<QString, QStringList> &m_variantData; // item -> its variant folders' Data/
    std::function<std::optional<LinkedEffectInfo>(qint64)> m_effectLookup; // for titles' filters
};

// One family.json per font family found anywhere in the folder, each face once.
struct FontFamilies
{
    struct Face
    {
        QString file;
        QString style;
        int weight = 400;
        bool italic = false;
    };
    struct Family
    {
        QString name;
        QString category;
        QList<Face> faces;
    };
    QMap<QString, Family> families; // by lower-case name
    int duplicateFaces = 0;

    bool hasFace(const QString &family, int weight, bool italic, QString *style = nullptr) const
    {
        const auto it = families.constFind(family.toLower());
        if (it == families.cend())
            return false;
        for (const Face &face : it->faces) {
            if (face.weight == weight && face.italic == italic) {
                if (style)
                    *style = face.style;
                return true;
            }
        }
        return false;
    }

    void add(const QJsonObject &font)
    {
        QString family = font.value(QStringLiteral("family")).toString();
        const QString legacy = font.value(QStringLiteral("legacyFamily")).toString();
        const QString style = font.value(QStringLiteral("style")).toString();
        const int weight = font.value(QStringLiteral("weight")).toInt(400);
        const bool italic = font.value(QStringLiteral("italic")).toBool();
        QString existingStyle;
        if (hasFace(family, weight, italic, &existingStyle)) {
            // Another width of the family at the same weight ("Condensed Bold" beside "Bold"):
            // under its own older family name, so neither hides the other. The same face again
            // (a copied file) is simply used once.
            if (existingStyle.compare(style, Qt::CaseInsensitive) != 0 && !legacy.isEmpty()
                && legacy.compare(family, Qt::CaseInsensitive) != 0 && !hasFace(legacy, weight, italic)) {
                family = legacy;
            } else {
                ++duplicateFaces;
                return;
            }
        }
        Family &entry = families[family.toLower()];
        if (entry.name.isEmpty()) {
            entry.name = family;
            entry.category = font.value(QStringLiteral("category")).toString();
        }
        entry.faces.append({font.value(QStringLiteral("file")).toString(), style, weight, italic});
    }
};

} // namespace

int ImportReport::skipped() const
{
    int n = 0;
    for (const ImportedItem &item : items) {
        if (item.kind.isEmpty())
            ++n;
    }
    return n;
}

ShaderTranslation translateFilmoraShader(const QByteArray &fragFile, const QJsonObject &conf,
                                         bool transition)
{
    ShaderTranslation out;

    QString body;
    const bool encoded = conf.value(QStringLiteral("ENCRYPT")).toVariant().toString() == QLatin1String("1");
    if (encoded) {
        QByteArray compact = fragFile;
        compact.replace('\r', QByteArray()).replace('\n', QByteArray()).replace(' ', QByteArray());
        const auto decoded = QByteArray::fromBase64Encoding(
            compact, QByteArray::Base64Encoding | QByteArray::AbortOnBase64DecodingErrors);
        if (decoded)
            body = QString::fromUtf8(*decoded);
    } else {
        body = QString::fromUtf8(fragFile);
    }
    if (!body.contains(QLatin1String("FUNCNAME"))) {
        out.error = QStringLiteral("Its shader is locked or in a form Dafin can't read");
        return out;
    }
    body = withoutComments(body);

    // The parameters, by Filmora name, and how each reads in GLSL.
    QHash<QString, QString> prefixMap;
    QStringList uniforms;
    static const QRegularExpression kIdentifier(QStringLiteral("^[A-Za-z_][A-Za-z0-9_]*$"));
    for (const QJsonValue &value : conf.value(QStringLiteral("PARAMS")).toArray()) {
        const QJsonObject p = value.toObject();
        const QString name = p.value(QStringLiteral("NAME")).toString();
        if (name.isEmpty() || prefixMap.contains(name))
            continue; // the same parameter twice is declared once
        if (!kIdentifier.match(name).hasMatch()) {
            out.error = QStringLiteral("A parameter has a name Dafin can't use (%1)").arg(name.left(40));
            return out;
        }
        const QString type = p.value(QStringLiteral("TYPE")).toString().toLower();
        const QString uniform = QStringLiteral("fm_") + name;
        uniforms.append(QStringLiteral("uniform float %1;").arg(uniform));
        // Dafin binds every number as a float; Filmora's ints and bools are read back as such.
        if (type == QLatin1String("int"))
            prefixMap.insert(name, QStringLiteral("int(floor(%1 + 0.5))").arg(uniform));
        else if (type == QLatin1String("bool"))
            prefixMap.insert(name, QStringLiteral("(%1 > 0.5)").arg(uniform));
        else
            prefixMap.insert(name, uniform);
    }
    // Values Filmora's renderer fills in itself, where Dafin has the same thing.
    prefixMap.insert(QStringLiteral("global_time"), QStringLiteral("u_time"));
    prefixMap.insert(QStringLiteral("iFrame"), QStringLiteral("u_frameIndex"));
    prefixMap.insert(QStringLiteral("iResolution"), QStringLiteral("u_resolution"));
    if (transition)
        prefixMap.insert(QStringLiteral("progress"), QStringLiteral("u_progress"));

    static const QRegularExpression kPrefix(
        QStringLiteral("PREFIX\\s*\\(\\s*([A-Za-z_][A-Za-z0-9_]*)\\s*\\)"));
    QStringList unknown;
    QString translated;
    int last = 0;
    for (auto it = kPrefix.globalMatch(body); it.hasNext();) {
        const QRegularExpressionMatch m = it.next();
        const QString name = m.captured(1);
        translated += body.mid(last, m.capturedStart() - last);
        last = m.capturedEnd();
        const auto found = prefixMap.constFind(name);
        if (found == prefixMap.cend()) {
            if (!unknown.contains(name))
                unknown.append(name);
            translated += name;
        } else {
            translated += QLatin1Char('(') + found.value() + QLatin1Char(')');
        }
    }
    translated += body.mid(last);
    if (!unknown.isEmpty()) {
        out.error = QStringLiteral("Needs values only Filmora's renderer provides (%1)")
                        .arg(unknown.mid(0, 3).join(QStringLiteral(", ")));
        return out;
    }

    // Samplers: a filter reads its clip as INPUT / INPUT1, a transition the two clips as
    // INPUT1 / INPUT2. Anything past that is one of Filmora's own extra render passes.
    static const QRegularExpression kInput(QStringLiteral("\\bINPUT(\\d*)\\s*\\("));
    for (auto it = kInput.globalMatch(translated); it.hasNext();) {
        const QString n = it.next().captured(1);
        const bool ok = transition ? (n == QLatin1String("1") || n == QLatin1String("2"))
                                   : (n.isEmpty() || n == QLatin1String("1"));
        if (!ok) {
            out.error = QStringLiteral("Uses Filmora's extra render passes (INPUT%1)").arg(n);
            return out;
        }
    }

    QString header = QStringLiteral(
        "#version 330 core\n"
        "// Converted by Dafin from a Filmora shader in a linked folder.\n"
        "in vec2 v_texCoord;\n"
        "out vec4 fragColor;\n"
        "uniform vec2 u_resolution;\n"
        "uniform float u_time;\n"
        "uniform int u_frameIndex;\n"
        "uniform float u_progress;\n");
    if (transition) {
        header += QStringLiteral(
            "uniform sampler2D u_fromTexture;\n"
            "uniform sampler2D u_toTexture;\n"
            "#define INPUT1(uv) texture(u_fromTexture, (uv))\n"
            "#define INPUT2(uv) texture(u_toTexture, (uv))\n");
    } else {
        header += QStringLiteral(
            "uniform sampler2D u_currentTexture;\n"
            "#define INPUT(uv) texture(u_currentTexture, (uv))\n"
            "#define INPUT1(uv) texture(u_currentTexture, (uv))\n");
    }
    header += uniforms.join(QLatin1Char('\n')) + QLatin1Char('\n');
    header += QStringLiteral(
        "#define iResolution u_resolution\n"
        "#define iResolutionOrig u_resolution\n"
        "#define FUNCNAME fm_effect\n");

    out.source = header + translated
                 + QStringLiteral("\nvoid main() { fragColor = fm_effect(v_texCoord); }\n");
    out.ok = true;
    return out;
}

QString smartCategory(const QString &packageKind, const CategoryHints &hints)
{
    const bool isTransition = packageKind == QLatin1String("transitions");
    const QList<CategoryRule> &rules = isTransition ? transitionRules() : effectRules();

    // Strongest hints first: Filmora's own category name, its slug, the title, the folders, then
    // the code (only words that really say something there).
    const QStringList categoryWords = wordsOf(hints.filmoraCategories.join(QLatin1Char(' ')));
    const QStringList slugWords = wordsOf(hints.categorySlugs.join(QLatin1Char(' ')));
    const QStringList titleWords = wordsOf(hints.title);
    const QStringList folderWords = wordsOf(hints.folderNames.join(QLatin1Char(' ')));
    const QStringList codeWords = wordsOf(hints.paramNames.join(QLatin1Char(' ')) + QLatin1Char(' ') + hints.shader);

    QString best;
    int bestScore = 0;
    for (const CategoryRule &rule : rules) {
        int score = 0;
        score += keywordHits(categoryWords, rule.words) > 0 ? 6 : 0;
        score += keywordHits(slugWords, rule.words) > 0 ? 4 : 0;
        score += keywordHits(titleWords, rule.words) > 0 ? 3 : 0;
        score += keywordHits(folderWords, rule.words) > 0 ? 2 : 0;
        score += qMin(2, keywordHits(codeWords, rule.strong));
        if (score > bestScore) { // ties keep the earlier, more specific rule
            bestScore = score;
            best = rule.id;
        }
    }
    // A shader word alone is not enough; a title or a category is.
    if (bestScore >= 3)
        return best;
    for (const QString &name : hints.filmoraCategories) {
        if (!name.trimmed().isEmpty())
            return name.trimmed();
    }
    return QStringLiteral("filmora");
}

QString dafinCategoryFor(const QString &packageKind, const QString &filmoraCategory,
                         const QString &title)
{
    CategoryHints hints;
    if (!filmoraCategory.isEmpty())
        hints.filmoraCategories.append(filmoraCategory);
    hints.title = title;
    return smartCategory(packageKind, hints);
}

FontInfo readFontInfo(const QString &path)
{
    FontInfo info;
    QFile file(path);
    if (file.size() < 12 || file.size() > kMaxFontFile || !file.open(QIODevice::ReadOnly))
        return info;
    const QByteArray data = file.readAll();
    const FontBytes b(data);

    qint64 base = 0;
    if (b.u32(0) == tag('t', 't', 'c', 'f'))
        base = b.u32(12); // a collection: the first font speaks for the file
    const quint32 version = b.u32(base);
    if (version != 0x00010000 && version != tag('O', 'T', 'T', 'O') && version != tag('t', 'r', 'u', 'e'))
        return info;

    qint64 nameOff = -1, os2Off = -1, postOff = -1;
    qint64 nameLen = 0, os2Len = 0, postLen = 0;
    const int numTables = int(b.u16(base + 4));
    for (int i = 0; i < numTables && i < 256; ++i) {
        const qint64 rec = base + 12 + 16 * i;
        if (!b.has(rec, 16))
            break;
        const quint32 t = b.u32(rec);
        const qint64 off = b.u32(rec + 8);
        const qint64 len = b.u32(rec + 12);
        if (!b.has(off, len))
            continue; // a damaged table is skipped, never read past the end
        if (t == tag('n', 'a', 'm', 'e')) { nameOff = off; nameLen = len; }
        else if (t == tag('O', 'S', '/', '2')) { os2Off = off; os2Len = len; }
        else if (t == tag('p', 'o', 's', 't')) { postOff = off; postLen = len; }
    }

    // Names: Windows English first, then any Unicode, then Mac Roman.
    QHash<int, QString> names;
    QHash<int, int> rank;
    if (nameOff >= 0 && nameLen >= 6) {
        const int count = int(b.u16(nameOff + 2));
        const qint64 strings = nameOff + b.u16(nameOff + 4);
        for (int i = 0; i < count && i < 4096; ++i) {
            const qint64 rec = nameOff + 6 + 12 * i;
            if (!b.has(rec, 12))
                break;
            const int platform = int(b.u16(rec));
            const int language = int(b.u16(rec + 4));
            const int nameId = int(b.u16(rec + 6));
            if (nameId != 1 && nameId != 2 && nameId != 16 && nameId != 17)
                continue;
            int r = 0;
            if (platform == 3)
                r = language == 0x0409 ? 4 : 3;
            else if (platform == 0)
                r = 2;
            else if (platform == 1 && language == 0)
                r = 1;
            if (r == 0 || rank.value(nameId) >= r)
                continue;
            const QByteArray raw = b.bytes(strings + b.u16(rec + 10), b.u16(rec + 8));
            QString text;
            if (platform == 1) {
                text = QString::fromLatin1(raw);
            } else {
                for (int k = 0; k + 1 < raw.size(); k += 2)
                    text += QChar(char16_t((quint8(raw.at(k)) << 8) | quint8(raw.at(k + 1))));
            }
            text = text.simplified();
            if (text.isEmpty())
                continue;
            names.insert(nameId, text);
            rank.insert(nameId, r);
        }
    }
    info.legacyFamily = names.value(1);
    info.family = names.value(16, info.legacyFamily);
    info.style = names.value(17, names.value(2));
    if (info.family.isEmpty())
        return info;

    int familyClass = 0, panoseFamily = 0, panoseSerif = 0, widthClass = 5;
    bool italic = false;
    int weight = 0;
    if (os2Off >= 0 && os2Len >= 64) {
        weight = int(b.u16(os2Off + 4));
        widthClass = int(b.u16(os2Off + 6));
        familyClass = int(b.u16(os2Off + 30) >> 8);
        panoseFamily = int(b.u8(os2Off + 32));
        panoseSerif = int(b.u8(os2Off + 33));
        italic = (b.u16(os2Off + 62) & 0x1) != 0;
    }
    const bool monospace = postOff >= 0 && postLen >= 16 && b.u32(postOff + 12) != 0;
    const QString styleWords = info.style.toLower();
    if (styleWords.contains(QLatin1String("italic")) || styleWords.contains(QLatin1String("oblique")))
        italic = true;
    if (weight > 0 && weight < 10)
        weight *= 100; // some fonts use 1..9
    if (weight <= 0) {
        static const QList<QPair<QString, int>> kWeights = {
            {QStringLiteral("extralight"), 200}, {QStringLiteral("semibold"), 600},
            {QStringLiteral("extrabold"), 800},  {QStringLiteral("thin"), 100},
            {QStringLiteral("light"), 300},      {QStringLiteral("medium"), 500},
            {QStringLiteral("bold"), 700},       {QStringLiteral("black"), 900},
        };
        weight = 400;
        const QString compact = QString(styleWords).remove(QLatin1Char(' '));
        for (const auto &w : kWeights) {
            if (compact.contains(w.first)) {
                weight = w.second;
                break;
            }
        }
    }
    info.weight = qBound(100, ((weight + 50) / 100) * 100, 900);
    info.italic = italic;
    info.category = fontCategoryFor(info, familyClass, panoseFamily, panoseSerif,
                                    widthClass < 1 ? 5 : widthClass, monospace);
    info.ok = true;
    return info;
}

QString linkedLibrariesRoot()
{
    return QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation))
        .filePath(QStringLiteral("linked-libraries"));
}

QString outputDirFor(const QString &sourceFolder)
{
    QString key = QDir::cleanPath(QFileInfo(sourceFolder).absoluteFilePath());
#ifdef Q_OS_WIN
    key = key.toLower();
#endif
    return QDir(linkedLibrariesRoot()).filePath(hashOf(key, 12));
}

ImportReport importFilmoraFolder(const QString &sourceRoot, const QString &outRoot,
                                 const ShaderCheck &check, const std::atomic_bool *cancel)
{
    ImportReport report;
    const QDir source(sourceRoot);
    if (sourceRoot.isEmpty() || !QFileInfo(sourceRoot).isDir()) {
        report.error = QStringLiteral("The folder is missing (is its drive connected?)");
        return report;
    }
    QDir().mkpath(outRoot);
    const QString indexPath = QDir(outRoot).filePath(QStringLiteral("library.json"));
    const QJsonObject oldIndex = readObject(indexPath); // a damaged index just means a full rescan
    const QJsonObject oldItems = oldIndex.value(QStringLiteral("items")).toObject();
    const QString packId = QStringLiteral("linked-") + QDir(outRoot).dirName();
    const QString libraryName = source.dirName().isEmpty() ? QStringLiteral("Linked") : source.dirName();

    // The item whose shader check took Dafin down last time, if any.
    const QString markerPath = Converter::markerPath(outRoot);
    const QString crashedKey = QString::fromUtf8(readSmall(markerPath, 4096)).trimmed();
    QFile::remove(markerPath);

    // ── 1. One walk: Filmora items (folders with resinfo.json, or Data/filter.xml) and the files
    //       outside them.
    QStringList allFiles;
    QSet<QString> resinfoDirs;    // folders with a resinfo.json: Filmora items
    QSet<QString> shaderOnlyDirs; // folders with Data/filter.xml (and maybe no resinfo.json)
    bool capped = false;
    {
        QDirIterator it(source.absolutePath(), QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            if (allFiles.size() >= kMaxFiles) {
                capped = true;
                break;
            }
            const QString path = it.next();
            allFiles.append(path);
            const QFileInfo fi(path);
            if (fi.fileName() == QLatin1String("resinfo.json")) {
                resinfoDirs.insert(fi.absolutePath());
            } else if (fi.fileName().compare(QLatin1String("filter.xml"), Qt::CaseInsensitive) == 0
                       && fi.absoluteDir().dirName().compare(QLatin1String("Data"), Qt::CaseInsensitive) == 0) {
                shaderOnlyDirs.insert(QFileInfo(fi.absolutePath()).absolutePath());
            }
        }
    }
    if (capped)
        report.note = QStringLiteral("Only the first %1 files were read; link a smaller folder").arg(kMaxFiles);

    // A folder with Data/filter.xml but no resinfo.json inside a Filmora item (its "All", "9" ...
    // folders) is a copy of that item's own files, not an item of its own: it used to appear as
    // an extra effect named "All" with no picture. Its Data/ is kept for the item, which uses it
    // when its own Data/ holds no shader. Elsewhere such a folder is still an item by itself.
    const QString rootPath = source.absolutePath();
    QSet<QString> itemDirSet = resinfoDirs;
    QHash<QString, QStringList> variantData;
    for (const QString &dir : std::as_const(shaderOnlyDirs)) {
        if (resinfoDirs.contains(dir))
            continue;
        QString owner;
        for (QString parent = QFileInfo(dir).absolutePath(); parent.size() >= rootPath.size();
             parent = QFileInfo(parent).absolutePath()) {
            if (resinfoDirs.contains(parent)) {
                owner = parent;
                break;
            }
            if (parent == rootPath)
                break;
        }
        if (owner.isEmpty())
            itemDirSet.insert(dir);
        else
            variantData[owner].append(QDir(dir).filePath(QStringLiteral("Data")));
    }
    // "All" first (Filmora's full set), then by name, so the choice is the same on every scan.
    for (QStringList &dataDirs : variantData) {
        std::sort(dataDirs.begin(), dataDirs.end(), [](const QString &a, const QString &b) {
            const bool aAll = QFileInfo(QFileInfo(a).absolutePath()).fileName().compare(
                                  QLatin1String("All"), Qt::CaseInsensitive) == 0;
            const bool bAll = QFileInfo(QFileInfo(b).absolutePath()).fileName().compare(
                                  QLatin1String("All"), Qt::CaseInsensitive) == 0;
            return aAll != bAll ? aAll : a < b;
        });
    }

    // Every item is its own, even one that ended up inside another item's folder: each is
    // sorted by its own resinfo.json and content, never by where it sits.
    QStringList itemDirs(itemDirSet.cbegin(), itemDirSet.cend());
    itemDirs.sort();

    QStringList looseFiles;
    for (const QString &path : std::as_const(allFiles)) {
        bool inItem = false;
        for (QString parent = QFileInfo(path).absolutePath();
             parent.size() >= source.absolutePath().size(); parent = QFileInfo(parent).absolutePath()) {
            if (itemDirSet.contains(parent)) {
                inItem = true;
                break;
            }
            if (parent == source.absolutePath())
                break;
        }
        if (inItem)
            continue;
        const QString suffix = QFileInfo(path).suffix().toLower();
        if (kFontSuffixes.contains(suffix) || suffix == QLatin1String("cube")
            || kVideoSuffixes.contains(suffix) || kLooseImageSuffixes.contains(suffix)
            || (suffix == QLatin1String("json") && looksLikeLottie(path)))
            looseFiles.append(path);
    }
    looseFiles.sort();

    // Failsafe: an empty folder that had items before is far more likely a drive that is not
    // connected (or a mount point with nothing behind it) than a library deleted on purpose.
    if (itemDirs.isEmpty() && looseFiles.isEmpty() && !oldItems.isEmpty()) {
        report.error = QStringLiteral("The folder is empty right now (is its drive connected?). "
                                      "What was read from it before is kept.");
        return report;
    }

    // ── 2. Convert, keeping what is unchanged.
    QJsonObject newItems;
    QSet<QString> changed;
    QSet<QString> claimed;              // package folders written or kept in this run
    QHash<QString, QString> identities; // identity -> the first item that had it
    Converter converter(outRoot, check, crashedKey, itemDirSet, variantData);

    // `stampExtra`: what else the conversion depends on (for a title, the effects it can use).
    auto process = [&](const QString &key, const QString &path, const QString &name,
                       const std::function<Record()> &convert, const QString &identity,
                       const QString &stampExtra = QString()) {
        const QString stamp = stampExtra.isEmpty()
                                  ? stampFor(path)
                                  : hashOf(stampFor(path) + QLatin1Char('|') + stampExtra, 20);
        Record record = Record::fromJson(oldItems.value(key).toObject());

        if (!identity.isEmpty() && identities.contains(identity)) {
            // Failsafe: the same item twice. Only the first is used; this copy is listed.
            for (const QString &rel : std::as_const(record.outputs)) {
                if (!claimed.contains(rel)) {
                    removeOutput(outRoot, rel);
                    changed.insert(rel.section(QLatin1Char('/'), 0, 0));
                }
            }
            Record dup;
            dup.stamp = stamp;
            dup.name = name;
            dup.skipReason = QStringLiteral("Duplicate of %1, which is used instead").arg(identities.value(identity));
            return dup;
        }
        if (!identity.isEmpty())
            identities.insert(identity, key);

        bool reuse = record.stamp == stamp;
        for (const QString &rel : std::as_const(record.outputs)) {
            if (!QFileInfo::exists(QDir(outRoot).filePath(rel)) || claimed.contains(rel))
                reuse = false;
        }
        if (!reuse) {
            for (const QString &rel : std::as_const(record.outputs)) {
                if (claimed.contains(rel))
                    continue;
                removeOutput(outRoot, rel);
                changed.insert(rel.section(QLatin1Char('/'), 0, 0));
            }
            try {
                record = convert();
            } catch (const std::exception &e) {
                record = Record();
                record.name = humanName(QFileInfo(path).fileName());
                record.skipReason = QStringLiteral("Could not be read (%1)").arg(QString::fromUtf8(e.what()).left(80));
            } catch (...) {
                record = Record();
                record.name = humanName(QFileInfo(path).fileName());
                record.skipReason = QStringLiteral("Could not be read (damaged files?)");
            }
            record.stamp = stamp;
            // Fonts need nothing here: the families are compared and written in one go below.
            for (const QString &rel : std::as_const(record.outputs))
                changed.insert(rel.section(QLatin1Char('/'), 0, 0));
        }
        for (const QString &rel : std::as_const(record.outputs))
            claimed.insert(rel);
        return record;
    };

    auto tally = [&](const Record &record, const QString &source) {
        report.items.append({record.kind, record.name, record.category, source, record.skipReason});
        if (record.kind == QLatin1String("effect"))
            ++report.effects;
        else if (record.kind == QLatin1String("transition"))
            ++report.transitions;
        else if (record.kind == QLatin1String("sticker"))
            ++report.stickers;
        else if (record.kind == QLatin1String("look"))
            ++report.looks;
        else if (record.kind == QLatin1String("title"))
            ++report.titles;
    };

    // Filmora filters converted to Dafin effects, by Filmora id, so a title that uses one gets it
    // as a clip effect: those of this folder, then those of the other linked folders.
    QHash<qint64, QString> effectDirs; // Filmora id -> package folder
    QStringList siblingEffectDirs;     // <linked-libraries>/<other>/effects/fm-<id>
    if (QFileInfo(outRoot).absoluteDir().dirName() == QLatin1String("linked-libraries")) {
        const QDir linkedRoot = QFileInfo(outRoot).absoluteDir();
        for (const QString &library : linkedRoot.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
            if (library == QDir(outRoot).dirName())
                continue;
            const QDir effectsDir(linkedRoot.filePath(library + QLatin1Char('/') + kEffects));
            for (const QString &pkg : effectsDir.entryList({QStringLiteral("fm-*")}, QDir::Dirs, QDir::Name))
                siblingEffectDirs.append(effectsDir.filePath(pkg));
        }
    }
    QHash<qint64, std::optional<LinkedEffectInfo>> effectCache;
    converter.setEffectLookup([&](qint64 filmoraId) -> std::optional<LinkedEffectInfo> {
        const auto cached = effectCache.constFind(filmoraId);
        if (cached != effectCache.cend())
            return *cached;
        QString dir = effectDirs.value(filmoraId);
        if (dir.isEmpty()) {
            const QString wanted = QStringLiteral("/fm-%1").arg(filmoraId);
            for (const QString &sibling : std::as_const(siblingEffectDirs)) {
                if (sibling.endsWith(wanted)) {
                    dir = sibling;
                    break;
                }
            }
        }
        std::optional<LinkedEffectInfo> found;
        const QJsonObject pkg = dir.isEmpty() ? QJsonObject() : readObject(QDir(dir).filePath(QStringLiteral("effect.json")));
        if (!pkg.value(QStringLiteral("id")).toString().isEmpty()) {
            LinkedEffectInfo info;
            info.dafinId = pkg.value(QStringLiteral("id")).toString();
            for (const QJsonValue &v : pkg.value(QStringLiteral("parameters")).toArray()) {
                const QJsonObject p = v.toObject();
                info.params.append({p.value(QStringLiteral("identifier")).toString(),
                                    p.value(QStringLiteral("type")).toString(),
                                    p.value(QStringLiteral("minValue")).toDouble(0.0),
                                    p.value(QStringLiteral("maxValue")).toDouble(1.0)});
            }
            found = info;
        }
        effectCache.insert(filmoraId, found);
        return found;
    });

    // Two passes: titles last, so the effects they use are already converted.
    QString titleStampExtra;
    for (int pass = 0; pass < 2; ++pass) {
        if (pass == 1) {
            QStringList available;
            for (auto it = effectDirs.cbegin(); it != effectDirs.cend(); ++it)
                available.append(QString::number(it.key()));
            for (const QString &sibling : std::as_const(siblingEffectDirs))
                available.append(QFileInfo(sibling).fileName());
            available.sort();
            titleStampExtra = QStringLiteral("fx:") + hashOf(available.join(QLatin1Char(',')), 16);
        }
        for (const QString &itemDir : std::as_const(itemDirs)) {
            if (cancel && cancel->load()) {
                report.cancelled = true;
                return report; // the old index stays, so the next run picks up where this one stopped
            }
            const QString key = source.relativeFilePath(itemDir);
            QJsonObject info = readObject(QDir(itemDir).filePath(QStringLiteral("resinfo.json")));
            // Read only when needed: most items say what they are, and rescans must stay quick.
            int sniffed = -1;
            auto sniff = [&] {
                if (sniffed < 0)
                    sniffed = typeFromContent(itemDir, converter.dataDirFor(itemDir));
                return sniffed;
            };
            int type = info.value(QStringLiteral("type")).toInt();
            if (type <= 0) // no resinfo.json, or a damaged one: what is inside, then where it sits
                type = sniff() != TypeUnknown ? sniff() : typeFromFolders(itemDir);
            if ((type == TypeTitle) != (pass == 1))
                continue;
            const QString identity = identityFor(info, type, itemDir);
            const Record record = process(
                key, itemDir, englishTitle(info, itemDir),
                [&] {
                    Record r = converter.convertItem(key, itemDir, info, type);
                    // Filed under the wrong type (a sticker labelled as a filter, say): what is
                    // inside decides.
                    if (r.kind.isEmpty() && sniff() != type && isConvertibleType(sniff())) {
                        Record byContent = converter.convertItem(key, itemDir, info, sniff());
                        if (!byContent.kind.isEmpty())
                            return byContent;
                    }
                    return r;
                },
                identity, pass == 1 ? titleStampExtra : QString());
            newItems.insert(key, record.toJson());
            tally(record, itemDir);
            const qint64 filmoraId = info.value(QStringLiteral("id")).toVariant().toLongLong();
            if (record.kind == QLatin1String("effect") && filmoraId > 0 && !record.outputs.isEmpty())
                effectDirs.insert(filmoraId, QDir(outRoot).filePath(record.outputs.first()));
        }
    }

    for (const QString &path : std::as_const(looseFiles)) {
        if (cancel && cancel->load()) {
            report.cancelled = true;
            return report;
        }
        const QString key = QStringLiteral("file:") + source.relativeFilePath(path);
        const bool font = kFontSuffixes.contains(QFileInfo(path).suffix().toLower());
        // Fonts are de-duplicated per face when the families are built; other files by content.
        const QString identity = font ? QString() : QStringLiteral("file:") + contentKey(path);
        const Record record = process(
            key, path, humanName(QFileInfo(path).completeBaseName()),
            [&] { return converter.convertLooseFile(path, libraryName); }, identity);
        newItems.insert(key, record.toJson());
        tally(record, path);
    }

    // ── 3. Items that are gone take their packages with them (not when the walk was capped:
    //       what was not reached is not gone).
    if (!capped) {
        for (auto old = oldItems.constBegin(); old != oldItems.constEnd(); ++old) {
            if (newItems.contains(old.key()))
                continue;
            const Record gone = Record::fromJson(old.value().toObject());
            for (const QString &rel : gone.outputs) {
                if (claimed.contains(rel))
                    continue;
                removeOutput(outRoot, rel);
                changed.insert(rel.section(QLatin1Char('/'), 0, 0));
            }
        }
    }

    // ── 4. Fonts: one family per name, each face once, written only when it would read
    //       differently.
    FontFamilies families;
    for (auto item = newItems.constBegin(); item != newItems.constEnd(); ++item) {
        for (const QJsonValue &font : item.value().toObject().value(QStringLiteral("fonts")).toArray())
            families.add(font.toObject());
    }
    {
        const QDir fontsRoot(QDir(outRoot).filePath(kFonts));
        QSet<QString> wanted;
        for (const FontFamilies::Family &family : std::as_const(families.families)) {
            const QString dirName = slugOf(family.name) + QLatin1Char('-') + hashOf(family.name.toLower(), 6);
            wanted.insert(dirName);
            QJsonArray faces;
            for (const FontFamilies::Face &face : family.faces) {
                QJsonObject f{{QStringLiteral("file"), face.file},
                              {QStringLiteral("weight"), face.weight},
                              {QStringLiteral("styleName"), face.style}};
                if (face.italic)
                    f.insert(QStringLiteral("italic"), true);
                faces.append(f);
            }
            const QByteArray json = QJsonDocument(QJsonObject{
                {QStringLiteral("id"), QStringLiteral("linked-") + dirName},
                {QStringLiteral("family"), family.name},
                {QStringLiteral("category"), family.category},
                {QStringLiteral("license"), QStringLiteral("See the font's own licence")},
                {QStringLiteral("order"), 90000},
                {QStringLiteral("faces"), faces}}).toJson(QJsonDocument::Indented);
            const QString path = fontsRoot.filePath(dirName + QStringLiteral("/family.json"));
            if (readSmall(path) != json) {
                writeBytes(path, json);
                changed.insert(kFonts);
            }
        }
        for (const QString &dir : fontsRoot.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
            if (!wanted.contains(dir)) {
                QDir(fontsRoot.filePath(dir)).removeRecursively();
                changed.insert(kFonts);
            }
        }
        report.fonts = int(families.families.size());
    }

    // ── 5. One sticker pack for the whole folder, rewritten only when it would read differently.
    const QString packDir = QDir(outRoot).filePath(kStickers + QLatin1Char('/') + packId);
    const QString packPath = QDir(packDir).filePath(QStringLiteral("pack.json"));
    QJsonArray stickers;
    QJsonArray categories;
    QSet<QString> seenCategories;
    QSet<QString> seenStickerIds;
    for (auto item = newItems.constBegin(); item != newItems.constEnd(); ++item) {
        const QJsonObject sticker = item.value().toObject().value(QStringLiteral("sticker")).toObject();
        if (sticker.isEmpty() || seenStickerIds.contains(sticker.value(QStringLiteral("id")).toString()))
            continue;
        seenStickerIds.insert(sticker.value(QStringLiteral("id")).toString());
        const QString category = sticker.value(QStringLiteral("category")).toString();
        if (!seenCategories.contains(category)) {
            seenCategories.insert(category);
            categories.append(QJsonObject{
                {QStringLiteral("id"), category},
                {QStringLiteral("label"), sticker.value(QStringLiteral("categoryLabel")).toString()}});
        }
        QJsonObject entry = sticker;
        entry.remove(QStringLiteral("categoryLabel"));
        stickers.append(entry);
    }
    const QByteArray pack = stickers.isEmpty()
        ? QByteArray()
        : QJsonDocument(QJsonObject{{QStringLiteral("id"), packId},
                                    {QStringLiteral("name"), libraryName},
                                    {QStringLiteral("license"), QStringLiteral("From a linked folder")},
                                    {QStringLiteral("order"), 900},
                                    {QStringLiteral("categories"), categories},
                                    {QStringLiteral("stickers"), stickers}})
              .toJson(QJsonDocument::Indented);
    if (pack != readSmall(packPath)) {
        QDir(packDir).removeRecursively();
        if (!pack.isEmpty())
            writeBytes(packPath, pack);
        changed.insert(kStickers);
    } else {
        changed.remove(kStickers);
    }

    writeJson(indexPath, QJsonObject{{QStringLiteral("version"), kImporterVersion},
                                     {QStringLiteral("source"), source.absolutePath()},
                                     {QStringLiteral("items"), newItems}});

    for (const QString &kind : {kEffects, kTransitions, kStickers, kFonts, kLooks, kTitles}) {
        if (changed.contains(kind))
            report.changedKinds.append(kind);
    }
    return report;
}

} // namespace dafin::linked
