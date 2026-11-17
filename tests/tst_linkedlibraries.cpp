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
        QCOMPARE(dafinCategoryFor(QStringLiteral("effects"), QStringLiteral("VFX Text"), QStringLiteral("Color Glitch")),
                 QStringLiteral("glitch"));
        QCOMPARE(dafinCategoryFor(QStringLiteral("effects"), QStringLiteral("Social Media"), QStringLiteral("Chat 07")),
                 QStringLiteral("Social Media"));
        QCOMPARE(dafinCategoryFor(QStringLiteral("transitions"), QStringLiteral("Basic"), QStringLiteral("Fade")),
                 QStringLiteral("basic"));
        QCOMPARE(dafinCategoryFor(QStringLiteral("effects"), QString(), QStringLiteral("Something")),
                 QStringLiteral("filmora"));
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

        const QString font = src.filePath(QStringLiteral("resFont/35_Advent"));
        writeResinfo(font, 1005, 35, QStringLiteral("Advent Pro ExtraBold"));
        writeFile(QDir(font).filePath(QStringLiteral("Data/AdventPro-ExtraBold.ttf")), "ttf");
        writeFile(QDir(font).filePath(QStringLiteral("All/Data/AdventPro-ExtraBold.ttf")), "ttf");

        const QString title = src.filePath(QStringLiteral("title/1_Cartoon"));
        writeResinfo(title, 1006, 1, QStringLiteral("3D Cartoon"), QStringLiteral("3D Titles"));
        writeFile(QDir(title).filePath(QStringLiteral("Data/data.json")), "{}");
        writeFile(QDir(title).filePath(QStringLiteral("Font/2223506_Paytone One.ttf")), "ttf");

        writeResinfo(src.filePath(QStringLiteral("resTextAnimation/27_Glow")), 1007, 27, QStringLiteral("Glow In"));
        writeResinfo(src.filePath(QStringLiteral("resImageToVideo/106_Angry")), 1008, 106, QStringLiteral("Angry"));

        ImportReport report = importFilmoraFolder(source.path(), out.path());
        QVERIFY(report.error.isEmpty());
        QCOMPARE(report.items.size(), 8);
        QCOMPARE(report.effects, 1);
        QCOMPARE(report.transitions, 1);
        QCOMPARE(report.stickers, 1);
        QCOMPARE(report.fonts, 2); // the font item, and the font inside the title
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

        bool sawPaytone = false;
        const QFileInfoList fontDirs =
            QDir(o.filePath(QStringLiteral("fonts"))).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
        QCOMPARE(fontDirs.size(), 2);
        for (const QFileInfo &dir : fontDirs) {
            const QJsonObject family = readJson(QDir(dir.filePath()).filePath(QStringLiteral("family.json")));
            if (family.value(QStringLiteral("family")).toString() == QLatin1String("Paytone One"))
                sawPaytone = true;
            if (family.value(QStringLiteral("family")).toString() == QLatin1String("Advent Pro ExtraBold"))
                QCOMPARE(family.value(QStringLiteral("faces")).toArray().at(0).toObject()
                             .value(QStringLiteral("weight")).toInt(), 800);
        }
        QVERIFY(sawPaytone);

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
