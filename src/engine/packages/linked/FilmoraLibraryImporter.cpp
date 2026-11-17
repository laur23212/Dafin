#include "engine/packages/linked/FilmoraLibraryImporter.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>

namespace dafin::linked {

namespace {

// Bump when the conversion changes, so every item is converted again.
constexpr int kImporterVersion = 1;

const QString kEffects = QStringLiteral("effects");
const QString kTransitions = QStringLiteral("transitions");
const QString kStickers = QStringLiteral("stickers");
const QString kFonts = QStringLiteral("fonts");
const QString kLooks = QStringLiteral("looks");

// Filmora's resource types (resinfo.json "type").
enum FilmoraType {
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

QJsonObject readObject(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return QJsonDocument::fromJson(file.readAll()).object();
}

QByteArray readBytes(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return file.readAll();
}

bool writeBytes(const QString &path, const QByteArray &bytes)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return false;
    file.write(bytes);
    return file.commit();
}

bool writeJson(const QString &path, const QJsonObject &object)
{
    return writeBytes(path, QJsonDocument(object).toJson(QJsonDocument::Indented));
}

QString englishTitle(const QJsonObject &info)
{
    const QJsonObject titles = info.value(QStringLiteral("titles")).toObject();
    QString title = titles.value(QStringLiteral("en")).toString().trimmed();
    if (title.isEmpty() && !titles.isEmpty())
        title = titles.begin().value().toString().trimmed();
    if (title.isEmpty())
        title = info.value(QStringLiteral("slug")).toString().replace(QLatin1Char('_'), QLatin1Char(' '));
    return title;
}

QString filmoraCategory(const QJsonObject &info)
{
    const QJsonArray categories = info.value(QStringLiteral("categories")).toArray();
    for (const QJsonValue &value : categories) {
        const QString name = value.toObject().value(QStringLiteral("name")).toString().trimmed();
        if (!name.isEmpty())
            return name;
    }
    return {};
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
    return slug;
}

QString hashOf(const QString &text, int length)
{
    return QString::fromLatin1(
               QCryptographicHash::hash(text.toUtf8(), QCryptographicHash::Sha1).toHex())
        .left(length);
}

// The item's output folder name: its Filmora id when it has one (stable across renames).
QString packageNameFor(const QJsonObject &info, const QString &itemDir)
{
    const qint64 id = info.value(QStringLiteral("id")).toVariant().toLongLong();
    if (id > 0)
        return QStringLiteral("fm-%1").arg(id);
    return QStringLiteral("fm-") + hashOf(itemDir, 12);
}

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

const QStringList kVideoSuffixes = {QStringLiteral("webm"), QStringLiteral("mov"),
                                    QStringLiteral("mp4"), QStringLiteral("m4v"),
                                    QStringLiteral("mkv"), QStringLiteral("gif")};
const QStringList kImageSuffixes = {QStringLiteral("png"), QStringLiteral("webp"),
                                    QStringLiteral("apng"), QStringLiteral("jpg"),
                                    QStringLiteral("jpeg"), QStringLiteral("svg")};
const QStringList kFontSuffixes = {QStringLiteral("ttf"), QStringLiteral("otf")};

QStringList filesWithSuffix(const QString &dir, const QStringList &suffixes, bool recursive)
{
    QStringList out;
    QDirIterator it(dir, QDir::Files,
                    recursive ? QDirIterator::Subdirectories : QDirIterator::NoIteratorFlags);
    while (it.hasNext()) {
        const QString path = it.next();
        if (suffixes.contains(QFileInfo(path).suffix().toLower()))
            out.append(path);
    }
    out.sort();
    return out;
}

// Weight and italic from a font's name ("Advent Pro SemiExpanded ExtraBold" -> 800).
QPair<int, bool> weightFromName(const QString &name)
{
    const QString n = QString(name).remove(QLatin1Char(' ')).remove(QLatin1Char('-')).toLower();
    const bool italic = n.contains(QLatin1String("italic")) || n.contains(QLatin1String("oblique"));
    static const QList<QPair<QString, int>> kWeights = {
        {QStringLiteral("extralight"), 200}, {QStringLiteral("ultralight"), 200},
        {QStringLiteral("semibold"), 600},   {QStringLiteral("demibold"), 600},
        {QStringLiteral("extrabold"), 800},  {QStringLiteral("ultrabold"), 800},
        {QStringLiteral("thin"), 100},       {QStringLiteral("light"), 300},
        {QStringLiteral("medium"), 500},     {QStringLiteral("bold"), 700},
        {QStringLiteral("black"), 900},      {QStringLiteral("heavy"), 900},
    };
    for (const auto &w : kWeights) {
        if (n.contains(w.first))
            return {w.second, italic};
    }
    return {400, italic};
}

// Filmora prefixes the font files inside a title with a number: "2223506_Paytone One.ttf".
QString familyFromFontFile(const QString &path)
{
    QString base = QFileInfo(path).completeBaseName();
    static const QRegularExpression kNumberPrefix(QStringLiteral("^\\d+_"));
    base.remove(kNumberPrefix);
    return base.replace(QLatin1Char('_'), QLatin1Char(' ')).simplified();
}

struct Record
{
    QString stamp;
    QString kind;     // "effect", ... or empty when skipped
    QString name;
    QString category;
    QString skipReason;
    QStringList outputs; // relative to outRoot
    QJsonObject sticker; // a sticker's pack.json entry

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
        return r;
    }
};

// A stamp that changes when the item's files do.
QString stampFor(const QString &itemDir)
{
    QStringList parts{QString::number(kImporterVersion)};
    QDirIterator it(itemDir, QDir::Files, QDirIterator::Subdirectories);
    QStringList files;
    while (it.hasNext()) {
        const QString path = it.next();
        const QFileInfo fi(path);
        files.append(QStringLiteral("%1:%2:%3")
                         .arg(QDir(itemDir).relativeFilePath(path))
                         .arg(fi.size())
                         .arg(fi.lastModified().toMSecsSinceEpoch()));
    }
    files.sort();
    parts += files;
    return hashOf(parts.join(QLatin1Char('|')), 20);
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
    return {};
}

class Converter
{
public:
    Converter(const QString &outRoot, const ShaderCheck &check) : m_outRoot(outRoot), m_check(check) {}

