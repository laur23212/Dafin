// Dafin: linked Filmora effect folders (FilmoraLibraryImporter). The fixtures are written here,
// in Filmora's layout but with made-up content, so no Filmora asset is kept in the repository.
#include "engine/packages/linked/FilmoraLibraryImporter.h"
#include "engine/packages/linked/FilmoraTitleConverter.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QtTest>

using namespace dafin::linked;

namespace {

void writeFile(const QString &path, const QByteArray &bytes)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(bytes);
}

void writeResinfo(const QString &itemDir, qint64 id, int type, const QString &title,
                  const QString &category = {})
{
    QJsonObject info{{QStringLiteral("id"), id},
                     {QStringLiteral("type"), type},
                     {QStringLiteral("slug"), QString(title).replace(QLatin1Char(' '), QLatin1Char('_'))},
                     {QStringLiteral("titles"), QJsonObject{{QStringLiteral("en"), title}}},
                     {QStringLiteral("is_encriped"), 0}};
    if (!category.isEmpty())
        info.insert(QStringLiteral("categories"),
                    QJsonArray{QJsonObject{{QStringLiteral("id"), 1}, {QStringLiteral("name"), category}}});
    writeFile(QDir(itemDir).filePath(QStringLiteral("resinfo.json")), QJsonDocument(info).toJson());
}

const QByteArray kFilterFrag =
    "// float unused = PREFIX(total_time);\n"
    "vec4 FUNCNAME(vec2 tc) {\n"
    "    float wobble = sin(PREFIX(global_time) * PREFIX(speed)) * 0.01;\n"
    "    int steps = PREFIX(steps);\n"
    "    vec4 c = INPUT(tc + vec2(wobble, 0.0));\n"
    "    return vec4(c.rgb * float(steps) / 4.0, c.a);\n"
    "}\n";

QJsonObject filterConf(bool encrypted)
{
    return QJsonObject{
        {QStringLiteral("NAME"), QStringLiteral("Wobble")},
        {QStringLiteral("OPENGL"), QStringLiteral("Wobble.frag")},
        {QStringLiteral("ENCRYPT"), encrypted ? QStringLiteral("1") : QStringLiteral("0")},
        {QStringLiteral("PARAMS"),
         QJsonArray{QJsonObject{{QStringLiteral("NAME"), QStringLiteral("speed")},
                                {QStringLiteral("TYPE"), QStringLiteral("float")},
                                {QStringLiteral("DEFAULT"), 2.0},
                                {QStringLiteral("MIN"), 0.0},
                                {QStringLiteral("MAX"), 10.0}},
                    QJsonObject{{QStringLiteral("NAME"), QStringLiteral("steps")},
                                {QStringLiteral("TYPE"), QStringLiteral("int")},
                                {QStringLiteral("DEFAULT"), 4},
                                {QStringLiteral("MIN"), 1},
                                {QStringLiteral("MAX"), 8}}}},
    };
}

void writeShaderItem(const QString &itemDir, qint64 id, int type, const QString &title,
                     const QString &category, const QByteArray &frag, const QJsonObject &conf,
                     bool transition)
{
    writeResinfo(itemDir, id, type, title, category);
    const QString data = QDir(itemDir).filePath(QStringLiteral("Data"));
    writeFile(QDir(data).filePath(QStringLiteral("filter.xml")),
              QByteArray("<?xml version=\"1.0\"?>\n<Filter><Transition>")
                  + (transition ? "1" : "0") + "</Transition><GPUFilter>\n<ConfigFile>"
                  + conf.value(QStringLiteral("NAME")).toString().toUtf8()
                  + ".conf</ConfigFile></GPUFilter></Filter>");
    writeFile(QDir(data).filePath(conf.value(QStringLiteral("NAME")).toString() + QStringLiteral(".conf")),
              QJsonDocument(conf).toJson());
    const bool encrypted = conf.value(QStringLiteral("ENCRYPT")).toString() == QLatin1String("1");
    writeFile(QDir(data).filePath(conf.value(QStringLiteral("OPENGL")).toString()),
              encrypted ? frag.toBase64() : frag);
}

QString repoFont(const QString &relative)
{
    return QDir(QStringLiteral(DAFIN_SOURCE_DIR "/title-fonts")).filePath(relative);
}

void copyFile(const QString &from, const QString &to)
{
    QDir().mkpath(QFileInfo(to).absolutePath());
    QVERIFY2(QFile::copy(from, to), qPrintable(from));
}

QJsonObject readJson(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return QJsonDocument::fromJson(file.readAll()).object();
}

// One effect on a Filmora title clip: its display name, the Filmora resource it comes from
// (userData key 12, base64 JSON) and its parameters.
QJsonObject titleEffect(const QString &display, qint64 resourceId, int resType, const QJsonObject &params)
{
    QJsonArray paramList;
    for (auto it = params.constBegin(); it != params.constEnd(); ++it)
        paramList.append(QJsonObject{{QStringLiteral("name"), it.key()},
                                     {QStringLiteral("fxParam"), QJsonObject{{QStringLiteral("paramType"), 3},
                                                                             {QStringLiteral("unValue"), it.value()}}}});
    const QByteArray ref = QJsonDocument(QJsonObject{{QStringLiteral("id"), resourceId},
                                                     {QStringLiteral("res_type"), resType}}).toJson(QJsonDocument::Compact);
    return QJsonObject{{QStringLiteral("display"), display},
                       {QStringLiteral("id"), QStringLiteral("0000-%1").arg(display)},
                       {QStringLiteral("paramList"), paramList},
                       {QStringLiteral("userData"), QJsonArray{QJsonObject{{QStringLiteral("key"), 12},
                                                                           {QStringLiteral("data"), QString::fromLatin1(ref.toBase64())}}}}};
}

// A Filmora title item in Filmora's layout (resinfo.json, Data/data.json holding a small project
// with one text clip whose look is a JSON string in scriptBuf), with made-up content.
void writeTitleItem(const QString &itemDir, qint64 id, const QString &title, const QString &text,
                    const QJsonArray &effects, const QJsonObject &extraClipFields = {})
{
    writeResinfo(itemDir, id, 1, title, QStringLiteral("Social Media"));
    const QJsonObject script{
        {QStringLiteral("Text"), text},
        {QStringLiteral("ViewSize"), QStringLiteral("1920:1080")},
        {QStringLiteral("Layout"), QJsonObject{{QStringLiteral("TextAlign"), QStringLiteral("left")}}},
        {QStringLiteral("TextData"),
         QJsonArray{QJsonObject{
             {QStringLiteral("CharData"), text},
             {QStringLiteral("Basic"), QJsonObject{{QStringLiteral("FontName"), QStringLiteral("Lato Bold")},
                                                   {QStringLiteral("FontSize"), 120.4},
                                                   {QStringLiteral("TextAlpha"), 255},
                                                   // 0xRRGGBB: this is #102030.
                                                   {QStringLiteral("TextColor"), QJsonArray{QJsonObject{{QStringLiteral("Color"), 0x102030}}}}}},
             {QStringLiteral("Border"), QJsonObject{{QStringLiteral("Enable"), 1}, {QStringLiteral("Size"), 6},
                                                    {QStringLiteral("Alpha"), 255}, {QStringLiteral("Color"), 0xff0000}}},
             {QStringLiteral("Shadow"), QJsonObject{{QStringLiteral("Enable"), 1}, {QStringLiteral("Alpha"), 128},
                                                    {QStringLiteral("BlurRadius"), 5}, {QStringLiteral("Color"), 0},
                                                    {QStringLiteral("Direction"), 3}, {QStringLiteral("Distance"), 2}}}}}},
    };
    QJsonArray chain{QJsonObject{{QStringLiteral("display"), QStringLiteral("transform")},
                                 {QStringLiteral("id"), QStringLiteral("video/effect/transform")},
                                 {QStringLiteral("paramList"),
                                  QJsonArray{QJsonObject{{QStringLiteral("name"), QStringLiteral("Position_x")},
                                                         {QStringLiteral("fxParam"), QJsonObject{{QStringLiteral("unValue"), 0.5}}}},
                                             QJsonObject{{QStringLiteral("name"), QStringLiteral("Position_y")},
                                                         {QStringLiteral("fxParam"), QJsonObject{{QStringLiteral("unValue"), 0.85}}}}}}}};
    for (const QJsonValue &e : effects)
        chain.append(e);
    QJsonObject clip{{QStringLiteral("type"), 4},
                     {QStringLiteral("tlEnd"), 50000000},
                     {QStringLiteral("scriptBuf"), QString::fromUtf8(QJsonDocument(script).toJson(QJsonDocument::Compact))},
                     {QStringLiteral("effectChainList"), QJsonArray{QJsonObject{{QStringLiteral("effectList"), chain}}}}};
    for (auto it = extraClipFields.constBegin(); it != extraClipFields.constEnd(); ++it)
        clip.insert(it.key(), it.value());
    const QJsonObject data{
        {QStringLiteral("projectName"), QStringLiteral("TlbTitleClip")},
        {QStringLiteral("tlEnd"), 50000000},
        {QStringLiteral("subTimeline"),
         QJsonObject{{QStringLiteral("timelineInfos"),
                      QJsonArray{QJsonObject{{QStringLiteral("trackInfos"),
                                              QJsonArray{QJsonObject{{QStringLiteral("clipList"), QJsonArray{clip}}}}}}}}}},
    };
    writeFile(QDir(itemDir).filePath(QStringLiteral("Data/data.json")), QJsonDocument(data).toJson());
}

} // namespace

