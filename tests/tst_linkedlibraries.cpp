// Dafin: linked Filmora effect folders (FilmoraLibraryImporter). The fixtures are written here,
// in Filmora's layout but with made-up content, so no Filmora asset is kept in the repository.
#include "engine/packages/linked/FilmoraLibraryImporter.h"

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
    }
};

QTEST_GUILESS_MAIN(TestLinkedLibraries)
#include "tst_linkedlibraries.moc"