    Record convert(const QString &itemDir, const QJsonObject &info)
    {
        Record r;
        r.name = englishTitle(info);
        const int type = info.value(QStringLiteral("type")).toInt();
        if (info.value(QStringLiteral("is_encriped")).toInt() == 1) {
            r.skipReason = QStringLiteral("Encrypted by Filmora");
            return r;
        }
        switch (type) {
        case TypeFilter:
        case TypeEffect:
            convertShaderOrLook(itemDir, info, false, &r);
            break;
        case TypeTransition:
            convertShaderOrLook(itemDir, info, true, &r);
            break;
        case TypeElement:
            convertElement(itemDir, info, &r);
            break;
        case TypeFont: {
            // Data/ holds the font; some packs repeat it under All/Data, so only one is taken.
            QStringList files =
                filesWithSuffix(QDir(itemDir).filePath(QStringLiteral("Data")), kFontSuffixes, false);
            if (files.isEmpty())
                files = filesWithSuffix(itemDir, kFontSuffixes, true).mid(0, 1);
            convertFonts(files, r.name, &r);
            if (r.kind.isEmpty())
                r.skipReason = QStringLiteral("No font file in the folder");
            break;
        }
        case TypeTitle: {
            // The title itself needs Filmora's title engine; its fonts are still worth having.
            const QStringList fonts =
                filesWithSuffix(QDir(itemDir).filePath(QStringLiteral("Font")), kFontSuffixes, false);
            convertFonts(fonts, QString(), &r);
            const QString reason = QStringLiteral("Filmora title: needs Filmora's own title engine");
            if (r.kind.isEmpty())
                r.skipReason = reason;
            else
                r.skipReason = reason + QStringLiteral(" (its fonts were added)");
            break;
        }
        case TypeTextAnimation:
            r.skipReason = QStringLiteral("Text animation: uses an animation built into Filmora, not in the folder");
            break;
        case TypeSplitScreen: {
            static const QRegularExpression kBasic(QStringLiteral("^Split_Screen_(\\d+)"),
                                                   QRegularExpression::CaseInsensitiveOption);
            const QRegularExpressionMatch m = kBasic.match(info.value(QStringLiteral("slug")).toString());
            if (m.hasMatch() && m.captured(1).toInt() <= 30)
                r.skipReason = QStringLiteral("Split screen: already built into Dafin (Templates › Split Screen)");
            else
                r.skipReason = QStringLiteral("Split screen: not supported yet");
            break;
        }
        case TypeMusic:
            r.skipReason = QStringLiteral("Music: add audio files through Media instead");
            break;
        default:
            if (type >= 100 || !info.value(QStringLiteral("extension")).toObject()
                                    .value(QStringLiteral("extra_cfg")).toString().isEmpty())
                r.skipReason = QStringLiteral("AI template: runs on Wondershare's servers");
            else
                r.skipReason = QStringLiteral("Filmora item type %1: not supported yet").arg(type);
            break;
        }
        if (r.kind.isEmpty() && r.skipReason.isEmpty())
            r.skipReason = QStringLiteral("Nothing usable in the folder");
        return r;
    }

private:
    void convertShaderOrLook(const QString &itemDir, const QJsonObject &info, bool transition, Record *r)
    {
        const QDir data(QDir(itemDir).filePath(QStringLiteral("Data")));
        QString confName;
        const QByteArray filterXml = readBytes(data.filePath(QStringLiteral("filter.xml")));
        static const QRegularExpression kConfig(QStringLiteral("<ConfigFile>\\s*([^<]+?)\\s*</ConfigFile>"));
        const QRegularExpressionMatch m = kConfig.match(QString::fromUtf8(filterXml));
        if (m.hasMatch())
            confName = m.captured(1);
        if (confName.isEmpty()) {
            const QStringList confs = data.entryList({QStringLiteral("*.conf")}, QDir::Files, QDir::Name);
            confName = confs.value(0);
        }

        if (confName.isEmpty()) {
            if (!transition) {
                const QStringList cubes = filesWithSuffix(data.absolutePath(), {QStringLiteral("cube")}, true);
                if (!cubes.isEmpty()) {
                    convertLook(itemDir, cubes.first(), info, r);
                    return;
                }
            }
            r->skipReason = QStringLiteral("Not a shader effect (made with Filmora's own effect engine)");
            return;
        }

        QJsonParseError parseError{};
        const QJsonObject conf =
            QJsonDocument::fromJson(readBytes(data.filePath(confName)), &parseError).object();
        if (conf.isEmpty()) {
            r->skipReason = QStringLiteral("Its settings file could not be read");
            return;
        }
        if (conf.value(QStringLiteral("TRANSITION")).toInt() == 1 && !transition) {
            transition = true;
        }
        const QString fragName = conf.value(QStringLiteral("OPENGL")).toString();
        if (fragName.isEmpty()) {
            r->skipReason = QStringLiteral("No OpenGL shader in it (OpenCL only)");
            return;
        }
        const ShaderTranslation shader =
            translateFilmoraShader(readBytes(data.filePath(fragName)), conf, transition);
        if (!shader.ok) {
            r->skipReason = shader.error;
            return;
        }
        if (m_check) {
            QString error;
            if (!m_check(shader.source, &error)) {
                r->skipReason = QStringLiteral("Its shader does not compile here: %1").arg(firstLine(error));
                return;
            }
        }

        const QString kind = transition ? QStringLiteral("transition") : QStringLiteral("effect");
        const QString rel = kindDir(kind) + QLatin1Char('/') + packageNameFor(info, itemDir);
        QDir out(QDir(m_outRoot).filePath(rel));
        out.removeRecursively();
        QDir().mkpath(out.absolutePath());

        QJsonArray params;
        for (const QJsonValue &value : conf.value(QStringLiteral("PARAMS")).toArray()) {
            const QJsonObject p = value.toObject();
            const QString name = p.value(QStringLiteral("NAME")).toString();
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
                param.insert(QStringLiteral("defaultValue"), p.value(QStringLiteral("DEFAULT")).toVariant().toDouble() > 0.5);
            } else {
                param.insert(QStringLiteral("minValue"), p.value(QStringLiteral("MIN")).toDouble(0.0));
                param.insert(QStringLiteral("maxValue"), p.value(QStringLiteral("MAX")).toDouble(1.0));
                param.insert(QStringLiteral("defaultValue"), p.value(QStringLiteral("DEFAULT")).toDouble(0.0));
            }
            params.append(param);
        }