class TestLinkedLibraries : public QObject
{
    Q_OBJECT

private slots:
    void shaderTranslatesPrefixesInputsAndParams()
    {
        const ShaderTranslation t = translateFilmoraShader(kFilterFrag.toBase64(), filterConf(true), false);
        QVERIFY2(t.ok, qPrintable(t.error));
        QVERIFY(t.source.startsWith(QLatin1String("#version 330 core")));
        QVERIFY(t.source.contains(QLatin1String("uniform float fm_speed;")));
        QVERIFY(t.source.contains(QLatin1String("(u_time)")));
        QVERIFY(t.source.contains(QLatin1String("int(floor(fm_steps + 0.5))")));
        QVERIFY(t.source.contains(QLatin1String("#define INPUT(uv) texture(u_currentTexture, (uv))")));
        QVERIFY(t.source.contains(QLatin1String("fragColor = fm_effect(v_texCoord)")));
        // The commented-out total_time is not a real use.
        QVERIFY(!t.source.contains(QLatin1String("total_time")));
    }

    void shaderNeedingFilmoraInternalsIsRefused()
    {
        const QByteArray internal = "vec4 FUNCNAME(vec2 tc) { vec4 l = PREFIX(leftLine); return INPUT1(tc) * l; }\n";
        ShaderTranslation t = translateFilmoraShader(internal, filterConf(false), false);
        QVERIFY(!t.ok);
        QVERIFY(t.error.contains(QLatin1String("leftLine")));

        const QByteArray extraPass = "vec4 FUNCNAME(vec2 tc) { return INPUT1(tc) + INPUT3(tc); }\n";
        t = translateFilmoraShader(extraPass, filterConf(false), true);
        QVERIFY(!t.ok);
        QVERIFY(t.error.contains(QLatin1String("INPUT3")));

        t = translateFilmoraShader(QByteArray("not a shader at all"), filterConf(false), false);
        QVERIFY(!t.ok);
    }

    void transitionReadsBothClipsAndProgress()
    {
        const QByteArray frag = "vec4 FUNCNAME(vec2 tc) { return mix(INPUT1(tc), INPUT2(tc), PREFIX(progress)); }\n";
        const ShaderTranslation t = translateFilmoraShader(frag, filterConf(false), true);
        QVERIFY2(t.ok, qPrintable(t.error));
        QVERIFY(t.source.contains(QLatin1String("u_fromTexture")));
        QVERIFY(t.source.contains(QLatin1String("u_toTexture")));
        QVERIFY(t.source.contains(QLatin1String("(u_progress)")));
    }

    void categoriesPreferDafinsOwn()
    {
        QCOMPARE(dafinCategoryFor(QStringLiteral("effects"), QStringLiteral("Blur"), QStringLiteral("Blur to Clear")),
                 QStringLiteral("blurs"));
        // A category Dafin does not know: the title decides (glitch and color tie; glitch is the
        // more specific).
        QCOMPARE(dafinCategoryFor(QStringLiteral("effects"), QStringLiteral("VFX Text"), QStringLiteral("Color Glitch")),
                 QStringLiteral("glitch"));
        QCOMPARE(dafinCategoryFor(QStringLiteral("effects"), QStringLiteral("Social Media"), QStringLiteral("Chat 07")),
                 QStringLiteral("Social Media"));
        QCOMPARE(dafinCategoryFor(QStringLiteral("transitions"), QStringLiteral("Basic"), QStringLiteral("Fade")),
                 QStringLiteral("basic"));
        QCOMPARE(dafinCategoryFor(QStringLiteral("transitions"), QStringLiteral("Light"), QStringLiteral("Beam Transition 7")),
                 QStringLiteral("cinematic"));
        QCOMPARE(dafinCategoryFor(QStringLiteral("effects"), QString(), QStringLiteral("Something")),
                 QStringLiteral("filmora"));
        // "Filmora" in a folder name is not "film".
        QCOMPARE(dafinCategoryFor(QStringLiteral("effects"), QString(), QStringLiteral("Filmora Pack 3")),
                 QStringLiteral("filmora"));
    }

    void smartCategoryWeighsAllHints()
    {
        CategoryHints slugOnly;
        slugOnly.categorySlugs = {QStringLiteral("effect_BackgroundBlur_en")};
        slugOnly.title = QStringLiteral("Clear 01");
        QCOMPARE(smartCategory(QStringLiteral("effects"), slugOnly), QStringLiteral("blurs"));

        // Words in the code alone are not enough to move an item...
        CategoryHints codeOnly;
        codeOnly.filmoraCategories = {QStringLiteral("Trending")};
        codeOnly.title = QStringLiteral("Wobble 01");
        codeOnly.shader = QStringLiteral("float gaussianBlur(vec2 uv) { return blurRadius; }");
        QCOMPARE(smartCategory(QStringLiteral("effects"), codeOnly), QStringLiteral("Trending"));

        // ...but they settle a tie between two names.
        CategoryHints tie;
        tie.title = QStringLiteral("Dream Haze Blur");
        QCOMPARE(smartCategory(QStringLiteral("effects"), tie), QStringLiteral("blurs"));
        tie.paramNames = {QStringLiteral("bloomStrength")};
        tie.shader = QStringLiteral("vec3 glow = bloom(c);");
        QCOMPARE(smartCategory(QStringLiteral("effects"), tie), QStringLiteral("dreamy"));
    }

    void fontInfoComesFromTheFile()
    {
        const FontInfo cabin = readFontInfo(repoFont(QStringLiteral("cabin/Cabin-Bold.ttf")));
        QVERIFY(cabin.ok);
        QCOMPARE(cabin.family, QStringLiteral("Cabin"));
        QCOMPARE(cabin.weight, 700);
        QVERIFY(!cabin.italic);
        QCOMPARE(cabin.category, QStringLiteral("clean"));

        const FontInfo serif = readFontInfo(repoFont(QStringLiteral("noto-serif/NotoSerif-Regular.ttf")));
        QCOMPARE(serif.family, QStringLiteral("Noto Serif"));
        QCOMPARE(serif.category, QStringLiteral("editorial"));

        // Not a font, or a damaged one: refused, never read past its end.
        QTemporaryDir dir;
        const QString fake = QDir(dir.path()).filePath(QStringLiteral("fake.ttf"));
        writeFile(fake, QByteArray("\x00\x01\x00\x00\x00\xff", 6) + QByteArray(40, '\xff'));
        QVERIFY(!readFontInfo(fake).ok);
        writeFile(fake, "not a font");
        QVERIFY(!readFontInfo(fake).ok);
    }