        const QString category = dafinCategoryFor(kindDir(kind), filmoraCategory(info), r->name);
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

        // A small picture for the card: the item's own thumbnail, copied (it is a few KB).
        const QString thumb = QDir(itemDir).filePath(QStringLiteral("thumbnail.png"));
        if (QFileInfo::exists(thumb) && QFile::copy(thumb, out.filePath(QStringLiteral("thumbnail.png")))) {
            if (transition) {
                pkg.insert(QStringLiteral("previewStrip"), QStringLiteral("thumbnail.png"));
                pkg.insert(QStringLiteral("previewFrames"), 1);
            } else {
                pkg.insert(QStringLiteral("thumbnail"), QStringLiteral("thumbnail.png"));
            }
        }

        writeBytes(out.filePath(QStringLiteral("main.frag")), shader.source.toUtf8());
        writeJson(out.filePath(transition ? QStringLiteral("transition.json") : QStringLiteral("effect.json")), pkg);
        r->kind = kind;
        r->category = category;
        r->outputs.append(rel);
    }

    void convertLook(const QString &itemDir, const QString &cubePath, const QJsonObject &info, Record *r)
    {
        const QString rel = kLooks + QLatin1Char('/') + packageNameFor(info, itemDir);
        QDir out(QDir(m_outRoot).filePath(rel));
        out.removeRecursively();
        QDir().mkpath(out.absolutePath());
        if (!QFile::copy(cubePath, out.filePath(QStringLiteral("look.cube")))) {
            r->skipReason = QStringLiteral("Its colour table could not be copied");
            return;
        }
        const QString category = filmoraCategory(info).isEmpty() ? QStringLiteral("Filmora") : filmoraCategory(info);
        writeJson(out.filePath(QStringLiteral("look.json")),
                  QJsonObject{{QStringLiteral("name"), r->name},
                              {QStringLiteral("category"), category},
                              {QStringLiteral("description"), QStringLiteral("From a linked Filmora folder.")},
                              {QStringLiteral("order"), 90000}});
        r->kind = QStringLiteral("look");
        r->category = category;
        r->outputs.append(rel);
    }

    void convertElement(const QString &itemDir, const QJsonObject &info, Record *r)
    {
        const QString data = QDir(itemDir).filePath(QStringLiteral("Data"));
        QString media = filesWithSuffix(data, kVideoSuffixes, true).value(0);
        if (media.isEmpty())
            media = filesWithSuffix(data, kImageSuffixes, true).value(0);
        if (media.isEmpty()) {
            r->skipReason = QFileInfo::exists(QDir(data).filePath(QStringLiteral("data.xml")))
                                ? QStringLiteral("Element made with Filmora's own template engine")
                                : QStringLiteral("No video or picture in the folder");
            return;
        }
        QString thumbnail = QDir(itemDir).filePath(QStringLiteral("thumbnail.png"));
        if (!QFileInfo::exists(thumbnail))
            thumbnail = kImageSuffixes.contains(QFileInfo(media).suffix().toLower()) ? media : QString();

        const QString categoryName =
            filmoraCategory(info).isEmpty() ? QStringLiteral("Filmora") : filmoraCategory(info);
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

    void convertFonts(const QStringList &files, const QString &title, Record *r)
    {
        for (const QString &file : files) {
            const QString family = title.isEmpty() || files.size() > 1 ? familyFromFontFile(file) : title;
            if (family.isEmpty())
                continue;
            const QString rel = kFonts + QStringLiteral("/fm-") + hashOf(QFileInfo(file).absoluteFilePath(), 12);
            const QDir out(QDir(m_outRoot).filePath(rel));
            QDir().mkpath(out.absolutePath());
            const auto weight = weightFromName(family);
            QJsonObject face{{QStringLiteral("file"), QFileInfo(file).absoluteFilePath()},
                             {QStringLiteral("weight"), weight.first}};
            if (weight.second)
                face.insert(QStringLiteral("italic"), true);
            writeJson(out.filePath(QStringLiteral("family.json")),
                      QJsonObject{{QStringLiteral("id"), out.dirName()},
                                  {QStringLiteral("family"), family},
                                  {QStringLiteral("category"), QStringLiteral("imported")},
                                  {QStringLiteral("license"), QStringLiteral("See the font's own licence")},
                                  {QStringLiteral("order"), 90000},
                                  {QStringLiteral("faces"), QJsonArray{face}}});
            r->outputs.append(rel);
            if (r->kind.isEmpty()) {
                r->kind = QStringLiteral("font");
                r->category = QStringLiteral("imported");
            }
        }
    }

    QString m_outRoot;
    const ShaderCheck &m_check;
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
    for (const QJsonValue &value : conf.value(QStringLiteral("PARAMS")).toArray()) {
        const QJsonObject p = value.toObject();
        const QString name = p.value(QStringLiteral("NAME")).toString();
        if (name.isEmpty())
            continue;
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

QString dafinCategoryFor(const QString &packageKind, const QString &filmoraCategory,
                         const QString &title)
{
    struct Rule
    {
        QStringList words;
        QString effect;     // Dafin effect category
        QString transition; // Dafin transition category
    };
    static const QList<Rule> kRules = {
        {{QStringLiteral("glitch"), QStringLiteral("distort"), QStringLiteral("digital")},
         QStringLiteral("glitch"), QStringLiteral("glitch")},
        {{QStringLiteral("blur")}, QStringLiteral("blurs"), QString()},
        {{QStringLiteral("retro"), QStringLiteral("vintage"), QStringLiteral("vhs"), QStringLiteral("film"),
          QStringLiteral("old")},
         QStringLiteral("retro"), QString()},
        {{QStringLiteral("basic"), QStringLiteral("dissolve"), QStringLiteral("fade")}, QString(),
         QStringLiteral("basic")},
        {{QStringLiteral("shape"), QStringLiteral("geometric"), QStringLiteral("grid"), QStringLiteral("slide"),
          QStringLiteral("wipe")},
         QString(), QStringLiteral("geometric")},
        {{QStringLiteral("liquid"), QStringLiteral("particle"), QStringLiteral("ink")}, QString(),
         QStringLiteral("liquid")},
        {{QStringLiteral("shake"), QStringLiteral("impact"), QStringLiteral("punch")},
         QStringLiteral("impact"), QString()},
        {{QStringLiteral("color"), QStringLiteral("colour"), QStringLiteral("tone"), QStringLiteral("lut")},
         QStringLiteral("color"), QString()},
        {{QStringLiteral("light"), QStringLiteral("flare"), QStringLiteral("bokeh"), QStringLiteral("dream"),
          QStringLiteral("glow")},
         QStringLiteral("dreamy"), QString()},
    };
    const bool isTransition = packageKind == QLatin1String("transitions");
    auto match = [&](const QString &text) -> QString {
        const QString lower = text.toLower();
        for (const Rule &rule : kRules) {
            const QString target = isTransition ? rule.transition : rule.effect;
            if (target.isEmpty())
                continue;
            for (const QString &word : rule.words) {
                if (lower.contains(word))
                    return target;
            }
        }
        return {};
    };
    QString category = match(filmoraCategory);
    if (category.isEmpty())
        category = match(title);
    if (category.isEmpty())
        category = filmoraCategory.trimmed();
    if (category.isEmpty())
        category = QStringLiteral("filmora");
    return category;
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
    if (sourceRoot.isEmpty() || !source.exists()) {
        report.error = QStringLiteral("The folder is missing");
        return report;
    }
    QDir().mkpath(outRoot);
    const QString indexPath = QDir(outRoot).filePath(QStringLiteral("library.json"));
    const QJsonObject oldIndex = readObject(indexPath);
    const QJsonObject oldItems = oldIndex.value(QStringLiteral("items")).toObject();
    const QString packId = QStringLiteral("linked-") + QDir(outRoot).dirName();

    QJsonObject newItems;
    QSet<QString> changed;
    Converter converter(outRoot, check);

    QStringList itemDirs;
    QDirIterator it(source.absolutePath(), {QStringLiteral("resinfo.json")}, QDir::Files,
                    QDirIterator::Subdirectories);
    while (it.hasNext())
        itemDirs.append(QFileInfo(it.next()).absolutePath());
    itemDirs.sort();

    for (const QString &itemDir : itemDirs) {
        if (cancel && cancel->load()) {
            report.cancelled = true;
            return report; // the old index stays, so the next run picks up where this one stopped
        }
        const QString key = source.relativeFilePath(itemDir);
        const QString stamp = stampFor(itemDir);
        Record record = Record::fromJson(oldItems.value(key).toObject());
        bool reuse = record.stamp == stamp;
        for (const QString &rel : record.outputs) {
            if (!QFileInfo::exists(QDir(outRoot).filePath(rel)))
                reuse = false;
        }
        if (!reuse) {
            for (const QString &rel : record.outputs) {
                QDir(QDir(outRoot).filePath(rel)).removeRecursively();
                changed.insert(rel.section(QLatin1Char('/'), 0, 0));
            }
            const QString oldKind = record.kind;
            record = converter.convert(itemDir, readObject(QDir(itemDir).filePath(QStringLiteral("resinfo.json"))));
            record.stamp = stamp;
            for (const QString &rel : record.outputs)
                changed.insert(rel.section(QLatin1Char('/'), 0, 0));
            if (record.kind == QLatin1String("sticker") || oldKind == QLatin1String("sticker"))
                changed.insert(kStickers);
        }
        newItems.insert(key, record.toJson());

        ImportedItem item{record.kind, record.name, record.category, itemDir, record.skipReason};
        report.items.append(item);
        if (record.kind == QLatin1String("effect"))
            ++report.effects;
        else if (record.kind == QLatin1String("transition"))
            ++report.transitions;
        else if (record.kind == QLatin1String("sticker"))
            ++report.stickers;
        else if (record.kind == QLatin1String("look"))
            ++report.looks;
        else if (record.kind == QLatin1String("font"))
            report.fonts += record.outputs.size(); // a title can bring several
    }

    // Items that are gone take their packages with them.
    for (auto old = oldItems.constBegin(); old != oldItems.constEnd(); ++old) {
        if (newItems.contains(old.key()))
            continue;
        const Record gone = Record::fromJson(old.value().toObject());
        for (const QString &rel : gone.outputs) {
            QDir(QDir(outRoot).filePath(rel)).removeRecursively();
            changed.insert(rel.section(QLatin1Char('/'), 0, 0));
        }
        if (gone.kind == QLatin1String("sticker"))
            changed.insert(kStickers);
    }

    // One sticker pack for the whole folder, rewritten only when it would read differently.
    const QString packDir = QDir(outRoot).filePath(kStickers + QLatin1Char('/') + packId);
    const QString packPath = QDir(packDir).filePath(QStringLiteral("pack.json"));
    QJsonArray stickers;
    QJsonArray categories;
    QSet<QString> seenCategories;
    for (auto item = newItems.constBegin(); item != newItems.constEnd(); ++item) {
        const QJsonObject sticker = item.value().toObject().value(QStringLiteral("sticker")).toObject();
        if (sticker.isEmpty())
            continue;
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
                                    {QStringLiteral("name"), source.dirName()},
                                    {QStringLiteral("license"), QStringLiteral("From a linked folder")},
                                    {QStringLiteral("order"), 900},
                                    {QStringLiteral("categories"), categories},
                                    {QStringLiteral("stickers"), stickers}})
              .toJson(QJsonDocument::Indented);
    if (pack != readBytes(packPath)) {
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

    for (const QString &kind : {kEffects, kTransitions, kStickers, kFonts, kLooks}) {
        if (changed.contains(kind))
            report.changedKinds.append(kind);
    }
    return report;
}

} // namespace dafin::linked