    void importsAFilmoraFolderIntoPackages()
    {
        QTemporaryDir source;
        QTemporaryDir out;
        QVERIFY(source.isValid() && out.isValid());
        const QDir src(source.path());

        writeShaderItem(src.filePath(QStringLiteral("Filter/3_Wobble")), 1001, 3, QStringLiteral("Color Wobble"),
                        QStringLiteral("VFX Text"), kFilterFrag, filterConf(true), false);
        writeShaderItem(src.filePath(QStringLiteral("transition/2_Mix")), 1002, 2, QStringLiteral("Soft Mix"),
                        QStringLiteral("Basic"),
                        "vec4 FUNCNAME(vec2 tc) { return mix(INPUT1(tc), INPUT2(tc), PREFIX(progress)); }\n",
                        filterConf(false), true);
        writeFile(src.filePath(QStringLiteral("transition/2_Mix/thumbnail.png")), "png");
        writeShaderItem(src.filePath(QStringLiteral("transition/2_Beam")), 1003, 2, QStringLiteral("Beam"),
                        QStringLiteral("Light"), "vec4 FUNCNAME(vec2 tc) { return INPUT3(tc); }\n",
                        filterConf(false), true);

        const QString element = src.filePath(QStringLiteral("Element/7_Chat"));
        writeResinfo(element, 1004, 7, QStringLiteral("Chat Bubble"), QStringLiteral("Social Media"));
        writeFile(QDir(element).filePath(QStringLiteral("Data/Chat Bubble.webm")), "webm");
        writeFile(QDir(element).filePath(QStringLiteral("thumbnail.png")), "png");

        const QString font = src.filePath(QStringLiteral("resFont/35_Cabin"));
        writeResinfo(font, 1005, 35, QStringLiteral("Cabin Bold"));
        copyFile(repoFont(QStringLiteral("cabin/Cabin-Bold.ttf")), QDir(font).filePath(QStringLiteral("Data/Cabin-Bold.ttf")));
        copyFile(repoFont(QStringLiteral("cabin/Cabin-Bold.ttf")), QDir(font).filePath(QStringLiteral("All/Data/Cabin-Bold.ttf")));

        const QString title = src.filePath(QStringLiteral("title/1_Cartoon"));
        writeResinfo(title, 1006, 1, QStringLiteral("3D Cartoon"), QStringLiteral("3D Titles"));
        writeFile(QDir(title).filePath(QStringLiteral("Data/data.json")), "{}");
        copyFile(repoFont(QStringLiteral("lato/Lato-Bold.ttf")), QDir(title).filePath(QStringLiteral("Font/2223506_Lato.ttf")));

        writeResinfo(src.filePath(QStringLiteral("resTextAnimation/27_Glow")), 1007, 27, QStringLiteral("Glow In"));
        writeResinfo(src.filePath(QStringLiteral("resImageToVideo/106_Angry")), 1008, 106, QStringLiteral("Angry"));

        ImportReport report = importFilmoraFolder(source.path(), out.path());
        QVERIFY(report.error.isEmpty());
        QCOMPARE(report.items.size(), 8);
        QCOMPARE(report.effects, 1);
        QCOMPARE(report.transitions, 1);
        QCOMPARE(report.stickers, 1);
        QCOMPARE(report.fonts, 2); // Cabin (its All/Data copy used once), and Lato from the title
        QCOMPARE(report.skipped(), 3); // Beam, text animation, AI template
        QVERIFY(report.changedKinds.contains(QStringLiteral("effects")));
        QVERIFY(report.changedKinds.contains(QStringLiteral("stickers")));

        const QDir o(out.path());
        const QJsonObject effect = readJson(o.filePath(QStringLiteral("effects/fm-1001/effect.json")));
        QCOMPARE(effect.value(QStringLiteral("id")).toString(), QStringLiteral("filmora.1001"));
        QCOMPARE(effect.value(QStringLiteral("displayName")).toString(), QStringLiteral("Color Wobble"));
        // "VFX Text" means nothing to Dafin, so the title decides.
        QCOMPARE(effect.value(QStringLiteral("category")).toString(), QStringLiteral("color"));
        const QJsonArray params = effect.value(QStringLiteral("parameters")).toArray();
        QCOMPARE(params.size(), 2);
        QCOMPARE(params.at(1).toObject().value(QStringLiteral("identifier")).toString(), QStringLiteral("fm_steps"));
        QCOMPARE(params.at(1).toObject().value(QStringLiteral("type")).toString(), QStringLiteral("int"));
        QVERIFY(QFileInfo::exists(o.filePath(QStringLiteral("effects/fm-1001/main.frag"))));

        const QJsonObject transition = readJson(o.filePath(QStringLiteral("transitions/fm-1002/transition.json")));
        QCOMPARE(transition.value(QStringLiteral("category")).toString(), QStringLiteral("basic"));
        QCOMPARE(transition.value(QStringLiteral("previewStrip")).toString(), QStringLiteral("thumbnail.png"));
        QVERIFY(!QFileInfo::exists(o.filePath(QStringLiteral("transitions/fm-1003"))));

        const QJsonObject pack = readJson(o.filePath(QStringLiteral("stickers/linked-%1/pack.json").arg(o.dirName())));
        const QJsonObject sticker = pack.value(QStringLiteral("stickers")).toArray().at(0).toObject();
        QCOMPARE(sticker.value(QStringLiteral("label")).toString(), QStringLiteral("Chat Bubble"));
        QCOMPARE(sticker.value(QStringLiteral("category")).toString(), QStringLiteral("social-media"));
        QVERIFY(sticker.value(QStringLiteral("file")).toString().endsWith(QLatin1String("Chat Bubble.webm")));
        QCOMPARE(pack.value(QStringLiteral("categories")).toArray().at(0).toObject()
                     .value(QStringLiteral("label")).toString(),
                 QStringLiteral("Social Media"));

        const QFileInfoList fontDirs =
            QDir(o.filePath(QStringLiteral("fonts"))).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
        QCOMPARE(fontDirs.size(), 2);
        QStringList families;
        for (const QFileInfo &dir : fontDirs) {
            const QJsonObject family = readJson(QDir(dir.filePath()).filePath(QStringLiteral("family.json")));
            families.append(family.value(QStringLiteral("family")).toString());
            QCOMPARE(family.value(QStringLiteral("faces")).toArray().size(), 1);
            QCOMPARE(family.value(QStringLiteral("faces")).toArray().at(0).toObject()
                         .value(QStringLiteral("weight")).toInt(), 700);
        }
        families.sort();
        QCOMPARE(families, (QStringList{QStringLiteral("Cabin"), QStringLiteral("Lato")}));

        // Nothing changed: nothing is converted or reloaded again.
        report = importFilmoraFolder(source.path(), out.path());
        QCOMPARE(report.effects, 1);
        QVERIFY2(report.changedKinds.isEmpty(), qPrintable(report.changedKinds.join(QLatin1Char(','))));

        // An item removed from the folder takes its package with it.
        QVERIFY(QDir(src.filePath(QStringLiteral("Filter/3_Wobble"))).removeRecursively());
        report = importFilmoraFolder(source.path(), out.path());
        QCOMPARE(report.effects, 0);
        QCOMPARE(report.changedKinds, QStringList{QStringLiteral("effects")});
        QVERIFY(!QFileInfo::exists(o.filePath(QStringLiteral("effects/fm-1001"))));
    }

    void duplicatesAndLooseFilesAreHandled()
    {
        QTemporaryDir source;
        QTemporaryDir out;
        const QDir src(source.path());

        // The same effect twice (copied folder): used once, the copy listed as a duplicate.
        writeShaderItem(src.filePath(QStringLiteral("Filter/3_Wobble")), 3001, 3, QStringLiteral("Wobble"),
                        QString(), kFilterFrag, filterConf(true), false);
        writeShaderItem(src.filePath(QStringLiteral("Filter - Copy/3_Wobble")), 3001, 3, QStringLiteral("Wobble"),
                        QString(), kFilterFrag, filterConf(true), false);
        // A Filmora-style folder without resinfo.json: the type comes from filter.xml.
        writeShaderItem(src.filePath(QStringLiteral("misc/2_Soft_Fade")), 0, 0, QString(), QString(),
                        "vec4 FUNCNAME(vec2 tc) { return mix(INPUT1(tc), INPUT2(tc), PREFIX(progress)); }\n",
                        filterConf(false), true);
        QVERIFY(QFile::remove(src.filePath(QStringLiteral("misc/2_Soft_Fade/resinfo.json"))));

        // Loose files: a Fonts folder (one face twice), a sticker in a named folder, twice.
        copyFile(repoFont(QStringLiteral("noto-serif/NotoSerif-Regular.ttf")), src.filePath(QStringLiteral("Fonts/NotoSerif-Regular.ttf")));
        copyFile(repoFont(QStringLiteral("noto-serif/NotoSerif-Bold.ttf")), src.filePath(QStringLiteral("Fonts/NotoSerif-Bold.ttf")));
        copyFile(repoFont(QStringLiteral("noto-serif/NotoSerif-Bold.ttf")), src.filePath(QStringLiteral("Fonts/old/NotoSerif-Bold copy.ttf")));
        writeFile(src.filePath(QStringLiteral("Arrows/arrow_up.webm")), "same video bytes");
        writeFile(src.filePath(QStringLiteral("Arrows/arrow_up (1).webm")), "same video bytes");
        writeFile(src.filePath(QStringLiteral("notes.txt")), "ignored");

        const ImportReport report = importFilmoraFolder(source.path(), out.path());
        QVERIFY(report.error.isEmpty());
        QCOMPARE(report.effects, 1);
        QCOMPARE(report.transitions, 1);
        QCOMPARE(report.stickers, 1);
        QCOMPARE(report.fonts, 1);
        int duplicates = 0;
        for (const ImportedItem &item : report.items) {
            if (item.skipReason.startsWith(QLatin1String("Duplicate of")))
                ++duplicates;
        }
        QCOMPARE(duplicates, 2); // the copied effect folder and the copied sticker

        const QDir o(out.path());
        QVERIFY(QFileInfo::exists(o.filePath(QStringLiteral("effects/fm-3001/effect.json"))));
        const QFileInfoList transitions =
            QDir(o.filePath(QStringLiteral("transitions"))).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
        QCOMPARE(transitions.size(), 1);
        const QJsonObject fade = readJson(QDir(transitions.first().filePath()).filePath(QStringLiteral("transition.json")));
        QCOMPARE(fade.value(QStringLiteral("displayName")).toString(), QStringLiteral("Soft Fade"));
        QCOMPARE(fade.value(QStringLiteral("category")).toString(), QStringLiteral("basic"));

        const QFileInfoList fontDirs =
            QDir(o.filePath(QStringLiteral("fonts"))).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
        QCOMPARE(fontDirs.size(), 1);
        const QJsonObject family = readJson(QDir(fontDirs.first().filePath()).filePath(QStringLiteral("family.json")));
        QCOMPARE(family.value(QStringLiteral("family")).toString(), QStringLiteral("Noto Serif"));
        QCOMPARE(family.value(QStringLiteral("category")).toString(), QStringLiteral("editorial"));
        QCOMPARE(family.value(QStringLiteral("faces")).toArray().size(), 2); // Regular and Bold, Bold once

        const QJsonObject pack = readJson(o.filePath(QStringLiteral("stickers/linked-%1/pack.json").arg(o.dirName())));
        QCOMPARE(pack.value(QStringLiteral("stickers")).toArray().size(), 1);
        QCOMPARE(pack.value(QStringLiteral("categories")).toArray().at(0).toObject()
                     .value(QStringLiteral("label")).toString(),
                 QStringLiteral("Arrows"));
    }

    void aScrambledLibraryStillSorts()
    {
        QTemporaryDir source;
        QTemporaryDir out;
        const QDir src(source.path());

        // Everything in one folder, one item even inside another item's folder.
        const QString title = src.filePath(QStringLiteral("mixed/1_Cartoon"));
        writeResinfo(title, 7001, 1, QStringLiteral("Cartoon Title"));
        writeFile(QDir(title).filePath(QStringLiteral("Data/data.json")), "{\"projectName\":\"TlbTitleClip\"}");
        writeShaderItem(QDir(title).filePath(QStringLiteral("2_Mix")), 7002, 2, QStringLiteral("Soft Mix"),
                        QStringLiteral("Basic"),
                        "vec4 FUNCNAME(vec2 tc) { return mix(INPUT1(tc), INPUT2(tc), PREFIX(progress)); }\n",
                        filterConf(false), true);
        // A split screen next to them: one folder per aspect ratio.
        const QString split = src.filePath(QStringLiteral("mixed/5_Split"));
        writeResinfo(split, 7003, 5, QStringLiteral("Split Screen 1"));
        writeFile(QDir(split).filePath(QStringLiteral("16_9/Data/split.svg")), "<svg/>");
        // A sticker that lost its resinfo.json, and one Filmora filed as a filter.
        writeFile(src.filePath(QStringLiteral("mixed/7_Arrow/Data/arrow.webm")), "arrow video");
        writeFile(src.filePath(QStringLiteral("mixed/7_Arrow/Data/filter.xml")), QByteArray());
        QVERIFY(QFile::remove(src.filePath(QStringLiteral("mixed/7_Arrow/Data/filter.xml"))));
        writeResinfo(src.filePath(QStringLiteral("mixed/7_Arrow")), 0, 0, QString());
        const QString misfiled = src.filePath(QStringLiteral("mixed/3_Star"));
        writeResinfo(misfiled, 7005, 3, QStringLiteral("Star Burst"), QStringLiteral("Shapes"));
        writeFile(QDir(misfiled).filePath(QStringLiteral("Data/star.webm")), "star video");

        const ImportReport report = importFilmoraFolder(source.path(), out.path());
        QCOMPARE(report.transitions, 1); // found inside the title's folder, not swallowed by it
        QCOMPARE(report.stickers, 2);    // the one without resinfo.json, and the misfiled one
        QCOMPARE(report.effects, 0);
        int titles = 0;
        int splits = 0;
        for (const ImportedItem &item : report.items) {
            titles += item.skipReason.startsWith(QLatin1String("Filmora title")) ? 1 : 0;
            splits += item.skipReason.startsWith(QLatin1String("Split screen")) ? 1 : 0;
        }
        QCOMPARE(titles, 1);
        QCOMPARE(splits, 1);
    }

    // A Filmora title becomes a Dafin title template: text, font, colours, outline, shadow,
    // position, animations, its converted Filmora filter as a clip effect, Glow as a glow layer.
    void filmoraTitleBecomesADafinTitle()
    {
        QTemporaryDir source;
        QTemporaryDir out;
        const QDir src(source.path());
        writeShaderItem(src.filePath(QStringLiteral("Filter/3_Wobble")), 9001, 3, QStringLiteral("Wobble"),
                        QString(), kFilterFrag, filterConf(false), false);
        const QString title = src.filePath(QStringLiteral("title/1_Neon"));
        writeTitleItem(title, 9100, QStringLiteral("Neon Lower"), QStringLiteral("HELLO"),
                       QJsonArray{titleEffect(QStringLiteral("Wobble"), 9001, 3,
                                              QJsonObject{{QStringLiteral("speed"), 25.0}, {QStringLiteral("steps"), 3}}),
                                  titleEffect(QStringLiteral("Glow"), 38912, 3,
                                              QJsonObject{{QStringLiteral("radius"), 5}, {QStringLiteral("alpha"), 50}}),
                                  titleEffect(QStringLiteral("SparkleFX"), 123, 3, QJsonObject{})},
                       QJsonObject{{QStringLiteral("inAnimation"),
                                    QJsonObject{{QStringLiteral("name"), QStringLiteral("Center Gradually Appears")},
                                                {QStringLiteral("duration"), 5000000}}},
                                   {QStringLiteral("outAnimation"),
                                    QJsonObject{{QStringLiteral("name"), QStringLiteral("Slide Left")},
                                                {QStringLiteral("duration"), 3000000}}}});
        copyFile(repoFont(QStringLiteral("lato/Lato-Bold.ttf")), QDir(title).filePath(QStringLiteral("Font/1_Lato.ttf")));

        const ImportReport report = importFilmoraFolder(source.path(), out.path());
        QCOMPARE(report.titles, 1);
        QCOMPARE(report.effects, 1);
        QVERIFY(report.changedKinds.contains(QStringLiteral("text-styles")));
        const QJsonObject t = readJson(QDir(out.path()).filePath(QStringLiteral("text-styles/fm-9100.json")));
        QCOMPARE(t.value(QStringLiteral("format")).toInt(), 1);
        QCOMPARE(t.value(QStringLiteral("id")).toString(), QStringLiteral("filmora.9100"));
        QCOMPARE(t.value(QStringLiteral("label")).toString(), QStringLiteral("Neon Lower"));
        QCOMPARE(t.value(QStringLiteral("sampleText")).toString(), QStringLiteral("HELLO"));
        QCOMPARE(t.value(QStringLiteral("category")).toString(), QStringLiteral("Social Media"));

        const QJsonObject style = t.value(QStringLiteral("style")).toObject();
        QCOMPARE(style.value(QStringLiteral("fontFamily")).toString(), QStringLiteral("Lato")); // from the font file
        QCOMPARE(style.value(QStringLiteral("fontWeight")).toInt(), 700);
        QVERIFY(style.value(QStringLiteral("pixelSize")).isDouble());
        QCOMPARE(style.value(QStringLiteral("pixelSize")).toDouble(), 120.0); // a whole number
        QCOMPARE(style.value(QStringLiteral("valign")).toString(), QStringLiteral("bottom"));
        QStringList ids;
        QHash<QString, QJsonObject> layers;
        for (const QJsonValue &v : style.value(QStringLiteral("layers")).toArray()) {
            ids.append(v.toObject().value(QStringLiteral("id")).toString());
            layers.insert(ids.last(), v.toObject());
        }
        QCOMPARE(ids, (QStringList{QStringLiteral("shadow"), QStringLiteral("glow"), QStringLiteral("outline"),
                                   QStringLiteral("fill")}));
        auto colorOf = [&](const QString &id) {
            return layers.value(id).value(QStringLiteral("paint")).toObject().value(QStringLiteral("color")).toString();
        };
        QCOMPARE(colorOf(QStringLiteral("fill")), QStringLiteral("#ff102030"));
        QCOMPARE(colorOf(QStringLiteral("outline")), QStringLiteral("#ffff0000"));
        QCOMPARE(layers.value(QStringLiteral("outline")).value(QStringLiteral("width")).toDouble(), 6.0);
        QVERIFY(layers.value(QStringLiteral("shadow")).value(QStringLiteral("offsetY")).toDouble() > 0.0);

        const QJsonArray effects = t.value(QStringLiteral("effects")).toArray();
        QCOMPARE(effects.size(), 1);
        const QJsonObject wobble = effects.at(0).toObject();
        QCOMPARE(wobble.value(QStringLiteral("effect")).toString(), QStringLiteral("filmora.9001"));
        const QJsonObject params = wobble.value(QStringLiteral("parameters")).toObject();
        QCOMPARE(params.value(QStringLiteral("fm_speed")).toDouble(), 10.0); // 25 clamped to its range
        QCOMPARE(params.value(QStringLiteral("fm_steps")).toDouble(), 3.0);
        QVERIFY(style.value(QStringLiteral("effectPadding")).toDouble() > 0.0);

        const QJsonObject animation = style.value(QStringLiteral("animation")).toObject();
        const QJsonObject in = animation.value(QStringLiteral("in")).toObject();
        QCOMPARE(in.value(QStringLiteral("preset")).toString(), QStringLiteral("fade"));
        QCOMPARE(in.value(QStringLiteral("params")).toObject().value(QStringLiteral("order")).toString(),
                 QStringLiteral("centerOut"));
        // Letter by letter: each letter keeps its pace, the whole entrance fits Filmora's 0.5 s.
        QVERIFY(!in.value(QStringLiteral("params")).toObject().contains(QStringLiteral("duration")));
        QCOMPARE(animation.value(QStringLiteral("timing")).toObject().value(QStringLiteral("maxReadMs")).toDouble(), 500.0);
        const QJsonObject outAnim = animation.value(QStringLiteral("out")).toObject();
        QCOMPARE(outAnim.value(QStringLiteral("preset")).toString(), QStringLiteral("slide-left"));
        QCOMPARE(outAnim.value(QStringLiteral("params")).toObject().value(QStringLiteral("ease")).toString(),
                 QStringLiteral("easeIn"));

        // Added, with what was left out named.
        const ImportedItem *item = nullptr;
        for (const ImportedItem &i : report.items) {
            if (i.kind == QLatin1String("title"))
                item = &i;
        }
        QVERIFY(item);
        QVERIFY(item->skipReason.contains(QLatin1String("SparkleFX")));
        QVERIFY(!item->skipReason.contains(QLatin1String("Glow")));

        // The filter gone from the folder: the title is converted again, without it.
        QVERIFY(QDir(src.filePath(QStringLiteral("Filter"))).removeRecursively());
        const ImportReport again = importFilmoraFolder(source.path(), out.path());
        QCOMPARE(again.titles, 1);
        const QJsonObject t2 = readJson(QDir(out.path()).filePath(QStringLiteral("text-styles/fm-9100.json")));
        QVERIFY(t2.value(QStringLiteral("effects")).toArray().isEmpty());

        // Gone altogether: its template goes too (a sticker stays, so the folder is not empty,
        // which would read as a drive that is not connected).
        writeFile(src.filePath(QStringLiteral("Stickers/star.png")), "png");
        QVERIFY(QDir(title).removeRecursively());
        importFilmoraFolder(source.path(), out.path());
        QVERIFY(!QFileInfo::exists(QDir(out.path()).filePath(QStringLiteral("text-styles/fm-9100.json"))));
    }

    // An older Filmora title keeps its text in Data/data.xml: a <TimeLine> whose
    // Text.Section.CaptionData property is an escaped <NLETitleArray>, text and font names as
    // UTF-16BE hex, colours as COLORREFs (0x00BBGGRR).
    void filmoraXmlTitleBecomesADafinTitle()
    {
        auto hex = [](const QString &s) {
            QString out;
            for (const QChar c : s)
                out += QStringLiteral("%1").arg(uint(c.unicode()), 4, 16, QLatin1Char('0'));
            return out.toUpper();
        };
        QCOMPARE(decodeFilmoraHexText(QStringLiteral("0054006500780074")), QStringLiteral("Text"));
        QCOMPARE(decodeFilmoraHexText(QStringLiteral("Arial")), QStringLiteral("Arial"));
        QCOMPARE(decodeFilmoraHexText(QStringLiteral("12345")), QStringLiteral("12345")); // not UTF-16 hex

        const QString caption =
            QStringLiteral("<NLETitleArray><NLETitle><TextData Align=\"1\" OffsetX=\"0\" OffsetY=\"0\">"
                           "<CharData Title=\"%1\"/></TextData>"
                           "<Font FontName=\"%2\" Size=\"120\" Bold=\"1\" Italic=\"0\"/>"
                           "<Face Color1=\"%3\"/>"
                           "<Border Enable=\"1\" Size=\"6\" Color1=\"%4\"/>"
                           "<Shadow Enable=\"1\" Color=\"0\" Alpha=\"128\" BlurRadius=\"5\" Distance=\"2\" Direction=\"3\"/>"
                           "<Gradient Enable=\"0\"/><Background_01 Enable=\"1\" Color1=\"%5\" Alpha=\"128\"/>"
                           "</NLETitle></NLETitleArray>")
                .arg(hex(QStringLiteral("ART TITLE")), hex(QStringLiteral("Lato Bold")))
                .arg(0x302010)  // COLORREF of #102030
                .arg(0x0000ff)  // COLORREF of red
                .arg(0xff0000); // COLORREF of blue
        QString escaped = caption.toHtmlEscaped();
        const QByteArray xml =
            QStringLiteral("<?xml version=\"1.0\"?><TimeLine>"
                           "<Property Name=\"Render.Framerate\" Value=\"25/1\"/>"
                           "<Property Name=\"Render.TotalFrameCount\" Value=\"125\"/>"
                           "<Property Name=\"Transform.CenterPoint\" Value=\"0.5,0.85\"/>"
                           "<Property Name=\"Text.Section.CaptionData\" Value=\"%1\"/>"
                           "</TimeLine>")
                .arg(escaped.replace(QLatin1Char('"'), QStringLiteral("&quot;")))
                .toUtf8();

        QTemporaryDir source;
        QTemporaryDir out;
        const QString title = QDir(source.path()).filePath(QStringLiteral("title/Art_Title_23"));
        writeResinfo(title, 9200, 1, QStringLiteral("Art Title 23"), QStringLiteral("Art"));
        writeFile(QDir(title).filePath(QStringLiteral("Data/data.xml")), xml);
        copyFile(repoFont(QStringLiteral("lato/Lato-Bold.ttf")), QDir(title).filePath(QStringLiteral("Font/1_Lato Bold.ttf")));
        // Filmora's "All/" copy of the same files is part of the item, not a second title.
        writeFile(QDir(title).filePath(QStringLiteral("All/Data/data.xml")), xml);

        const ImportReport report = importFilmoraFolder(source.path(), out.path());
        QCOMPARE(report.titles, 1);
        const QJsonObject t = readJson(QDir(out.path()).filePath(QStringLiteral("text-styles/fm-9200.json")));
        QCOMPARE(t.value(QStringLiteral("label")).toString(), QStringLiteral("Art Title 23"));
        QCOMPARE(t.value(QStringLiteral("sampleText")).toString(), QStringLiteral("ART TITLE"));
        QCOMPARE(t.value(QStringLiteral("sourceDurationMs")).toDouble(), 5000.0); // 125 frames at 25 fps
        const QJsonObject style = t.value(QStringLiteral("style")).toObject();
        QCOMPARE(style.value(QStringLiteral("fontFamily")).toString(), QStringLiteral("Lato"));
        QCOMPARE(style.value(QStringLiteral("fontWeight")).toInt(), 700);
        QCOMPARE(style.value(QStringLiteral("pixelSize")).toDouble(), 120.0);
        QCOMPARE(style.value(QStringLiteral("valign")).toString(), QStringLiteral("bottom"));
        QCOMPARE(style.value(QStringLiteral("boxEnabled")).toBool(), true);
        QCOMPARE(style.value(QStringLiteral("boxColor")).toString(), QStringLiteral("#800000ff"));
        QHash<QString, QJsonObject> layers;
        for (const QJsonValue &v : style.value(QStringLiteral("layers")).toArray())
            layers.insert(v.toObject().value(QStringLiteral("id")).toString(), v.toObject());
        const auto colorOf = [&](const QString &id) {
            return layers.value(id).value(QStringLiteral("paint")).toObject().value(QStringLiteral("color")).toString();
        };
        QCOMPARE(colorOf(QStringLiteral("fill")), QStringLiteral("#ff102030"));
        QCOMPARE(colorOf(QStringLiteral("outline")), QStringLiteral("#ffff0000"));
        QCOMPARE(layers.value(QStringLiteral("outline")).value(QStringLiteral("width")).toDouble(), 6.0);
        QVERIFY(layers.contains(QStringLiteral("shadow")));
        QCOMPARE(layers.value(QStringLiteral("shadow")).value(QStringLiteral("opacity")).toDouble(), 128.0 / 255.0);

        // The same caption as element text, the property as a child element.
        const QByteArray xml2 =
            QStringLiteral("<TimeLine><Render><Framerate>30/1</Framerate><TotalFrameCount>60</TotalFrameCount></Render>"
                           "<Text.Section.CaptionData>%1</Text.Section.CaptionData></TimeLine>")
                .arg(caption.toHtmlEscaped())
                .toUtf8();
        QString why;
        const QJsonObject root = QJsonDocument::fromJson(filmoraXmlTitleAsJson(xml2, &why)).object();
        QVERIFY2(!root.isEmpty(), qPrintable(why));
        QCOMPARE(root.value(QStringLiteral("tlEnd")).toDouble(), 2.0 * 10'000'000.0);
        QVERIFY(filmoraXmlTitleAsJson("<TimeLine/>", &why).isEmpty());
        QVERIFY(!why.isEmpty());
    }

    // A video title (a colour video and its green-on-black matte) is named, not turned into an
    // empty text title.
    void filmoraVideoTitleSaysWhy()
    {
        QTemporaryDir source;
        QTemporaryDir out;
        const QString title = QDir(source.path()).filePath(QStringLiteral("title/Vlog_Title_02"));
        writeResinfo(title, 9300, 1, QStringLiteral("Vlog Title 02"));
        writeFile(QDir(title).filePath(QStringLiteral("Data/data.xml")), "<TimeLine/>");
        writeFile(QDir(title).filePath(QStringLiteral("Data/Vlog.mp4")), "not really a video");
        writeFile(QDir(title).filePath(QStringLiteral("Data/Vlog_L0.mp4")), "not really a video");
        const ImportReport report = importFilmoraFolder(source.path(), out.path());
        QCOMPARE(report.titles, 0);
        QCOMPARE(report.stickers, 0); // not its colour video with a black background
        bool named = false;
        for (const ImportedItem &item : report.items)
            named = named || item.skipReason.contains(QStringLiteral("video title"));
        QVERIFY(named);
    }

    void filmoraTitleNamesMapToDafin()
    {
        QCOMPARE(filmoraColor(0x102030), QStringLiteral("#ff102030"));
        QCOMPARE(filmoraColor(0xee7f35), QStringLiteral("#ffee7f35")); // an orange title, not blue
        QCOMPARE(filmoraColor(0xffffff, 128), QStringLiteral("#80ffffff"));
        const TitleFont bold = splitFontName(QStringLiteral("Montserrat Extra Bold Italic"));
        QCOMPARE(bold.family, QStringLiteral("Montserrat"));
        QCOMPARE(bold.weight, 800);
        QVERIFY(bold.italic);
        QCOMPARE(splitFontName(QStringLiteral("Paytone One Regular")).family, QStringLiteral("Paytone One"));
        QCOMPARE(splitFontName(QStringLiteral("Bernard MT Condensed")).family, QStringLiteral("Bernard MT Condensed"));
        QJsonObject params;
        QCOMPARE(dafinAnimationFor(QStringLiteral("Typewriter"), QStringLiteral("in"), &params), QStringLiteral("typewriter"));
        QCOMPARE(dafinAnimationFor(QStringLiteral("Zoom Out"), QStringLiteral("out"), &params), QStringLiteral("shrink"));
        QCOMPARE(dafinAnimationFor(QStringLiteral("Drop In"), QStringLiteral("out"), &params), QStringLiteral("fall"));
        QCOMPARE(dafinAnimationFor(QStringLiteral("Stamp"), QStringLiteral("out"), &params), QStringLiteral("fade-scale"));
        QCOMPARE(dafinAnimationFor(QStringLiteral("Swing"), QStringLiteral("loop"), &params), QStringLiteral("swing"));
        QCOMPARE(dafinAnimationFor(QStringLiteral("Vertical Rotation Appears"), QStringLiteral("in"), &params),
                 QStringLiteral("spin-in"));
        QJsonObject verbatim;
        QCOMPARE(dafinAnimationFor(QStringLiteral("Appearing Verbatim"), QStringLiteral("in"), &verbatim), QStringLiteral("fade"));
        QCOMPARE(verbatim.value(QStringLiteral("unit")).toString(), QStringLiteral("character"));
        QVERIFY(dafinAnimationFor(QStringLiteral("Mystery"), QStringLiteral("loop"), &params).isEmpty());
        QCOMPARE(dafinAnimationFor(QStringLiteral("Mystery"), QStringLiteral("in"), &params), QStringLiteral("fade"));

        // By Filmora's preset id first: the same id is the entrance and the exit.
        QJsonObject byId;
        QCOMPARE(dafinAnimationForId(QStringLiteral("MG_2d_34"), QStringLiteral("in"), &byId), QStringLiteral("scatter-in"));
        QCOMPARE(dafinAnimationForId(QStringLiteral("MG_2d_02"), QStringLiteral("out"), &byId), QStringLiteral("leap-in"));
        byId = {};
        QCOMPARE(dafinAnimationForId(QStringLiteral("MG_2d_22"), QStringLiteral("in"), &byId), QStringLiteral("pop"));
        QCOMPARE(byId.value(QStringLiteral("from")).toDouble(), 200.0); // starts enlarged
        QCOMPARE(dafinAnimationForId(QStringLiteral("8C3B822D-8C59-4A7B-AF96-56E280CFB7C0"), QStringLiteral("in"), &byId),
                 QStringLiteral("slide-down"));
        QVERIFY(dafinAnimationForId(QStringLiteral("MG_2d_99"), QStringLiteral("in"), &byId).isEmpty());
        QVERIFY(dafinAnimationForId(QStringLiteral("MG_2d_09"), QStringLiteral("loop"), &byId).isEmpty());
        QCOMPARE(dafinAnimationFor(QStringLiteral("Right Dir Insert"), QStringLiteral("in"), &params), QStringLiteral("slide-right"));
        QCOMPARE(dafinAnimationFor(QStringLiteral("Evaporate"), QStringLiteral("in"), &params), QStringLiteral("blur-in"));

        // A title whose animation is in the clip's "animation" field (inRatio of the clip), and one
        // whose "animation" is Filmora's nameless "none".
        auto titleWith = [](const QJsonObject &clipFields) {
            const QJsonObject script{{QStringLiteral("Text"), QStringLiteral("Hi")},
                                     {QStringLiteral("TextData"), QJsonArray{QJsonObject{{QStringLiteral("CharData"), QStringLiteral("Hi")}}}}};
            QJsonObject clip{{QStringLiteral("type"), 4}, {QStringLiteral("tlBegin"), 0}, {QStringLiteral("tlEnd"), 40000000},
                             {QStringLiteral("scriptBuf"), QString::fromUtf8(QJsonDocument(script).toJson(QJsonDocument::Compact))}};
            for (auto it = clipFields.constBegin(); it != clipFields.constEnd(); ++it)
                clip.insert(it.key(), it.value());
            const QJsonObject root{{QStringLiteral("subTimeline"),
                                    QJsonObject{{QStringLiteral("timelineInfos"),
                                                 QJsonArray{QJsonObject{{QStringLiteral("trackInfos"),
                                                                         QJsonArray{QJsonObject{{QStringLiteral("clipList"), QJsonArray{clip}}}}}}}}}}};
            return convertFilmoraTitle(QJsonDocument(root).toJson(), QStringLiteral("filmora.2"), QStringLiteral("T"), QString())
                .templateJson.value(QStringLiteral("style")).toObject().value(QStringLiteral("animation")).toObject();
        };
        const QJsonObject legacy = titleWith({{QStringLiteral("animation"),
                                               QJsonObject{{QStringLiteral("name"), QStringLiteral("Fade1")},
                                                           {QStringLiteral("type"), 4},
                                                           {QStringLiteral("stringParam"), QStringLiteral("0CAA397E-6EB2-4e51-9DF7-B7DDB338EB22")},
                                                           {QStringLiteral("inRatio"), 0.1}, {QStringLiteral("outRatio"), 0.0}}}});
        QCOMPARE(legacy.value(QStringLiteral("in")).toObject().value(QStringLiteral("preset")).toString(), QStringLiteral("fade"));
        QCOMPARE(legacy.value(QStringLiteral("in")).toObject().value(QStringLiteral("params")).toObject()
                     .value(QStringLiteral("duration")).toDouble(), 0.4); // a tenth of 4 s
        QVERIFY(!legacy.contains(QStringLiteral("out")));
        const QJsonObject none = titleWith({{QStringLiteral("animation"),
                                             QJsonObject{{QStringLiteral("name"), QString()}, {QStringLiteral("type"), 2},
                                                         {QStringLiteral("stringParam"), QStringLiteral("0A6EE4AB-01BC-4D82-AA52-03FFA6F708DC")},
                                                         {QStringLiteral("inRatio"), 0.0}}}});
        QVERIFY(none.isEmpty());
        const QJsonObject scattered = titleWith({{QStringLiteral("inAnimation"),
                                                  QJsonObject{{QStringLiteral("name"), QStringLiteral("Scatter and Fly in")},
                                                              {QStringLiteral("type"), 5}, {QStringLiteral("stringParam"), QStringLiteral("MG_2d_34")},
                                                              {QStringLiteral("duration"), 15000000}}}});
        QCOMPARE(scattered.value(QStringLiteral("in")).toObject().value(QStringLiteral("preset")).toString(), QStringLiteral("scatter-in"));
        QCOMPARE(scattered.value(QStringLiteral("timing")).toObject().value(QStringLiteral("maxReadMs")).toDouble(), 1500.0);

        // A title with no text (shapes only) is listed, not made into an empty title.
        const TitleConversion empty = convertFilmoraTitle("{\"projectName\":\"TlbTitleClip\"}", QStringLiteral("filmora.1"),
                                                          QStringLiteral("Empty"), QString());
        QVERIFY(!empty.ok);
        QVERIFY(!empty.error.isEmpty());
    }

    // Filmora keeps copies of an item's files in folders such as All/Data or 9/Data, with no
    // resinfo.json of their own. They used to become extra effects named "All" and "9" with no
    // picture; they are part of the item.
    void variantFoldersArePartOfTheirItem()
    {
        QTemporaryDir source;
        QTemporaryDir out;
        const QDir src(source.path());
        auto variant = [](const QString &dir) {
            writeShaderItem(dir, 1, 3, QStringLiteral("x"), QString(), kFilterFrag, filterConf(false), false);
            QVERIFY(QFile::remove(QDir(dir).filePath(QStringLiteral("resinfo.json"))));
        };

        // Shader in its own Data/ and copied into All/ and 9/.
        const QString wobble = src.filePath(QStringLiteral("Filter/3_Wobble"));
        writeShaderItem(wobble, 5001, 3, QStringLiteral("Wobble"), QString(), kFilterFrag,
                        filterConf(false), false);
        variant(QDir(wobble).filePath(QStringLiteral("All")));
        variant(QDir(wobble).filePath(QStringLiteral("9")));
        // Shader only under All/Data: the item uses it, with its own name and thumbnail.
        const QString glow = src.filePath(QStringLiteral("Overlay/4_Glow"));
        writeResinfo(glow, 5002, 4, QStringLiteral("Soft Glow"));
        writeFile(QDir(glow).filePath(QStringLiteral("thumbnail.png")), "png");
        variant(QDir(glow).filePath(QStringLiteral("All")));
        // No resinfo.json anywhere: named after the folder above "All".
        variant(src.filePath(QStringLiteral("Extra/Neon Haze/All")));

        const ImportReport report = importFilmoraFolder(source.path(), out.path());
        QCOMPARE(report.effects, 3);
        QStringList names;
        for (const ImportedItem &item : report.items) {
            if (item.kind == QLatin1String("effect"))
                names.append(item.name);
        }
        names.sort();
        QCOMPARE(names, (QStringList{QStringLiteral("Neon Haze"), QStringLiteral("Soft Glow"),
                                     QStringLiteral("Wobble")}));
        const QJsonObject glowPkg =
            readJson(QDir(out.path()).filePath(QStringLiteral("effects/fm-5002/effect.json")));
        QCOMPARE(glowPkg.value(QStringLiteral("displayName")).toString(), QStringLiteral("Soft Glow"));
        QCOMPARE(glowPkg.value(QStringLiteral("thumbnail")).toString(), QStringLiteral("thumbnail.png"));
    }

    void anEmptyFolderKeepsWhatWasConverted()
    {
        QTemporaryDir source;
        QTemporaryDir out;
        writeShaderItem(QDir(source.path()).filePath(QStringLiteral("Filter/3_Wobble")), 4001, 3,
                        QStringLiteral("Wobble"), QString(), kFilterFrag, filterConf(false), false);
        QCOMPARE(importFilmoraFolder(source.path(), out.path()).effects, 1);

        // The drive behind a mount point is gone: the folder is there, but empty.
        QVERIFY(QDir(QDir(source.path()).filePath(QStringLiteral("Filter"))).removeRecursively());
        const ImportReport report = importFilmoraFolder(source.path(), out.path());
        QVERIFY(!report.error.isEmpty());
        QVERIFY(report.changedKinds.isEmpty());
        QVERIFY(QFileInfo::exists(QDir(out.path()).filePath(QStringLiteral("effects/fm-4001/effect.json"))));

        // A folder that is not there at all: the same.
        QVERIFY(!importFilmoraFolder(QDir(source.path()).filePath(QStringLiteral("nope")), out.path()).error.isEmpty());
    }

    void anItemThatCrashedTheCheckIsLeftOut()
    {
        QTemporaryDir source;
        QTemporaryDir out;
        writeShaderItem(QDir(source.path()).filePath(QStringLiteral("Filter/3_Wobble")), 5001, 3,
                        QStringLiteral("Wobble"), QString(), kFilterFrag, filterConf(false), false);
        // Left behind by a check that never returned (the driver took Dafin down).
        writeFile(QDir(out.path()).filePath(QStringLiteral("checking.txt")), "Filter/3_Wobble");
        bool checked = false;
        const ShaderCheck check = [&](const QString &, QString *) {
            checked = true;
            return true;
        };
        const ImportReport report = importFilmoraFolder(source.path(), out.path(), check);
        QCOMPARE(report.effects, 0);
        QVERIFY(!checked);
        QVERIFY(report.items.at(0).skipReason.contains(QLatin1String("closed")));
        QVERIFY(!QFileInfo::exists(QDir(out.path()).filePath(QStringLiteral("checking.txt"))));
    }

    void damagedFilesSkipOnlyTheirItem()
    {
        QTemporaryDir source;
        QTemporaryDir out;
        const QDir src(source.path());
        writeShaderItem(src.filePath(QStringLiteral("Filter/3_Good")), 6001, 3, QStringLiteral("Good"),
                        QString(), kFilterFrag, filterConf(false), false);
        // A damaged conf, a damaged resinfo, a shader that is far too large.
        writeShaderItem(src.filePath(QStringLiteral("Filter/3_BadConf")), 6002, 3, QStringLiteral("Bad conf"),
                        QString(), kFilterFrag, filterConf(false), false);
        writeFile(src.filePath(QStringLiteral("Filter/3_BadConf/Data/Wobble.conf")), "{ \"PARAMS\": [ ");
        writeShaderItem(src.filePath(QStringLiteral("Filter/3_BadInfo")), 6003, 3, QStringLiteral("Bad info"),
                        QString(), kFilterFrag, filterConf(false), false);
        writeFile(src.filePath(QStringLiteral("Filter/3_BadInfo/resinfo.json")), "{ not json");
        writeShaderItem(src.filePath(QStringLiteral("Filter/3_Huge")), 6004, 3, QStringLiteral("Huge"),
                        QString(), kFilterFrag, filterConf(false), false);
        writeFile(src.filePath(QStringLiteral("Filter/3_Huge/Data/Wobble.frag")), QByteArray(3 * 1024 * 1024, 'x'));

        const ImportReport report = importFilmoraFolder(source.path(), out.path());
        // The good one, and the one whose resinfo.json is damaged (its type comes from the folder).
        QCOMPARE(report.effects, 2);
        QCOMPARE(report.skipped(), 2);
    }

    void aShaderTheDriverRejectsIsSkipped()
    {
        QTemporaryDir source;
        QTemporaryDir out;
        writeShaderItem(QDir(source.path()).filePath(QStringLiteral("Filter/3_Wobble")), 2001, 3,
                        QStringLiteral("Wobble"), QString(), kFilterFrag, filterConf(false), false);
        const ShaderCheck reject = [](const QString &, QString *error) {
            *error = QStringLiteral("ERROR: 0:12: 'foo' : undeclared identifier\nmore");
            return false;
        };
        const ImportReport report = importFilmoraFolder(source.path(), out.path(), reject);
        QCOMPARE(report.effects, 0);
        QCOMPARE(report.skipped(), 1);
        QVERIFY(report.items.at(0).skipReason.contains(QLatin1String("undeclared identifier")));
        QVERIFY(!report.items.at(0).skipReason.contains(QLatin1String("more")));
        // Nothing was assumed about this one, so nothing is added to the driver's words.
        QVERIFY(!report.items.at(0).skipReason.contains(QLatin1String("note:")));
    }

    void refusalNamesEveryCauseNotOnlyTheFirst()
    {
        const QByteArray frag =
            "uniform sampler2D grain;\n"
            "vec4 FUNCNAME(vec2 tc) {\n"
            "    float g = PREFIX(gloss) + PREFIX(sheen);\n"
            "    return INPUT(tc) * g + INPUT2(tc) + INPUT3(tc);\n"
            "}\n";
        const ShaderTranslation t = translateFilmoraShader(frag, filterConf(false), false);
        QVERIFY(!t.ok);
        QVERIFY(t.source.isEmpty());
        for (const char *word : {"gloss", "sheen", "INPUT2", "INPUT3", "grain"})
            QVERIFY2(t.error.contains(QLatin1String(word)), word);
        QCOMPARE(t.codes, (QStringList{QStringLiteral("renderer-values"), QStringLiteral("extra-inputs"),
                                       QStringLiteral("own-samplers")}));
        // INPUT and INPUT1 are the filter's own clip: never named as a problem.
        QVERIFY(!t.error.contains(QLatin1String("INPUT1")));
        QVERIFY(!t.error.contains(QLatin1String("INPUT,")));
    }

    void longListsInTheReasonAreShortened()
    {
        QByteArray frag = "vec4 FUNCNAME(vec2 tc) { float v = 0.0;\n";
        for (const char *name : {"aa", "bb", "cc", "dd", "ee", "ff"})
            frag += QByteArray("v += PREFIX(") + name + QByteArray(");\n");
        frag += "return INPUT(tc) * v; }\n";
        const ShaderTranslation t = translateFilmoraShader(frag, filterConf(false), false);
        QVERIFY(!t.ok);
        QVERIFY(t.error.contains(QLatin1String("aa, bb, cc, dd and 2 more")));
        QCOMPARE(t.codes, (QStringList{QStringLiteral("renderer-values")}));
    }

    void aNameUsedTwiceIsNamedOnce()
    {
        const QByteArray frag =
            "vec4 FUNCNAME(vec2 tc) { return INPUT3(tc) + INPUT3(tc * 2.0) + PREFIX(zz) * PREFIX(zz); }\n";
        const ShaderTranslation t = translateFilmoraShader(frag, filterConf(false), false);
        QVERIFY(!t.ok);
        QCOMPARE(t.error.count(QLatin1String("INPUT3")), 1);
        QCOMPARE(t.error.count(QLatin1String("zz")), 1);
    }

    void samplerWordsInCommentsAreNotRefused()
    {
        const QByteArray frag =
            "// uniform sampler2D grain;\n"
            "vec4 FUNCNAME(vec2 tc) { return INPUT(tc); } /* uniform sampler2D other; */\n";
        const ShaderTranslation t = translateFilmoraShader(frag, filterConf(false), false);
        QVERIFY2(t.ok, qPrintable(t.error));
        QVERIFY(t.codes.isEmpty());
    }

    void assumptionsBecomeNotes()
    {
        QJsonObject conf = filterConf(false);
        QJsonArray params = conf.value(QStringLiteral("PARAMS")).toArray();
        params.append(QJsonObject{{QStringLiteral("NAME"), QStringLiteral("grainMap")},
                                  {QStringLiteral("TYPE"), QStringLiteral("image")},
                                  {QStringLiteral("DEFAULT"), 0},
                                  {QStringLiteral("MIN"), 0},
                                  {QStringLiteral("MAX"), 1}});
        conf.insert(QStringLiteral("PARAMS"), params);
        ShaderTranslation t = translateFilmoraShader(kFilterFrag, conf, false);
        QVERIFY2(t.ok, qPrintable(t.error)); // still converts, as before
        QCOMPARE(t.notes.size(), 1);
        QVERIFY(t.notes.first().contains(QLatin1String("grainMap")));
        QVERIFY(t.notes.first().contains(QLatin1String("image")));

        // The notes are the ones in the plain shader's case: none.
        t = translateFilmoraShader(kFilterFrag, filterConf(false), false);
        QVERIFY(t.ok);
        QVERIFY(t.notes.isEmpty());

        const QByteArray old = "vec4 FUNCNAME(vec2 tc) { return texture2D(u_currentTexture, tc); }\n";
        t = translateFilmoraShader(old, filterConf(false), false);
        QVERIFY(t.ok);
        QVERIFY(t.notes.join(QLatin1Char(' ')).contains(QLatin1String("texture2D")));
    }

    void aCompileFailureSaysWhatWasAssumed()
    {
        QTemporaryDir source;
        QTemporaryDir out;
        QJsonObject conf = filterConf(false);
        QJsonArray params = conf.value(QStringLiteral("PARAMS")).toArray();
        params.append(QJsonObject{{QStringLiteral("NAME"), QStringLiteral("grainMap")},
                                  {QStringLiteral("TYPE"), QStringLiteral("image")},
                                  {QStringLiteral("DEFAULT"), 0},
                                  {QStringLiteral("MIN"), 0},
                                  {QStringLiteral("MAX"), 1}});
        conf.insert(QStringLiteral("PARAMS"), params);
        writeShaderItem(QDir(source.path()).filePath(QStringLiteral("Filter/3_Grain")), 2101, 3,
                        QStringLiteral("Grain"), QString(), kFilterFrag, conf, false);
        const ShaderCheck reject = [](const QString &, QString *error) {
            *error = QStringLiteral("ERROR: 0:9: no matching overload");
            return false;
        };
        const ImportReport report = importFilmoraFolder(source.path(), out.path(), reject);
        QCOMPARE(report.skipped(), 1);
        const QString reason = report.items.at(0).skipReason;
        QVERIFY(reason.contains(QLatin1String("no matching overload")));
        QVERIFY(reason.contains(QLatin1String("note:")));
        QVERIFY(reason.contains(QLatin1String("grainMap")));
    }
};

QTEST_GUILESS_MAIN(TestLinkedLibraries)
#include "tst_linkedlibraries.moc"
