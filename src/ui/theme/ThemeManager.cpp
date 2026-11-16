#include "ui/theme/ThemeManager.h"

#include "core/base/ZipArchive.h"
#include "engine/platform/AndroidUri.h"
#include "ui/theme/ReducedMotion.h"

#include <algorithm>
#include <cmath>
#include <memory>

#include <QColor>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QQmlEngine>
#include <QtQml/qqml.h>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>

namespace {

constexpr auto kDarkResource = ":/themes/dark.json";
constexpr auto kLightResource = ":/themes/light.json";

struct BuiltinTheme {
    const char *id;
    const char *resource;
};

constexpr BuiltinTheme kBuiltinThemes[] = {
    {"dark", kDarkResource},
    {"light", kLightResource},
    {"graphite", ":/themes/graphite.json"},
    {"frost", ":/themes/frost.json"},
    {"filmora", ":/themes/filmora.json"},
};

QString builtinResource(const QString &id)
{
    for (const BuiltinTheme &theme : kBuiltinThemes) {
        if (id == QLatin1String(theme.id))
            return QString::fromLatin1(theme.resource);
    }
    return {};
}

bool isBuiltinId(const QString &id)
{
    return !builtinResource(id).isEmpty();
}

// A theme document is ~140 KB. These caps leave headroom and stop a zip
// entry from expanding without a bound the importer agreed to.
constexpr qint64 kMaxThemeFileBytes = 2 * 1024 * 1024;
constexpr qint64 kMaxThemeTotalBytes = 8 * 1024 * 1024;
constexpr int kMaxZipEntries = 64;

constexpr int kThemeSchemaVersion = ThemeManager::kSchemaVersion;

const QStringList kGroups = {
    QStringLiteral("palette"),
    QStringLiteral("semantic"),
    QStringLiteral("components"),
    QStringLiteral("metrics"),
    QStringLiteral("typography"),
    QStringLiteral("motion"),
    QStringLiteral("window"),
};

// Palette keys a starter export keeps: surfaces, text, accents and status colors,
// which drive most of the UI. Clip, curve and gizmo colors fall through to the base.
const QStringList kStarterPaletteKeys = {
    QStringLiteral("appBackground"),
    QStringLiteral("panelBackground"),
    QStringLiteral("panelBorder"),
    QStringLiteral("panelForeground"),
    QStringLiteral("panelAccent"),
    QStringLiteral("panelAccentForeground"),
    QStringLiteral("panelMuted"),
    QStringLiteral("panelSecondaryBg"),
    QStringLiteral("panelSecondaryBorder"),
    QStringLiteral("panelSecondaryForeground"),
    QStringLiteral("border"),
    QStringLiteral("foreground"),
    QStringLiteral("mutedForeground"),
    QStringLiteral("primary"),
    QStringLiteral("primaryForeground"),
    QStringLiteral("accent"),
    QStringLiteral("accentForeground"),
    QStringLiteral("accentOnPanel"),
    QStringLiteral("popoverHover"),
    QStringLiteral("constructive"),
    QStringLiteral("destructive"),
    QStringLiteral("warning"),
    QStringLiteral("overlay"),
    QStringLiteral("scrim"),
    QStringLiteral("scrimStrong"),
    QStringLiteral("skeleton"),
    QStringLiteral("skeletonHighlight"),
    QStringLiteral("scrollbarTrack"),
    QStringLiteral("scrollbarHandle"),
    QStringLiteral("scrollbarHandleHover"),
    QStringLiteral("scrollbarHandlePressed"),
    QStringLiteral("sliderTrack"),
    QStringLiteral("sheetHandle"),
    QStringLiteral("snapGuide"),
    QStringLiteral("exportGradientTop"),
    QStringLiteral("exportGradientBottom"),
    QStringLiteral("exportGlow"),
    QStringLiteral("previewLetterbox"),
};

// A reference chain longer than this is treated as a cycle even if the detector
// missed it. Themes are a few dozen tokens deep at most.
constexpr int kMaxReferenceDepth = 64;

QString userThemesDirectory()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
           + QStringLiteral("/themes");
}

QString &bundledRootStorage()
{
    static QString root = QStringLiteral(":/themes/bundled");
    return root;
}

bool isUnderBundledRoot(const QString &path)
{
    const QString root = bundledRootStorage();
    return !root.isEmpty() && path.startsWith(root + QLatin1Char('/'));
}

// qrc:/… for a file compiled into the app, file:/// otherwise.
QUrl themeFileUrl(const QString &path)
{
    if (path.startsWith(QLatin1Char(':')))
        return QUrl(QStringLiteral("qrc") + path);
    return QUrl::fromLocalFile(QFileInfo(path).absoluteFilePath());
}

// Dafin: the Appearance accent colour replaces the theme's primary colour (buttons,
// selection, focus ring, sliders: everything that reads semantic.primary), with a
// readable foreground on top of it.
void applyAccentOverride(QVariantMap *doc, const QString &color)
{
    const QColor accent(color);
    if (!accent.isValid())
        return;
    const double luminance = 0.2126 * accent.redF() + 0.7152 * accent.greenF() + 0.0722 * accent.blueF();
    const QString foreground = luminance > 0.55 ? QStringLiteral("#111111") : QStringLiteral("#ffffff");
    const QString value = accent.name(QColor::HexRgb);
    for (const char *group : {"palette", "semantic"}) {
        QVariantMap map = doc->value(QLatin1String(group)).toMap();
        map.insert(QStringLiteral("primary"), value);
        map.insert(QStringLiteral("primaryForeground"), foreground);
        doc->insert(QLatin1String(group), map);
    }
}

// A content:// document id has no suffix, and importLocal chooses json vs zip
// from the file name. Keep the provider's display name, and sniff when it has
// neither extension.
QString stagedThemeFileName(const QUrl &url, const QByteArray &data)
{
    QString name = AndroidUri::displayName(url);
    name.replace(QLatin1Char('/'), QLatin1Char('_'));
    name.replace(QLatin1Char('\\'), QLatin1Char('_'));
    name.replace(QLatin1Char(':'), QLatin1Char('_'));
    if (name.isEmpty() || name == QLatin1String(".") || name == QLatin1String(".."))
        name = QStringLiteral("theme");
    const QString suffix = QFileInfo(name).suffix().toLower();
    if (suffix != QLatin1String("json") && suffix != QLatin1String("zip")) {
        if (drift::zip::looksLikeZip(data.left(4)))
            name += QStringLiteral(".zip");
        else
            name += QStringLiteral(".json");
    }
    return name;
}

bool isBuiltinPath(const QString &path)
{
    for (const BuiltinTheme &theme : kBuiltinThemes) {
        if (path == QLatin1String(theme.resource))
            return true;
    }
    return false;
}

QString fallbackName(const QString &path)
{
    const QFileInfo info(path);
    if (info.fileName() == QLatin1String("theme.json"))
        return QFileInfo(info.path()).fileName();
    return info.completeBaseName();
}

QQmlPropertyMap *makeMap(QObject *parent)
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 11, 0)
    QQmlPropertyMap *map = QQmlPropertyMap::create(parent);
#else
    QQmlPropertyMap *map = new QQmlPropertyMap(parent);
#endif
    // The engine must not delete maps ThemeManager still owns. Bindings hold
    // the nested objects for the life of the theme manager.
    QQmlEngine::setObjectOwnership(map, QQmlEngine::CppOwnership);
    return map;
}

QQmlPropertyMap *asMap(const QVariant &value)
{
    if (auto *map = value.value<QQmlPropertyMap *>())
        return map;
    return qobject_cast<QQmlPropertyMap *>(value.value<QObject *>());
}

bool lookup(const QVariantMap &root, const QString &dotted, QVariant *out)
{
    const QStringList parts = dotted.split(QLatin1Char('.'), Qt::SkipEmptyParts);
    if (parts.isEmpty())
        return false;
    QVariant current = root;
    for (const QString &part : parts) {
        if (current.typeId() != QMetaType::QVariantMap)
            return false;
        const QVariantMap map = current.toMap();
        if (!map.contains(part))
            return false;
        current = map.value(part);
    }
    *out = current;
    return true;
}

QVariant lookupPath(const QVariantMap &root, const QString &dotted)
{
    QVariant value;
    if (!lookup(root, dotted, &value))
        return {};
    return value;
}

bool isColorLiteral(const QString &text)
{
    static const QRegularExpression re(
        QStringLiteral("^#(?:[0-9A-Fa-f]{3}|[0-9A-Fa-f]{4}|[0-9A-Fa-f]{6}|[0-9A-Fa-f]{8})$"));
    return re.match(text).hasMatch();
}

bool isReference(const QString &text)
{
    static const QRegularExpression re(
        QStringLiteral("^\\$[A-Za-z_][A-Za-z0-9_]*(?:\\.[A-Za-z_][A-Za-z0-9_]*)*$"));
    return re.match(text).hasMatch();
}

bool isValidSlotName(const QString &slot)
{
    static const QRegularExpression re(QStringLiteral("^[A-Za-z_][A-Za-z0-9_]*$"));
    return re.match(slot).hasMatch();
}

QVariantMap readObjectUncached(const QString &path, QString *error, bool warn);

QVariantMap readObject(const QString &path, QString *error, bool warn = true)
{
    // Dafin: a resource (":/…") never changes while the app runs, and dark.json is
    // ~140 KB parsed again under every theme read. Keep the parsed copy.
    if (!path.startsWith(QLatin1Char(':')))
        return readObjectUncached(path, error, warn);
    static QHash<QString, QVariantMap> cache;
    if (const auto it = cache.constFind(path); it != cache.cend())
        return *it;
    const QVariantMap doc = readObjectUncached(path, error, warn);
    if (error->isEmpty())
        cache.insert(path, doc);
    return doc;
}

QVariantMap readObjectUncached(const QString &path, QString *error, bool warn)
{
    QFile file(path);
    if (file.exists() && file.size() > kMaxThemeFileBytes) {
        *error = QStringLiteral("theme file is larger than %1 bytes: %2")
                     .arg(kMaxThemeFileBytes)
                     .arg(path);
        if (warn)
            qWarning("theme: %s", qPrintable(*error));
        return {};
    }
    if (!file.open(QIODevice::ReadOnly)) {
        *error = QStringLiteral("cannot open %1: %2").arg(path, file.errorString());
        if (warn)
            qWarning("theme: %s", qPrintable(*error));
        return {};
    }
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        const QString detail = parseError.error == QJsonParseError::NoError
                                   ? QStringLiteral("expected an object")
                                   : parseError.errorString();
        *error = QStringLiteral("invalid JSON in %1: %2").arg(path, detail);
        if (warn)
            qWarning("theme: %s", qPrintable(*error));
        return {};
    }
    return doc.object().toVariantMap();
}

QString readThemeName(const QString &path)
{
    // Name lookup walks every file in the themes directory. The caller reports
    // a real load failure; scanning must not warn about files it only peeks at.
    QString error;
    const QVariantMap doc = readObject(path, &error, false);
    if (!error.isEmpty())
        return {};
    const QVariant name = doc.value(QStringLiteral("name"));
    if (name.typeId() != QMetaType::QString)
        return {};
    return name.toString().trimmed();
}

bool validateOverlay(const QVariantMap &overlay, const QString &path, QString *error)
{
    auto fail = [&](const QString &message) {
        *error = message;
        qWarning("theme: %s (%s)", qPrintable(message), qPrintable(path));
        return false;
    };
    for (auto it = overlay.cbegin(); it != overlay.cend(); ++it) {
        if (it.key() == QLatin1String("name") || it.key() == QLatin1String("extends")
            || it.key() == QLatin1String("variant") || it.key() == QLatin1String("variants"))
            return fail(QStringLiteral("a variant overlay cannot contain \"%1\"").arg(it.key()));
        if (!kGroups.contains(it.key())) {
            qWarning("theme: unknown key \"%s\" in %s", qPrintable(it.key()), qPrintable(path));
            continue;
        }
        if (it.value().typeId() != QMetaType::QVariantMap)
            return fail(QStringLiteral("\"%1\" must be an object").arg(it.key()));
    }
    if (overlay.contains(QStringLiteral("window"))) {
        const QVariantMap window = overlay.value(QStringLiteral("window")).toMap();
        if (window.contains(QStringLiteral("translucent"))
            && window.value(QStringLiteral("translucent")).typeId() != QMetaType::Bool)
            return fail(QStringLiteral("window.translucent must be a boolean"));
        if (window.contains(QStringLiteral("backdrop"))
            && window.value(QStringLiteral("backdrop")).typeId() != QMetaType::QString)
            return fail(QStringLiteral("window.backdrop must be a string"));
    }
    return true;
}

bool validateStructure(const QVariantMap &doc, const QString &path, QString *error)
{
    auto fail = [&](const QString &message) {
        *error = message;
        qWarning("theme: %s (%s)", qPrintable(message), qPrintable(path));
        return false;
    };

    const auto requireString = [&](const char *key, bool allowMissing) {
        if (!doc.contains(QLatin1String(key)))
            return allowMissing;
        if (doc.value(QLatin1String(key)).typeId() != QMetaType::QString
            || doc.value(QLatin1String(key)).toString().trimmed().isEmpty())
            return fail(QStringLiteral("\"%1\" must be a non-empty string").arg(QLatin1String(key)));
        return true;
    };
    if (!requireString("name", true) || !requireString("extends", true))
        return false;
    if (doc.contains(QStringLiteral("schemaVersion"))) {
        const QVariant version = doc.value(QStringLiteral("schemaVersion"));
        const int versionType = version.typeId();
        if ((versionType != QMetaType::Double && versionType != QMetaType::Int
             && versionType != QMetaType::LongLong)
            || version.toDouble() < 1 || version.toDouble() != std::floor(version.toDouble()))
            return fail(QStringLiteral("\"schemaVersion\" must be a positive integer"));
    }
    if (doc.contains(QStringLiteral("variant"))) {
        const QString variant = doc.value(QStringLiteral("variant")).toString();
        if (variant != QLatin1String("dark") && variant != QLatin1String("light"))
            return fail(QStringLiteral("\"variant\" must be \"dark\" or \"light\""));
    }

    for (auto it = doc.cbegin(); it != doc.cend(); ++it) {
        if (it.key() == QLatin1String("name") || it.key() == QLatin1String("variant")
            || it.key() == QLatin1String("extends") || it.key() == QLatin1String("variants")
            || it.key() == QLatin1String("schemaVersion") || kGroups.contains(it.key()))
            continue;
        qWarning("theme: unknown key \"%s\" in %s", qPrintable(it.key()), qPrintable(path));
    }

    if (doc.contains(QStringLiteral("variants"))) {
        if (doc.value(QStringLiteral("variants")).typeId() != QMetaType::QVariantMap)
            return fail(QStringLiteral("\"variants\" must be an object"));
        const QVariantMap variants = doc.value(QStringLiteral("variants")).toMap();
        for (auto it = variants.cbegin(); it != variants.cend(); ++it) {
            if (it.key() != QLatin1String("dark") && it.key() != QLatin1String("light"))
                return fail(QStringLiteral("variants.%1 is not \"dark\" or \"light\"").arg(it.key()));
            if (it.value().typeId() != QMetaType::QVariantMap)
                return fail(QStringLiteral("variants.%1 must be an object").arg(it.key()));
            QString overlayError;
            if (!validateOverlay(it.value().toMap(), path + QStringLiteral("#variants.") + it.key(),
                                 &overlayError))
                return fail(overlayError);
        }
    }

    for (const QString &group : kGroups) {
        if (!doc.contains(group))
            continue;
        if (doc.value(group).typeId() != QMetaType::QVariantMap)
            return fail(QStringLiteral("\"%1\" must be an object").arg(group));
    }

    if (doc.contains(QStringLiteral("palette"))) {
        const QVariantMap palette = doc.value(QStringLiteral("palette")).toMap();
        for (auto it = palette.cbegin(); it != palette.cend(); ++it) {
            if (it.value().typeId() != QMetaType::QString)
                return fail(QStringLiteral("palette.%1 must be a string").arg(it.key()));
        }
    }

    if (doc.contains(QStringLiteral("window"))) {
        const QVariantMap window = doc.value(QStringLiteral("window")).toMap();
        if (window.contains(QStringLiteral("translucent"))
            && window.value(QStringLiteral("translucent")).typeId() != QMetaType::Bool)
            return fail(QStringLiteral("window.translucent must be a boolean"));
        if (window.contains(QStringLiteral("backdrop"))
            && window.value(QStringLiteral("backdrop")).typeId() != QMetaType::QString)
            return fail(QStringLiteral("window.backdrop must be a string"));
        if (window.contains(QStringLiteral("backgroundOpacity"))) {
            const QVariant opacity = window.value(QStringLiteral("backgroundOpacity"));
            const int opacityType = opacity.typeId();
            if (opacityType != QMetaType::Double && opacityType != QMetaType::Int
                && opacityType != QMetaType::LongLong)
                return fail(QStringLiteral("window.backgroundOpacity must be a number"));
            const double value = opacity.toDouble();
            if (value < 0.0 || value > 1.0)
                qWarning("theme: window.backgroundOpacity %g is outside 0..1 in %s", value,
                         qPrintable(path));
        }
    }
    return true;
}

QVariant deepMerge(const QVariant &base, const QVariant &overlay)
{
    if (base.typeId() == QMetaType::QVariantMap && overlay.typeId() == QMetaType::QVariantMap) {
        QVariantMap result = base.toMap();
        const QVariantMap over = overlay.toMap();
        for (auto it = over.cbegin(); it != over.cend(); ++it)
            result.insert(it.key(), deepMerge(result.value(it.key()), it.value()));
        return result;
    }
    return overlay;
}

QVariant resolveTree(const QVariant &value, const QVariantMap &root, QStringList *stack,
                     const QString &path, const QString &filePath)
{
    if (value.typeId() == QMetaType::QString) {
        const QString text = value.toString().trimmed();
        if (!text.startsWith(QLatin1Char('$')))
            return text;
        if (!isReference(text)) {
            qWarning("theme: malformed reference \"%s\" at %s in %s", qPrintable(text),
                     qPrintable(path), qPrintable(filePath));
            return text;
        }
        const QString ref = text.mid(1);
        if (stack->contains(ref) || stack->size() >= kMaxReferenceDepth) {
            qWarning("theme: reference cycle at %s involving $%s in %s", qPrintable(path),
                     qPrintable(ref), qPrintable(filePath));
            return text;
        }
        QVariant target;
        if (!lookup(root, ref, &target)) {
            qWarning("theme: unresolved reference $%s at %s in %s", qPrintable(ref),
                     qPrintable(path), qPrintable(filePath));
            return text;
        }
        stack->append(ref);
        const QVariant resolved = resolveTree(target, root, stack, ref, filePath);
        stack->removeLast();
        return resolved;
    }
    if (value.typeId() == QMetaType::QVariantMap) {
        QVariantMap result;
        const QVariantMap map = value.toMap();
        for (auto it = map.cbegin(); it != map.cend(); ++it) {
            const QString childPath = path.isEmpty() ? it.key() : path + QLatin1Char('.') + it.key();
            result.insert(it.key(), resolveTree(it.value(), root, stack, childPath, filePath));
        }
        return result;
    }
    if (value.typeId() == QMetaType::QVariantList) {
        QVariantList result;
        const QVariantList list = value.toList();
        for (int i = 0; i < list.size(); ++i) {
            const QString childPath = path + QLatin1Char('[') + QString::number(i) + QLatin1Char(']');
            result.append(resolveTree(list.at(i), root, stack, childPath, filePath));
        }
        return result;
    }
    return value;
}

QVariant coerceTree(const QVariant &value, const QString &path, const QString &filePath);

// Writes value at a dotted path, creating the maps on the way.
void insertPath(QVariantMap *root, const QStringList &parts, int index, const QVariant &value)
{
    if (index == parts.size() - 1) {
        root->insert(parts.at(index), value);
        return;
    }
    QVariantMap child = root->value(parts.at(index)).toMap();
    insertPath(&child, parts, index + 1, value);
    root->insert(parts.at(index), child);
}

QVariant coerceTree(const QVariant &value, const QString &path, const QString &filePath)
{
    if (value.typeId() == QMetaType::QString) {
        const QString text = value.toString();
        if (!text.startsWith(QLatin1Char('#')))
            return text;
        if (!isColorLiteral(text)) {
            qWarning("theme: invalid color \"%s\" at %s in %s", qPrintable(text), qPrintable(path),
                     qPrintable(filePath));
            return text;
        }
        const QColor color(text);
        if (!color.isValid()) {
            qWarning("theme: invalid color \"%s\" at %s in %s", qPrintable(text), qPrintable(path),
                     qPrintable(filePath));
            return text;
        }
        return color;
    }
    if (value.typeId() == QMetaType::QVariantMap) {
        QVariantMap result;
        const QVariantMap map = value.toMap();
        for (auto it = map.cbegin(); it != map.cend(); ++it) {
            const QString childPath = path.isEmpty() ? it.key() : path + QLatin1Char('.') + it.key();
            result.insert(it.key(), coerceTree(it.value(), childPath, filePath));
        }
        return result;
    }
    if (value.typeId() == QMetaType::QVariantList) {
        QVariantList result;
        const QVariantList list = value.toList();
        for (int i = 0; i < list.size(); ++i) {
            const QString childPath = path + QLatin1Char('[') + QString::number(i) + QLatin1Char(']');
            result.append(coerceTree(list.at(i), childPath, filePath));
        }
        return result;
    }
    return value;
}

void removeKey(QQmlPropertyMap *map, const QString &key)
{
    QQmlPropertyMap *child = asMap(map->value(key));
    map->clear(key);
    if (child)
        child->deleteLater();
}

void syncMap(QQmlPropertyMap *map, const QVariantMap &data)
{
    const QStringList existing = map->keys();
    for (const QString &key : existing) {
        if (!data.contains(key))
            removeKey(map, key);
    }
    // One insert for the lot: each single-key insert of a new key extends the map's metaobject,
    // which made filling a theme's few thousand tokens quadratic.
    QVariantHash changed;
    for (auto it = data.cbegin(); it != data.cend(); ++it) {
        if (it.value().typeId() == QMetaType::QVariantMap) {
            QQmlPropertyMap *child = asMap(map->value(it.key()));
            if (!child) {
                child = makeMap(map);
                changed.insert(it.key(), QVariant::fromValue(static_cast<QObject *>(child)));
            }
            syncMap(child, it.value().toMap());
            continue;
        }
        if (QQmlPropertyMap *old = asMap(map->value(it.key()))) {
            map->clear(it.key());
            old->deleteLater();
        }
        if (map->value(it.key()) != it.value())
            changed.insert(it.key(), it.value());
    }
    if (!changed.isEmpty())
        map->insert(changed);
}

void stripOverlayMeta(QVariantMap *overlay)
{
    overlay->remove(QStringLiteral("name"));
    overlay->remove(QStringLiteral("extends"));
    overlay->remove(QStringLiteral("variant"));
    overlay->remove(QStringLiteral("variants"));
}

bool isSafeThemeId(const QString &name)
{
    if (name.isEmpty() || isBuiltinId(name) || name == QLatin1String("system"))
        return false;
    if (name.startsWith(QLatin1Char('.')))
        return false;
    if (name.contains(QLatin1Char('/')) || name.contains(QLatin1Char('\\'))
        || name.contains(QLatin1String("..")))
        return false;
    for (const QChar ch : name) {
        if (ch.unicode() < 32)
            return false;
    }
    return true;
}

bool pathIsInside(const QString &root, const QString &candidate)
{
    const QString rootPath = QFileInfo(root).absoluteFilePath();
    const QString candidatePath = QFileInfo(candidate).absoluteFilePath();
    return candidatePath == rootPath
           || candidatePath.startsWith(rootPath + QLatin1Char('/'));
}

QString colorToHex(const QColor &color)
{
    if (!color.isValid())
        return {};
    if (color.alpha() == 255)
        return color.name(QColor::HexRgb);
    return color.name(QColor::HexArgb);
}

QJsonValue variantToJson(const QVariant &value)
{
    if (value.typeId() == QMetaType::QColor)
        return colorToHex(value.value<QColor>());
    if (value.typeId() == QMetaType::QVariantMap) {
        QJsonObject object;
        const QVariantMap map = value.toMap();
        for (auto it = map.cbegin(); it != map.cend(); ++it)
            object.insert(it.key(), variantToJson(it.value()));
        return object;
    }
    if (value.typeId() == QMetaType::QVariantList) {
        QJsonArray array;
        const QVariantList list = value.toList();
        for (const QVariant &entry : list)
            array.append(variantToJson(entry));
        return array;
    }
    if (value.typeId() == QMetaType::QString)
        return value.toString();
    if (value.typeId() == QMetaType::Bool)
        return value.toBool();
    if (value.typeId() == QMetaType::Int || value.typeId() == QMetaType::LongLong
        || value.typeId() == QMetaType::UInt || value.typeId() == QMetaType::ULongLong)
        return value.toLongLong();
    if (value.typeId() == QMetaType::Double || value.typeId() == QMetaType::Float)
        return value.toDouble();
    return QJsonValue::fromVariant(value);
}

bool isJunkZipPath(const QString &path)
{
    if (path.startsWith(QLatin1String("__MACOSX/")))
        return true;
    const QString base = path.section(QLatin1Char('/'), -1);
    return base == QLatin1String(".DS_Store") || base.startsWith(QLatin1String("._"));
}

bool isAllowedThemeRelative(const QString &relative)
{
    if (relative == QLatin1String("theme.json") || relative == QLatin1String("theme.dark.json")
        || relative == QLatin1String("theme.light.json"))
        return true;
    if (!relative.startsWith(QLatin1String("qml/")))
        return false;
    const QString base = relative.mid(4);
    if (base.isEmpty() || base.contains(QLatin1Char('/')))
        return false;
    if (!base.endsWith(QLatin1String(".qml")))
        return false;
    return isValidSlotName(base.chopped(4));
}

// Walks the central directory itself so a traversal entry that ZipArchive
// would drop still fails the import, and so unix symlink entries are rejected
// before any byte is written.
bool zipIsSafe(QFile &file, QString *error)
{
    auto fail = [&](const QString &message) {
        *error = message;
        return false;
    };
    if (file.size() > kMaxThemeTotalBytes)
        return fail(QStringLiteral("theme archive is too large"));

    const qint64 fileSize = file.size();
    if (fileSize < 22)
        return fail(QStringLiteral("not a valid ZIP archive"));
    const qint64 scanLen = qMin(fileSize, static_cast<qint64>(65535 + 22));
    if (!file.seek(fileSize - scanLen))
        return fail(QStringLiteral("not a valid ZIP archive"));
    const QByteArray tail = file.read(scanLen);
    int eocd = -1;
    for (int i = tail.size() - 22; i >= 0; --i) {
        if (static_cast<uchar>(tail.at(i)) == 0x50 && static_cast<uchar>(tail.at(i + 1)) == 0x4b
            && static_cast<uchar>(tail.at(i + 2)) == 0x05 && static_cast<uchar>(tail.at(i + 3)) == 0x06) {
            eocd = i;
            break;
        }
    }
    if (eocd < 0 || eocd + 22 > tail.size())
        return fail(QStringLiteral("not a valid ZIP archive"));

    const auto readU16 = [](const char *p) {
        const auto *b = reinterpret_cast<const uchar *>(p);
        return static_cast<quint16>(b[0] | (b[1] << 8));
    };
    const auto readU32 = [](const char *p) {
        const auto *b = reinterpret_cast<const uchar *>(p);
        return static_cast<quint32>(b[0] | (b[1] << 8) | (b[2] << 16) | (b[3] << 24));
    };

    const char *eocdPtr = tail.constData() + eocd;
    const quint16 totalEntries = readU16(eocdPtr + 10);
    const quint32 cdOffset = readU32(eocdPtr + 16);
    if (totalEntries > kMaxZipEntries)
        return fail(QStringLiteral("theme archive has too many entries"));
    if (!file.seek(cdOffset))
        return fail(QStringLiteral("not a valid ZIP archive"));

    qint64 uncompressedTotal = 0;
    for (int i = 0; i < totalEntries; ++i) {
        const QByteArray header = file.read(46);
        if (header.size() < 46 || readU32(header.constData()) != 0x02014b50)
            return fail(QStringLiteral("not a valid ZIP archive"));
        const quint16 method = readU16(header.constData() + 10);
        const quint32 compressed = readU32(header.constData() + 20);
        const quint32 uncompressed = readU32(header.constData() + 24);
        const quint16 nameLen = readU16(header.constData() + 28);
        const quint16 extraLen = readU16(header.constData() + 30);
        const quint16 commentLen = readU16(header.constData() + 32);
        const quint16 madeBy = readU16(header.constData() + 4);
        const quint32 externalAttrs = readU32(header.constData() + 38);
        const QByteArray nameBytes = file.read(nameLen);
        if (nameBytes.size() != nameLen)
            return fail(QStringLiteral("not a valid ZIP archive"));
        if (extraLen + commentLen > 0 && !file.seek(file.pos() + extraLen + commentLen))
            return fail(QStringLiteral("not a valid ZIP archive"));

        const QString name = QString::fromUtf8(nameBytes);
        if (name.isEmpty())
            return fail(QStringLiteral("theme archive contains an empty path"));
        const bool directory = name.endsWith(QLatin1Char('/')) || name.endsWith(QLatin1Char('\\'));
        if (name.startsWith(QLatin1Char('/')) || name.startsWith(QLatin1Char('\\'))
            || name.contains(QLatin1String("..")) || name.contains(QLatin1Char(':'))
            || name.contains(QLatin1Char('\\')))
            return fail(QStringLiteral("theme archive path is not allowed: %1").arg(name));
        if ((madeBy >> 8) == 3) {
            const quint32 mode = externalAttrs >> 16;
            if ((mode & 0170000) == 0120000)
                return fail(QStringLiteral("theme archive contains a symlink: %1").arg(name));
        }
        if (directory || isJunkZipPath(name))
            continue;
        if (method != 0 && method != 8)
            return fail(QStringLiteral("theme archive entry uses an unsupported method: %1").arg(name));
        if (compressed > kMaxThemeFileBytes || uncompressed > kMaxThemeFileBytes)
            return fail(QStringLiteral("theme archive entry is too large: %1").arg(name));
        uncompressedTotal += uncompressed;
        if (uncompressedTotal > kMaxThemeTotalBytes)
            return fail(QStringLiteral("theme archive is too large"));
    }
    return true;
}

} // namespace

ThemeManager *ThemeManager::shared()
{
    static ThemeManager instance;
    return &instance;
}

QVariantMap ThemeManager::builtinComponent(const QString &variant, const QString &component)
{
    const QString path = QString::fromLatin1(variant == QLatin1String("light") ? kLightResource : kDarkResource);
    QString error;
    const QVariantMap root = readObject(path, &error);
    if (!error.isEmpty())
        return {};
    const QString at = QStringLiteral("components.") + component;
    QStringList stack;
    const QVariant resolved = resolveTree(lookupPath(root, at), root, &stack, at, path);
    return coerceTree(resolved, at, path).toMap();
}

void ThemeManager::registerSingleton()
{
    // qmlRegisterSingletonInstance warns and no-ops if called twice. Tests and
    // main() share this entry point, so the second call must stay quiet.
    static bool registered = false;
    if (registered)
        return;
    registered = true;
    qmlRegisterSingletonInstance("Drift", 1, 0, "ThemeManager", shared());
}

ThemeManager::ThemeManager(QObject *parent)
    : QObject(parent)
    , m_palette(makeMap(this))
    , m_semantic(makeMap(this))
    , m_components(makeMap(this))
    , m_metrics(makeMap(this))
    , m_typography(makeMap(this))
    , m_motion(makeMap(this))
    , m_window(makeMap(this))
    , m_watcher(this)
{
    connect(&m_watcher, &QFileSystemWatcher::fileChanged, this, &ThemeManager::onThemeFileChanged);
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this, &ThemeManager::onThemeFileChanged);

    {
        QSettings settings;
        settings.beginGroup(QStringLiteral("Theme"));
        m_hotReload = settings.value(QStringLiteral("hotReload"), false).toBool();
        const QString accent = settings.value(QStringLiteral("accent")).toString().trimmed();
        if (QColor(accent).isValid())
            m_accentColor = QColor(accent).name(QColor::HexRgb);
    }

    QString error;
    if (auto dark = readTheme(QStringLiteral("dark"), &error))
        m_builtinDark = dark->document;
    else
        qWarning("theme: %s", qPrintable(error));
    error.clear();
    if (auto light = readTheme(QStringLiteral("light"), &error))
        m_builtinLight = light->document;
    else
        qWarning("theme: %s", qPrintable(error));

    if (!m_builtinDark.isEmpty()) {
        Loaded dark;
        dark.name = m_builtinDark.value(QStringLiteral("name")).toString();
        if (dark.name.isEmpty())
            dark.name = QStringLiteral("dark");
        dark.path = QString::fromLatin1(kDarkResource);
        dark.variant = QStringLiteral("dark");
        dark.user = false;
        dark.document = m_builtinDark;
        apply(dark);
    }
    restorePersisted();
}

QStringList ThemeManager::availableThemes() const
{
    // Re-read the folder: callers outside the Appearance grid expect a file copied
    // in by hand to show up. The Appearance grid reads the cached themes() instead.
    const QStringList before = m_userOrder;
    m_indexValid = false;
    ensureIndex();
    if (before != m_userOrder) {
        m_catalogValid = false;
        m_previewCache.clear();
    }
    QStringList names;
    for (const BuiltinTheme &theme : kBuiltinThemes)
        names.append(QString::fromLatin1(theme.id));
    names.append(m_bundledOrder);
    QStringList user = m_userOrder;
    user.sort();
    names.append(user);
    return names;
}

void ThemeManager::ensureIndex() const
{
    if (m_indexValid)
        return;
    m_indexValid = true;

    m_bundledOrder.clear();
    m_bundledPaths.clear();
    m_bundledInfo.clear();
    const QString root = bundledThemesRoot();
    const QDir bundledDir(root);
    if (!root.isEmpty() && bundledDir.exists()) {
        QString error;
        const QVariantMap catalog = readObject(bundledDir.filePath(QStringLiteral("catalog.json")), &error, false);
        QStringList ordered;
        const QVariantList entries = catalog.value(QStringLiteral("themes")).toList();
        for (const QVariant &entry : entries) {
            const QVariantMap info = entry.toMap();
            const QString id = info.value(QStringLiteral("id")).toString();
            if (id.isEmpty() || ordered.contains(id))
                continue;
            ordered.append(id);
            m_bundledInfo.insert(id, info);
        }
        // A folder the catalog does not list still ships, after the listed ones.
        const QStringList folders = bundledDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
        for (const QString &folder : folders) {
            if (!ordered.contains(folder))
                ordered.append(folder);
        }
        // The folder name is the theme name (checked by tst_thememanager), so no
        // theme file is opened here.
        for (const QString &id : ordered) {
            const QString path = bundledDir.filePath(id) + QStringLiteral("/theme.json");
            if (!isSafeThemeId(id) || !QFile::exists(path))
                continue;
            m_bundledOrder.append(id);
            m_bundledPaths.insert(id, path);
        }
    }

    m_userOrder.clear();
    m_userPaths.clear();
    const QDir dir(userThemesDirectory());
    if (!dir.exists())
        return;
    const QFileInfoList entries = dir.entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QFileInfo &info : entries) {
        QString path;
        QString fallback;
        if (info.isDir()) {
            path = info.filePath() + QStringLiteral("/theme.json");
            if (!QFile::exists(path))
                continue;
            fallback = info.fileName();
        } else if (info.suffix().compare(QLatin1String("json"), Qt::CaseInsensitive) == 0) {
            path = info.absoluteFilePath();
            fallback = info.completeBaseName();
        } else {
            continue;
        }
        QString name = readThemeName(path);
        if (name.isEmpty())
            name = fallback;
        if (isBuiltinId(name)) {
            qWarning("theme: user theme \"%s\" collides with a built-in (%s)", qPrintable(name),
                     qPrintable(path));
            continue;
        }
        // An imported copy of a theme Dafin now ships: the shipped one is used.
        if (m_bundledPaths.contains(name))
            continue;
        if (!isSafeThemeId(name) || m_userPaths.contains(name)) {
            if (m_userPaths.contains(name))
                qWarning("theme: duplicate theme name \"%s\" (%s)", qPrintable(name), qPrintable(path));
            continue;
        }
        m_userOrder.append(name);
        m_userPaths.insert(name, path);
    }
}

void ThemeManager::invalidateCatalog()
{
    m_indexValid = false;
    m_catalogValid = false;
    m_previewCache.clear();
    emit availableThemesChanged();
}

void ThemeManager::refreshThemes()
{
    // Cheap when nothing changed: only the user folder's theme names are read, and
    // the grid is rebuilt only when that set differs.
    const QStringList before = m_userOrder;
    const QHash<QString, QString> beforePaths = m_userPaths;
    m_indexValid = false;
    ensureIndex();
    if (m_catalogValid && before == m_userOrder && beforePaths == m_userPaths)
        return;
    m_catalogValid = false;
    m_previewCache.clear();
    emit availableThemesChanged();
}

bool ThemeManager::isBundledId(const QString &name) const
{
    ensureIndex();
    return m_bundledPaths.contains(name);
}

void ThemeManager::setBundledThemesRoot(const QString &root)
{
    bundledRootStorage() = root;
}

QString ThemeManager::bundledThemesRoot()
{
    return bundledRootStorage();
}

void ThemeManager::setAccentColor(const QString &color)
{
    const QColor parsed(color.trimmed());
    const QString value = parsed.isValid() ? parsed.name(QColor::HexRgb) : QString();
    if (value == m_accentColor)
        return;
    m_accentColor = value;
    QSettings settings;
    settings.beginGroup(QStringLiteral("Theme"));
    if (value.isEmpty())
        settings.remove(QStringLiteral("accent"));
    else
        settings.setValue(QStringLiteral("accent"), value);
    emit accentColorChanged();
    reload();
}

void ThemeManager::refreshOverrideCache()
{
    m_overrideDir.clear();
    m_overrideSlots.clear();
    if (!m_userTheme || !isFolderTheme(m_sourcePath))
        return;
    ensureIndex();
    // Shipped QML is trusted only at the shipped path, never a user copy that
    // borrows a shipped theme's name.
    const QString bundledPath = m_bundledPaths.value(m_currentTheme);
    const bool shipped = !bundledPath.isEmpty()
                         && QFileInfo(bundledPath).absoluteFilePath() == QFileInfo(m_sourcePath).absoluteFilePath();
    if (!shipped && !readTrustedThemes().contains(m_currentTheme))
        return;
    m_overrideDir = themeQmlDirectory(m_sourcePath);
    const QStringList names = overrideSlotsAt(m_sourcePath);
    for (const QString &name : names)
        m_overrideSlots.insert(name);
}

QVariant ThemeManager::token(const QString &dottedPath) const
{
    return lookupPath(m_resolved, dottedPath);
}

QVariant ThemeManager::builtinToken(const QString &variant, const QString &dottedPath) const
{
    if (variant == QLatin1String("dark"))
        return lookupPath(m_builtinDark, dottedPath);
    if (variant == QLatin1String("light"))
        return lookupPath(m_builtinLight, dottedPath);
    qWarning("theme: unknown variant \"%s\"", qPrintable(variant));
    return {};
}

bool ThemeManager::loadTheme(const QString &nameOrPath)
{
    QString error;
    const std::optional<Loaded> loaded = readTheme(nameOrPath, &error);
    if (!loaded) {
        setLastError(error.isEmpty() ? QStringLiteral("cannot load theme") : error);
        qWarning("theme: cannot load \"%s\": %s", qPrintable(nameOrPath), qPrintable(m_lastError));
        return false;
    }
    apply(*loaded);
    setLastError({});
    return true;
}

void ThemeManager::reload()
{
    if (m_sourcePath.isEmpty()) {
        loadTheme(m_requestedVariant.isEmpty() ? QStringLiteral("dark") : m_requestedVariant);
        return;
    }
    QString error;
    const std::optional<Loaded> loaded = readTheme(m_sourcePath, &error);
    if (!loaded) {
        qWarning("theme: reload failed for %s: %s", qPrintable(m_sourcePath), qPrintable(error));
        const QString fallback = m_requestedVariant == QLatin1String("light") ? QStringLiteral("light")
                                                                               : QStringLiteral("dark");
        const QString fallbackPath = fallback == QLatin1String("light") ? QString::fromLatin1(kLightResource)
                                                                        : QString::fromLatin1(kDarkResource);
        if (m_sourcePath != fallbackPath)
            loadTheme(fallback);
        return;
    }
    apply(*loaded);
}

void ThemeManager::setVariant(const QString &variant)
{
    if (variant != QLatin1String("dark") && variant != QLatin1String("light")) {
        qWarning("theme: unknown variant \"%s\"", qPrintable(variant));
        return;
    }
    if (m_requestedVariant == variant && m_userTheme && !m_hasVariants)
        return;
    if (m_requestedVariant == variant && !m_userTheme && m_currentTheme == variant)
        return;
    m_requestedVariant = variant;
    if (m_userTheme) {
        if (!m_hasVariants)
            return;
        QString error;
        const std::optional<Loaded> loaded = readTheme(m_sourcePath, &error);
        if (!loaded) {
            qWarning("theme: cannot apply variant \"%s\": %s", qPrintable(variant), qPrintable(error));
            return;
        }
        apply(*loaded);
        return;
    }
    // A pinned built-in stays until select() changes it. Theme.qml still calls
    // setVariant on every darkMode change, including while that pin is set.
    if (isBuiltinId(m_selection))
        return;
    // Session override (DRIFT_THEME). Theme.qml calls setVariant at startup and
    // on every darkMode change; that must not swap the override for a built-in.
    // Dual-variant overrides already swapped their overlay above.
    if (m_sessionOverride)
        return;
    if (m_currentTheme != variant)
        loadTheme(variant);
}

bool ThemeManager::applySessionOverride(const QString &nameOrPath)
{
    const QString theme = nameOrPath.trimmed();
    if (theme.isEmpty()) {
        setLastError(QStringLiteral("empty theme override"));
        return false;
    }
    if (!loadTheme(theme))
        return false;
    m_sessionOverride = true;
    return true;
}

QUrl ThemeManager::overrideUrl(const QString &slot) const
{
    if (m_overrideDir.isEmpty() || !isValidSlotName(slot) || !m_overrideSlots.contains(slot))
        return {};
    return themeFileUrl(m_overrideDir + QLatin1Char('/') + slot + QStringLiteral(".qml"));
}

bool ThemeManager::isThemeTrusted(const QString &name) const
{
    const QString theme = name.trimmed();
    if (theme.isEmpty())
        return false;
    if (isBundledId(theme))
        return true;
    return readTrustedThemes().contains(theme);
}

void ThemeManager::setThemeTrusted(const QString &name, bool trusted)
{
    const QString theme = name.trimmed();
    if (theme.isEmpty() || isBundledId(theme))
        return;
    QStringList names = readTrustedThemes();
    const bool listed = names.contains(theme);
    if (trusted == listed)
        return;
    if (trusted)
        names.append(theme);
    else
        names.removeAll(theme);
    writeTrustedThemes(names);
    invalidateCatalog();
    if (theme == m_currentTheme) {
        refreshOverrideCache();
        emit themeChanged();
    }
}

QStringList ThemeManager::overrideSlots(const QString &name) const
{
    QString error;
    const QString path = pathForThemeName(name.trimmed(), &error);
    if (path.isEmpty())
        return {};
    return overrideSlotsAt(path);
}

QStringList ThemeManager::overrideSlotsAt(const QString &path) const
{
    if (!isFolderTheme(path))
        return {};
    const QDir dir(themeQmlDirectory(path));
    if (!dir.exists())
        return {};
    const QStringList files = dir.entryList({QStringLiteral("*.qml")}, QDir::Files, QDir::Name);
    // `slots` is a Qt macro, so the list cannot use that name.
    QStringList slotNames;
    slotNames.reserve(files.size());
    for (const QString &file : files) {
        const QString base = QFileInfo(file).completeBaseName();
        if (isValidSlotName(base))
            slotNames.append(base);
    }
    return slotNames;
}

QString ThemeManager::themeQmlDirectory(const QString &themeJsonPath) const
{
    const QFileInfo info(themeJsonPath);
    if (info.fileName() != QLatin1String("theme.json"))
        return {};
    if (themeJsonPath.startsWith(QLatin1Char(':')) && !isUnderBundledRoot(themeJsonPath))
        return {};
    return info.dir().absoluteFilePath(QStringLiteral("qml"));
}

bool ThemeManager::isFolderTheme(const QString &themeJsonPath) const
{
    return !themeQmlDirectory(themeJsonPath).isEmpty();
}

bool ThemeManager::isThemeWatchPath(const QString &path) const
{
    const QString absolute = QFileInfo(path).absoluteFilePath();
    if (absolute == QFileInfo(m_sourcePath).absoluteFilePath())
        return true;
    const QFileInfo source(m_sourcePath);
    if (source.fileName() != QLatin1String("theme.json") || m_sourcePath.startsWith(QLatin1Char(':')))
        return false;
    const QString themeDir = source.dir().absolutePath();
    if (absolute == themeDir)
        return true;
    const QString darkSide = QFileInfo(source.dir().filePath(QStringLiteral("theme.dark.json"))).absoluteFilePath();
    const QString lightSide = QFileInfo(source.dir().filePath(QStringLiteral("theme.light.json"))).absoluteFilePath();
    if (absolute == darkSide || absolute == lightSide)
        return true;
    const QString qmlDir = themeQmlDirectory(m_sourcePath);
    return absolute == qmlDir || absolute.startsWith(qmlDir + QLatin1Char('/'));
}

QStringList ThemeManager::readTrustedThemes() const
{
    if (!m_trustedLoaded) {
        QSettings settings;
        settings.beginGroup(QStringLiteral("Theme"));
        m_trustedCache = settings.value(QStringLiteral("trusted")).toStringList();
        m_trustedLoaded = true;
    }
    return m_trustedCache;
}

void ThemeManager::writeTrustedThemes(const QStringList &names)
{
    QSettings settings;
    settings.beginGroup(QStringLiteral("Theme"));
    settings.setValue(QStringLiteral("trusted"), names);
    m_trustedCache = names;
    m_trustedLoaded = true;
}

void ThemeManager::setHotReload(bool enabled)
{
    if (m_hotReload == enabled)
        return;
    m_hotReload = enabled;
    QSettings settings;
    settings.beginGroup(QStringLiteral("Theme"));
    settings.setValue(QStringLiteral("hotReload"), m_hotReload);
    updateWatch();
    emit hotReloadChanged();
}

void ThemeManager::setLastError(const QString &error)
{
    if (m_lastError == error)
        return;
    m_lastError = error;
    emit lastErrorChanged();
}

void ThemeManager::writeSelection() const
{
    QSettings settings;
    settings.beginGroup(QStringLiteral("Theme"));
    settings.setValue(QStringLiteral("selection"), m_selection);
}

void ThemeManager::restorePersisted()
{
    QSettings settings;
    settings.beginGroup(QStringLiteral("Theme"));
    const QString saved = settings.value(QStringLiteral("selection"), QStringLiteral("system")).toString().trimmed();
    if (saved.isEmpty() || saved == QLatin1String("system")) {
        m_selection = QStringLiteral("system");
        return;
    }
    if (!select(saved)) {
        qWarning("theme: saved selection \"%s\" is unavailable (%s); using system", qPrintable(saved),
                 qPrintable(m_lastError));
        m_selection = QStringLiteral("system");
        writeSelection();
        setLastError({});
        if (m_userTheme || m_currentTheme != QLatin1String("dark"))
            loadTheme(QStringLiteral("dark"));
    }
}

QString ThemeManager::locate(const QString &nameOrPath, QString *error) const
{
    QString text = nameOrPath.trimmed();
    if (text.startsWith(QLatin1String("qrc:")))
        text.remove(0, 3);
    if (const QString builtin = builtinResource(text); !builtin.isEmpty())
        return builtin;

    const QFileInfo info(text);
    if (info.exists()) {
        if (info.isDir()) {
            const QString themeFile = info.filePath() + QStringLiteral("/theme.json");
            if (!QFile::exists(themeFile)) {
                *error = QStringLiteral("no theme.json in %1").arg(info.filePath());
                return {};
            }
            return themeFile;
        }
        return info.absoluteFilePath().startsWith(QLatin1Char(':')) ? text : info.absoluteFilePath();
    }
    if (text.startsWith(QLatin1Char(':')) || text.contains(QLatin1Char('/')) || text.contains(QLatin1Char('\\'))) {
        *error = QStringLiteral("theme file not found: %1").arg(text);
        return {};
    }

    QString nameError;
    const QString named = pathForThemeName(text, &nameError);
    if (named.isEmpty())
        *error = nameError;
    return named;
}

QString ThemeManager::pathForThemeName(const QString &name, QString *error) const
{
    if (const QString builtin = builtinResource(name); !builtin.isEmpty())
        return builtin;

    ensureIndex();
    const auto find = [&]() -> QString {
        if (const auto it = m_bundledPaths.constFind(name); it != m_bundledPaths.cend())
            return *it;
        if (const auto it = m_userPaths.constFind(name); it != m_userPaths.cend() && QFile::exists(*it))
            return *it;
        return {};
    };
    QString path = find();
    if (path.isEmpty()) {
        // A theme copied into the folder (or removed) while Dafin runs.
        const QStringList before = m_userOrder;
        m_indexValid = false;
        ensureIndex();
        if (before != m_userOrder) {
            m_catalogValid = false;
            m_previewCache.clear();
        }
        path = find();
    }
    if (path.isEmpty())
        *error = QStringLiteral("unknown theme \"%1\"").arg(name);
    return path;
}

QVariantMap ThemeManager::compose(const QString &path, QStringList *stack, QString *error) const
{
    if (stack->contains(path)) {
        *error = QStringLiteral("extends cycle at %1").arg(path);
        qWarning("theme: %s", qPrintable(*error));
        return {};
    }
    stack->append(path);

    QVariantMap doc = readObject(path, error);
    if (!error->isEmpty()) {
        stack->removeLast();
        return {};
    }
    if (!validateStructure(doc, path, error)) {
        stack->removeLast();
        return {};
    }
    if (doc.contains(QStringLiteral("extends"))) {
        const QString baseName = doc.take(QStringLiteral("extends")).toString();
        QString baseError;
        const QString basePath = pathForThemeName(baseName, &baseError);
        if (basePath.isEmpty()) {
            *error = baseError;
            qWarning("theme: %s (from %s)", qPrintable(baseError), qPrintable(path));
            stack->removeLast();
            return {};
        }
        const QVariantMap base = compose(basePath, stack, error);
        if (!error->isEmpty()) {
            stack->removeLast();
            return {};
        }
        // A newer base can carry keys this build does not understand either.
        const int version = std::max(base.value(QStringLiteral("schemaVersion")).toInt(),
                                     doc.value(QStringLiteral("schemaVersion")).toInt());
        doc = deepMerge(base, doc).toMap();
        if (version > 0)
            doc.insert(QStringLiteral("schemaVersion"), version);
    }
    stack->removeLast();
    return doc;
}

std::optional<ThemeManager::Loaded> ThemeManager::readTheme(const QString &nameOrPath, QString *error,
                                                           bool userAccent, const QStringList *onlyPaths) const
{
    const QString path = locate(nameOrPath, error);
    if (path.isEmpty())
        return std::nullopt;

    QStringList stack;
    QVariantMap merged = compose(path, &stack, error);
    if (!error->isEmpty())
        return std::nullopt;

    QVariantMap darkOverlay;
    QVariantMap lightOverlay;
    bool hasDark = false;
    bool hasLight = false;
    if (merged.contains(QStringLiteral("variants"))) {
        const QVariantMap variants = merged.take(QStringLiteral("variants")).toMap();
        if (variants.contains(QStringLiteral("dark"))) {
            darkOverlay = variants.value(QStringLiteral("dark")).toMap();
            stripOverlayMeta(&darkOverlay);
            hasDark = true;
        }
        if (variants.contains(QStringLiteral("light"))) {
            lightOverlay = variants.value(QStringLiteral("light")).toMap();
            stripOverlayMeta(&lightOverlay);
            hasLight = true;
        }
    }
    if (QFileInfo(path).fileName() == QLatin1String("theme.json") && !path.startsWith(QLatin1Char(':'))) {
        const QDir dir = QFileInfo(path).dir();
        const auto loadSide = [&](const char *fileName, QVariantMap *overlay, bool *present) {
            const QString sidePath = dir.filePath(QLatin1String(fileName));
            if (!QFile::exists(sidePath))
                return true;
            if (QFileInfo(sidePath).isSymLink()) {
                *error = QStringLiteral("refusing to read variant symlink %1").arg(sidePath);
                qWarning("theme: %s", qPrintable(*error));
                return false;
            }
            QVariantMap side = readObject(sidePath, error);
            if (!error->isEmpty())
                return false;
            QString overlayError;
            if (!validateOverlay(side, sidePath, &overlayError)) {
                *error = overlayError;
                return false;
            }
            stripOverlayMeta(&side);
            *overlay = deepMerge(*overlay, side).toMap();
            *present = true;
            return true;
        };
        if (!loadSide("theme.dark.json", &darkOverlay, &hasDark)
            || !loadSide("theme.light.json", &lightOverlay, &hasLight))
            return std::nullopt;
    }

    const bool hasVariants = hasDark && hasLight;
    QString declared = merged.value(QStringLiteral("variant")).toString();
    if (declared != QLatin1String("dark") && declared != QLatin1String("light"))
        declared.clear();
    if (hasVariants) {
        const QString side = m_requestedVariant == QLatin1String("light") ? QStringLiteral("light")
                                                                           : QStringLiteral("dark");
        merged = deepMerge(merged, side == QLatin1String("light") ? lightOverlay : darkOverlay).toMap();
        declared = side;
    } else if (hasDark || hasLight) {
        merged = deepMerge(merged, hasDark ? darkOverlay : lightOverlay).toMap();
        if (declared.isEmpty())
            declared = hasDark ? QStringLiteral("dark") : QStringLiteral("light");
    } else if (declared.isEmpty()) {
        declared = QStringLiteral("dark");
    }
    merged.insert(QStringLiteral("variant"), declared);

    const int schemaVersion = merged.value(QStringLiteral("schemaVersion")).toInt();
    if (!isBuiltinPath(path)) {
        QString defaultsError;
        const QVariantMap defaults = readObject(
            QString::fromLatin1(declared == QLatin1String("light") ? kLightResource : kDarkResource),
            &defaultsError);
        merged = deepMerge(defaults, merged).toMap();
    }
    merged.insert(QStringLiteral("schemaVersion"), std::max(schemaVersion, kThemeSchemaVersion));

    QString leafName = readThemeName(path);
    if (leafName.isEmpty())
        leafName = fallbackName(path);
    merged.insert(QStringLiteral("name"), leafName);
    if (userAccent && !m_accentColor.isEmpty())
        applyAccentOverride(&merged, m_accentColor);

    QVariant coerced;
    if (onlyPaths) {
        // Dafin: preview tiles need a dozen tokens, not the whole resolved document.
        QVariantMap partial;
        for (const QString &wanted : *onlyPaths) {
            QVariant raw;
            if (!lookup(merged, wanted, &raw))
                continue;
            QStringList stack;
            const QVariant value = coerceTree(resolveTree(raw, merged, &stack, wanted, path), wanted, path);
            insertPath(&partial, wanted.split(QLatin1Char('.')), 0, value);
        }
        partial.insert(QStringLiteral("name"), merged.value(QStringLiteral("name")));
        coerced = partial;
    } else {
        QStringList refs;
        const QVariant resolved = resolveTree(merged, merged, &refs, QString(), path);
        coerced = coerceTree(resolved, QString(), path);
    }
    if (coerced.typeId() != QMetaType::QVariantMap) {
        *error = QStringLiteral("theme did not resolve to an object");
        qWarning("theme: %s (%s)", qPrintable(*error), qPrintable(path));
        return std::nullopt;
    }

    Loaded loaded;
    loaded.document = coerced.toMap();
    loaded.path = isBuiltinPath(path) ? path : QFileInfo(path).absoluteFilePath();
    loaded.name = loaded.document.value(QStringLiteral("name")).toString();
    loaded.variant = declared;
    loaded.hasVariants = hasVariants;
    loaded.schemaVersion = schemaVersion;
    loaded.user = !isBuiltinPath(loaded.path);
    loaded.document.insert(QStringLiteral("variant"), declared);
    return loaded;
}

void ThemeManager::apply(const Loaded &loaded)
{
    const QString previousVariant = m_effectiveVariant;
    const bool previousFollows = appearanceFollowsTheme();

    m_currentTheme = loaded.name;
    m_sourcePath = loaded.path;
    m_userTheme = loaded.user;
    m_hasVariants = loaded.hasVariants;
    m_effectiveVariant = loaded.variant.isEmpty() ? QStringLiteral("dark") : loaded.variant;
    m_resolved = loaded.document;
    const QString warning = loaded.schemaVersion > kThemeSchemaVersion
                                ? QStringLiteral("“%1” was made for a newer version of Drift. "
                                                 "Some parts may look wrong until you update.")
                                      .arg(loaded.name)
                                : QString();
    if (warning != m_lastWarning) {
        m_lastWarning = warning;
        if (!warning.isEmpty())
            qWarning("theme: %s uses schemaVersion %d, this build reads %d", qPrintable(loaded.path),
                     loaded.schemaVersion, kThemeSchemaVersion);
        emit lastWarningChanged();
    }
    if (loaded.path == QLatin1String(kDarkResource))
        m_builtinDark = loaded.document;
    else if (loaded.path == QLatin1String(kLightResource))
        m_builtinLight = loaded.document;

    QQmlPropertyMap *maps[] = {
        m_palette, m_semantic, m_components, m_metrics, m_typography, m_motion, m_window,
    };
    for (int i = 0; i < kGroups.size(); ++i) {
        QVariantMap group = loaded.document.value(kGroups.at(i)).toMap();
        // Dafin: operating-system "reduce motion" zeroes the animation durations.
        if (kGroups.at(i) == QLatin1String("motion"))
            drift::motion::reduceTokens(group);
        syncMap(maps[i], group);
    }

    updateWatch();
    refreshOverrideCache();
    // Dafin: no availableThemesChanged here. The list does not depend on which
    // theme is on screen, and rebuilding it on every switch froze Appearance.
    emit themeChanged();
    // setVariant/select/session override all land here. Emit only for a real
    // change so Theme.darkMode is not re-entered while it is writing the side back.
    if (m_effectiveVariant != previousVariant)
        emit effectiveVariantChanged();
    if (appearanceFollowsTheme() != previousFollows)
        emit appearanceFollowsThemeChanged();
}

void ThemeManager::updateWatch()
{
    const QStringList watchedFiles = m_watcher.files();
    if (!watchedFiles.isEmpty())
        m_watcher.removePaths(watchedFiles);
    const QStringList watchedDirs = m_watcher.directories();
    if (!watchedDirs.isEmpty())
        m_watcher.removePaths(watchedDirs);
    if (!m_hotReload || m_sourcePath.isEmpty() || m_sourcePath.startsWith(QLatin1Char(':')))
        return;

    QStringList paths;
    if (QFile::exists(m_sourcePath))
        paths.append(m_sourcePath);
    if (isFolderTheme(m_sourcePath)) {
        const QFileInfo source(m_sourcePath);
        paths.append(source.dir().absolutePath());
        const QString darkSide = source.dir().filePath(QStringLiteral("theme.dark.json"));
        const QString lightSide = source.dir().filePath(QStringLiteral("theme.light.json"));
        if (QFile::exists(darkSide))
            paths.append(darkSide);
        if (QFile::exists(lightSide))
            paths.append(lightSide);
        const QString qmlDirPath = themeQmlDirectory(m_sourcePath);
        if (QDir(qmlDirPath).exists()) {
            paths.append(qmlDirPath);
            const QDir qmlDir(qmlDirPath);
            const QFileInfoList qmlFiles = qmlDir.entryInfoList({QStringLiteral("*.qml")}, QDir::Files);
            for (const QFileInfo &qmlFile : qmlFiles)
                paths.append(qmlFile.absoluteFilePath());
        }
    }
    if (!paths.isEmpty())
        m_watcher.addPaths(paths);
}

void ThemeManager::onThemeFileChanged(const QString &path)
{
    if (!m_hotReload || m_inReload || !isThemeWatchPath(path))
        return;
    m_inReload = true;
    m_previewCache.clear();
    m_catalogValid = false;
    reload();
    m_inReload = false;
}

QVariantList ThemeManager::themes() const
{
    if (m_catalogValid)
        return m_catalog;
    ensureIndex();
    QVariantList list;
    for (const BuiltinTheme &theme : kBuiltinThemes)
        list.append(themeRecord(QString::fromLatin1(theme.id), QString::fromLatin1(theme.resource), true));
    for (const QString &id : std::as_const(m_bundledOrder))
        list.append(themeRecord(id, m_bundledPaths.value(id), false));
    for (const QString &id : std::as_const(m_userOrder))
        list.append(themeRecord(id, m_userPaths.value(id), false));
    m_catalog = list;
    m_catalogValid = true;
    return list;
}

QVariantMap ThemeManager::themeRecord(const QString &id, const QString &path, bool builtIn) const
{
    QString error;
    const QVariantMap raw = readObject(path, &error, false);
    QString name = raw.value(QStringLiteral("name")).toString().trimmed();
    if (name.isEmpty())
        name = id;

    bool hasDarkFile = false;
    bool hasLightFile = false;
    if (!builtIn && QFileInfo(path).fileName() == QLatin1String("theme.json")) {
        const QDir dir = QFileInfo(path).dir();
        hasDarkFile = QFile::exists(dir.filePath(QStringLiteral("theme.dark.json")));
        hasLightFile = QFile::exists(dir.filePath(QStringLiteral("theme.light.json")));
    }
    const QVariantMap variants = raw.value(QStringLiteral("variants")).toMap();
    const bool hasDark = variants.contains(QStringLiteral("dark")) || hasDarkFile;
    const bool hasLight = variants.contains(QStringLiteral("light")) || hasLightFile;
    QString variant = raw.value(QStringLiteral("variant")).toString();
    if (variant != QLatin1String("dark") && variant != QLatin1String("light"))
        variant = (hasLight && !hasDark) ? QStringLiteral("light") : QStringLiteral("dark");

    bool translucent = false;
    if (raw.contains(QStringLiteral("window")))
        translucent = raw.value(QStringLiteral("window")).toMap().value(QStringLiteral("translucent")).toBool();

    // Dafin: shipped themes, display names and the Color / QML split for the grid.
    const bool bundled = !builtIn && m_bundledPaths.value(id) == path;
    const QStringList slotNames = builtIn ? QStringList() : overrideSlotsAt(path);
    const QVariantMap info = bundled ? m_bundledInfo.value(id) : QVariantMap();
    QString label = info.value(QStringLiteral("label")).toString();
    if (label.isEmpty())
        label = raw.value(QStringLiteral("label")).toString().trimmed();
    if (label.isEmpty())
        label = name;
    if (builtIn && !label.isEmpty())
        label[0] = label.at(0).toUpper();
    QString group = info.value(QStringLiteral("group")).toString();
    if (group.isEmpty())
        group = builtIn ? QStringLiteral("Dafin") : QStringLiteral("Installed");

    QVariantMap row;
    row.insert(QStringLiteral("id"), id);
    row.insert(QStringLiteral("name"), name);
    row.insert(QStringLiteral("variant"), variant);
    row.insert(QStringLiteral("builtIn"), builtIn);
    row.insert(QStringLiteral("hasOverrides"), !slotNames.isEmpty());
    row.insert(QStringLiteral("trusted"), bundled || readTrustedThemes().contains(id));
    row.insert(QStringLiteral("bundled"), bundled);
    row.insert(QStringLiteral("removable"), !builtIn && !bundled);
    row.insert(QStringLiteral("kind"), slotNames.isEmpty() ? QStringLiteral("color") : QStringLiteral("qml"));
    row.insert(QStringLiteral("label"), label);
    row.insert(QStringLiteral("group"), group);
    row.insert(QStringLiteral("path"), builtIn ? path : QFileInfo(path).absoluteFilePath());
    row.insert(QStringLiteral("hasVariants"), hasDark && hasLight);
    row.insert(QStringLiteral("translucent"), translucent);
    return row;
}

QVariantMap ThemeManager::previewColors(const QString &id) const
{
    const QString key = id.trimmed();
    const QString which = (key.isEmpty() || key == QLatin1String("system"))
                              ? (m_requestedVariant == QLatin1String("light") ? QStringLiteral("light")
                                                                              : QStringLiteral("dark"))
                              : key;
    const QString cacheKey = which + QLatin1Char('|') + m_requestedVariant;
    if (const auto it = m_previewCache.constFind(cacheKey); it != m_previewCache.cend())
        return *it;
    QString error;
    // The theme's own colours: the accent picked in Appearance is not drawn into tiles.
    static const QStringList wanted = {
        QStringLiteral("semantic.surface.app"), QStringLiteral("semantic.surface.panel"),
        QStringLiteral("semantic.accent"), QStringLiteral("semantic.foreground"),
        QStringLiteral("semantic.primary"), QStringLiteral("semantic.panel.border"),
        QStringLiteral("semantic.mutedForeground"), QStringLiteral("semantic.timeline.headerBackground"),
        QStringLiteral("components.timelineClipItem.videoBackground"),
        QStringLiteral("components.timelineClipItem.audioBackground"), QStringLiteral("metrics.radius.sm"),
    };
    const std::optional<Loaded> loaded = readTheme(which, &error, false, &wanted);
    if (!loaded)
        return {};
    const QVariantMap &doc = loaded->document;
    QVariantMap colors;
    colors.insert(QStringLiteral("surface"), lookupPath(doc, QStringLiteral("semantic.surface.app")));
    colors.insert(QStringLiteral("panel"), lookupPath(doc, QStringLiteral("semantic.surface.panel")));
    colors.insert(QStringLiteral("accent"), lookupPath(doc, QStringLiteral("semantic.accent")));
    colors.insert(QStringLiteral("foreground"), lookupPath(doc, QStringLiteral("semantic.foreground")));
    colors.insert(QStringLiteral("primary"), lookupPath(doc, QStringLiteral("semantic.primary")));
    colors.insert(QStringLiteral("border"), lookupPath(doc, QStringLiteral("semantic.panel.border")));
    colors.insert(QStringLiteral("muted"), lookupPath(doc, QStringLiteral("semantic.mutedForeground")));
    colors.insert(QStringLiteral("timeline"), lookupPath(doc, QStringLiteral("semantic.timeline.headerBackground")));
    colors.insert(QStringLiteral("clipVideo"),
                  lookupPath(doc, QStringLiteral("components.timelineClipItem.videoBackground")));
    colors.insert(QStringLiteral("clipAudio"),
                  lookupPath(doc, QStringLiteral("components.timelineClipItem.audioBackground")));
    colors.insert(QStringLiteral("radius"), lookupPath(doc, QStringLiteral("metrics.radius.sm")));
    m_previewCache.insert(cacheKey, colors);
    return colors;
}

bool ThemeManager::select(const QString &id)
{
    const QString choice = id.trimmed();
    if (choice.isEmpty()) {
        setLastError(QStringLiteral("empty theme selection"));
        return false;
    }
    if (choice == QLatin1String("system")) {
        const QString side = m_requestedVariant == QLatin1String("light") ? QStringLiteral("light")
                                                                           : QStringLiteral("dark");
        m_sessionOverride = false;
        m_selection = QStringLiteral("system");
        writeSelection();
        bool ok = true;
        if (m_userTheme || m_currentTheme != side)
            ok = loadTheme(side);
        else
            setLastError({});
        emit selectionChanged();
        return ok;
    }
    if (choice == QLatin1String("dark") || choice == QLatin1String("light")) {
        m_sessionOverride = false;
        m_requestedVariant = choice;
        m_selection = choice;
        writeSelection();
        const bool ok = loadTheme(choice);
        emit selectionChanged();
        return ok;
    }

    QString error;
    const std::optional<Loaded> loaded = readTheme(choice, &error);
    if (!loaded) {
        setLastError(error.isEmpty() ? QStringLiteral("unknown theme \"%1\"").arg(choice) : error);
        return false;
    }
    if (loaded->user && !isSafeThemeId(loaded->name)) {
        setLastError(QStringLiteral("theme name \"%1\" cannot be selected").arg(loaded->name));
        return false;
    }
    m_sessionOverride = false;
    m_selection = loaded->user ? loaded->name : choice;
    writeSelection();
    apply(*loaded);
    setLastError({});
    emit selectionChanged();
    return true;
}

QByteArray ThemeManager::themeTemplate(const QString &id, const QString &name, bool starter,
                                       QString *error) const
{
    error->clear();
    if (!isSafeThemeId(name)) {
        *error = QStringLiteral("theme name \"%1\" cannot be installed").arg(name);
        return {};
    }
    const std::optional<Loaded> loaded = readTheme(id, error);
    if (!loaded)
        return {};

    QVariantMap doc;
    if (starter) {
        doc.insert(QStringLiteral("extends"), id);
        doc.insert(QStringLiteral("variant"), loaded->variant);
        const QVariantMap palette = loaded->document.value(QStringLiteral("palette")).toMap();
        QVariantMap core;
        for (const QString &key : kStarterPaletteKeys) {
            if (palette.contains(key))
                core.insert(key, palette.value(key));
        }
        doc.insert(QStringLiteral("palette"), core);
        for (const char *group : {"metrics", "typography", "motion", "window"}) {
            const QString key = QLatin1String(group);
            if (loaded->document.contains(key))
                doc.insert(key, loaded->document.value(key));
        }
    } else {
        doc = loaded->document;
    }
    doc.insert(QStringLiteral("name"), name);
    doc.insert(QStringLiteral("schemaVersion"), std::max(loaded->schemaVersion, kThemeSchemaVersion));
    return QJsonDocument(variantToJson(doc).toObject()).toJson(QJsonDocument::Indented);
}

QUrl ThemeManager::themesFolderUrl() const
{
    const QString dir = userThemesDirectory();
    QDir().mkpath(dir);
    return QUrl::fromLocalFile(QDir(dir).absolutePath());
}

bool ThemeManager::exportTheme(const QUrl &file)
{
    if (m_resolved.isEmpty()) {
        setLastError(QStringLiteral("no theme is loaded"));
        return false;
    }

    QVariantMap doc = m_resolved;
    doc.remove(QStringLiteral("extends"));
    doc.remove(QStringLiteral("variants"));
    const QJsonObject object = variantToJson(doc).toObject();
    const QByteArray bytes = QJsonDocument(object).toJson(QJsonDocument::Indented);

    // Android's save dialog returns a content:// URI. QSaveFile cannot open one;
    // the document already exists and Qt's content file engine truncates it.
    if (AndroidUri::isContentUri(file)) {
        const std::unique_ptr<QFile> out = AndroidUri::openForWrite(file);
        if (!out || out->write(bytes) != bytes.size() || !out->flush()) {
            const QString detail = out ? QStringLiteral(": ") + out->errorString() : QString();
            setLastError(QStringLiteral("cannot write %1%2").arg(file.toString(), detail));
            return false;
        }
        setLastError({});
        return true;
    }

    QString path;
    if (file.isLocalFile())
        path = file.toLocalFile();
    else if (file.scheme().isEmpty())
        path = file.toString();
    if (path.isEmpty()) {
        setLastError(QStringLiteral("export path is not a local file"));
        return false;
    }
    QSaveFile out(path);
    if (!out.open(QIODevice::WriteOnly)) {
        setLastError(QStringLiteral("cannot write %1: %2").arg(path, out.errorString()));
        return false;
    }
    if (out.write(bytes) != bytes.size() || !out.commit()) {
        setLastError(QStringLiteral("cannot write %1: %2").arg(path, out.errorString()));
        return false;
    }
    setLastError({});
    return true;
}

bool ThemeManager::removeTheme(const QString &id)
{
    const QString theme = id.trimmed();
    if (theme.isEmpty() || isBuiltinId(theme) || theme == QLatin1String("system") || isBundledId(theme)) {
        setLastError(QStringLiteral("built-in themes cannot be removed"));
        return false;
    }
    QString error;
    const QString path = pathForThemeName(theme, &error);
    if (path.isEmpty() || isBuiltinPath(path)) {
        setLastError(error.isEmpty() ? QStringLiteral("unknown theme \"%1\"").arg(theme) : error);
        return false;
    }
    if (!removeInstalled(path, &error)) {
        setLastError(error);
        return false;
    }
    setThemeTrusted(theme, false);
    if (m_selection == theme || m_currentTheme == theme) {
        const QString side = m_requestedVariant == QLatin1String("light") ? QStringLiteral("light")
                                                                           : QStringLiteral("dark");
        m_sessionOverride = false;
        m_selection = QStringLiteral("system");
        writeSelection();
        loadTheme(side);
        emit selectionChanged();
    }
    invalidateCatalog();
    setLastError({});
    return true;
}

bool ThemeManager::removeInstalled(const QString &path, QString *error) const
{
    const QString root = QFileInfo(userThemesDirectory()).absoluteFilePath();
    const QFileInfo info(path);
    if (info.isSymLink()) {
        *error = QStringLiteral("refusing to delete a symlink");
        return false;
    }
    const QString canonical = info.canonicalFilePath();
    if (canonical.isEmpty() || canonical == root || !canonical.startsWith(root + QLatin1Char('/'))) {
        *error = QStringLiteral("theme is outside the themes directory");
        return false;
    }
    if (info.fileName() == QLatin1String("theme.json")) {
        const QString dirPath = info.dir().canonicalPath();
        if (dirPath.isEmpty() || dirPath == root || !dirPath.startsWith(root + QLatin1Char('/'))) {
            *error = QStringLiteral("theme is outside the themes directory");
            return false;
        }
        if (!QDir(dirPath).removeRecursively()) {
            *error = QStringLiteral("cannot remove %1").arg(dirPath);
            return false;
        }
        return true;
    }
    if (!QFile::remove(canonical)) {
        *error = QStringLiteral("cannot remove %1").arg(canonical);
        return false;
    }
    return true;
}

QString ThemeManager::importTheme(const QUrl &url, bool replace)
{
    QTemporaryDir scratch;
    if (!scratch.isValid()) {
        setLastError(QStringLiteral("cannot create a temporary directory"));
        return {};
    }

    QString path;
    if (url.isLocalFile())
        path = url.toLocalFile();
    else if (url.scheme().isEmpty())
        path = url.toString();
    else {
        // content:// (and any other non-file URL Qt can open). The bytes are read
        // here and staged under a name that keeps the .json/.zip suffix importLocal
        // switches on.
        const std::unique_ptr<QFile> input = AndroidUri::openForRead(url);
        if (!input) {
            setLastError(QStringLiteral("cannot open %1").arg(url.toString()));
            return {};
        }
        const QByteArray data = input->read(kMaxThemeTotalBytes + 1);
        if (data.size() > kMaxThemeTotalBytes) {
            setLastError(QStringLiteral("theme file is too large"));
            return {};
        }
        path = scratch.filePath(stagedThemeFileName(url, data));
        QFile out(path);
        if (!out.open(QIODevice::WriteOnly) || out.write(data) != data.size()) {
            setLastError(QStringLiteral("cannot stage the theme file"));
            return {};
        }
    }
    if (path.isEmpty()) {
        setLastError(QStringLiteral("no theme file"));
        return {};
    }
    const QString id = importLocal(path, replace);
    if (id.isEmpty())
        return id;
    if (replace) {
        setThemeTrusted(id, false);
        if (m_currentTheme == id)
            reload();
    }
    invalidateCatalog();
    return id;
}

QString ThemeManager::importLocal(const QString &path, bool replace)
{
    const QFileInfo info(path);
    if (!info.exists()) {
        setLastError(QStringLiteral("theme file not found: %1").arg(path));
        return {};
    }
    if (info.isSymLink()) {
        setLastError(QStringLiteral("refusing to import a symlink"));
        return {};
    }

    auto acceptDirectory = [&](const QString &dir) {
        const QString themeFile = QDir(dir).filePath(QStringLiteral("theme.json"));
        if (!QFile::exists(themeFile)) {
            setLastError(QStringLiteral("no theme.json in %1").arg(dir));
            return QString();
        }
        if (QFileInfo(themeFile).isSymLink()) {
            setLastError(QStringLiteral("refusing to import a symlink"));
            return QString();
        }
        const QDir qmlDir(QDir(dir).filePath(QStringLiteral("qml")));
        if (qmlDir.exists()) {
            if (QFileInfo(qmlDir.absolutePath()).isSymLink()) {
                setLastError(QStringLiteral("refusing to import a symlinked qml directory"));
                return QString();
            }
            const QFileInfoList qmlFiles = qmlDir.entryInfoList({QStringLiteral("*.qml")}, QDir::Files);
            for (const QFileInfo &qmlFile : qmlFiles) {
                if (qmlFile.isSymLink() || !isValidSlotName(qmlFile.completeBaseName())) {
                    setLastError(QStringLiteral("theme qml override is not allowed: %1").arg(qmlFile.fileName()));
                    return QString();
                }
                const QString qmlRoot = QFileInfo(qmlDir.absolutePath()).canonicalFilePath();
                const QString qmlPath = qmlFile.canonicalFilePath();
                if (qmlRoot.isEmpty() || !qmlPath.startsWith(qmlRoot + QLatin1Char('/'))) {
                    setLastError(QStringLiteral("theme qml override escapes the theme folder"));
                    return QString();
                }
            }
        }
        QString error;
        const std::optional<Loaded> loaded = readTheme(dir, &error);
        if (!loaded) {
            setLastError(error.isEmpty() ? QStringLiteral("invalid theme") : error);
            return QString();
        }
        if (isBundledId(loaded->name)) {
            setLastError(QStringLiteral("“%1” already ships with Dafin").arg(loaded->name));
            return QString();
        }
        if (!isSafeThemeId(loaded->name)) {
            setLastError(QStringLiteral("theme name \"%1\" cannot be installed").arg(loaded->name));
            return QString();
        }
        if (!installDirectory(dir, loaded->name, replace, &error)) {
            setLastError(error);
            return QString();
        }
        setLastError({});
        return loaded->name;
    };

    if (info.isDir())
        return acceptDirectory(info.absoluteFilePath());

    const QString suffix = info.suffix().toLower();
    if (suffix == QLatin1String("zip")) {
        QTemporaryDir extracted;
        if (!extracted.isValid()) {
            setLastError(QStringLiteral("cannot create a temporary directory"));
            return {};
        }
        QString error;
        if (!extractZipTheme(info.absoluteFilePath(), extracted.path(), &error)) {
            setLastError(error.isEmpty() ? QStringLiteral("cannot read the theme archive") : error);
            return {};
        }
        return acceptDirectory(extracted.path());
    }
    if (suffix != QLatin1String("json")) {
        setLastError(QStringLiteral("theme import must be a .json file, a folder, or a .zip"));
        return {};
    }
    if (info.size() > kMaxThemeFileBytes) {
        setLastError(QStringLiteral("theme file is too large"));
        return {};
    }

    QString error;
    const std::optional<Loaded> loaded = readTheme(info.absoluteFilePath(), &error);
    if (!loaded) {
        setLastError(error.isEmpty() ? QStringLiteral("invalid theme") : error);
        return {};
    }
    if (isBundledId(loaded->name)) {
        setLastError(QStringLiteral("“%1” already ships with Dafin").arg(loaded->name));
        return {};
    }
    if (!isSafeThemeId(loaded->name)) {
        setLastError(QStringLiteral("theme name \"%1\" cannot be installed").arg(loaded->name));
        return {};
    }
    if (!installJsonFile(info.absoluteFilePath(), loaded->name, replace, &error)) {
        setLastError(error);
        return {};
    }
    setLastError({});
    return loaded->name;
}

bool ThemeManager::installJsonFile(const QString &sourceFile, const QString &id, bool replace,
                                   QString *error) const
{
    const QString root = userThemesDirectory();
    if (!QDir().mkpath(root)) {
        *error = QStringLiteral("cannot create the themes directory");
        return false;
    }
    const QString dest = QDir(root).filePath(id + QStringLiteral(".json"));
    const QString destDir = QDir(root).filePath(id);
    if (QFileInfo::exists(dest) || QFileInfo(destDir).exists()) {
        if (!replace) {
            *error = QStringLiteral("theme \"%1\" is already installed").arg(id);
            return false;
        }
        const QString existing = QFileInfo::exists(dest) ? dest : QDir(destDir).filePath(QStringLiteral("theme.json"));
        if (!removeInstalled(existing, error))
            return false;
    }
    if (!pathIsInside(root, dest)) {
        *error = QStringLiteral("theme id escapes the themes directory");
        return false;
    }
    if (!QFile::copy(sourceFile, dest)) {
        *error = QStringLiteral("cannot copy theme to %1").arg(dest);
        return false;
    }
    return true;
}

bool ThemeManager::installDirectory(const QString &sourceDir, const QString &id, bool replace,
                                    QString *error) const
{
    const QString root = userThemesDirectory();
    if (!QDir().mkpath(root)) {
        *error = QStringLiteral("cannot create the themes directory");
        return false;
    }
    const QString dest = QDir(root).filePath(id);
    const QString destFile = QDir(root).filePath(id + QStringLiteral(".json"));
    if (QFileInfo::exists(dest) || QFileInfo::exists(destFile)) {
        if (!replace) {
            *error = QStringLiteral("theme \"%1\" is already installed").arg(id);
            return false;
        }
        const QString existing = QFileInfo::exists(destFile) ? destFile : QDir(dest).filePath(QStringLiteral("theme.json"));
        if (!removeInstalled(existing, error))
            return false;
    }
    if (!pathIsInside(root, dest) || !QDir().mkpath(dest)) {
        *error = QStringLiteral("cannot create %1").arg(dest);
        return false;
    }

    const auto copyChecked = [&](const QString &from, const QString &to) {
        const QFileInfo fromInfo(from);
        if (fromInfo.isSymLink()) {
            *error = QStringLiteral("refusing to copy a symlink");
            return false;
        }
        if (fromInfo.size() > kMaxThemeFileBytes) {
            *error = QStringLiteral("theme file is too large");
            return false;
        }
        if (!pathIsInside(dest, to) || !QFile::copy(from, to)) {
            *error = QStringLiteral("cannot copy %1").arg(fromInfo.fileName());
            return false;
        }
        return true;
    };
    const auto cleanup = [&]() { QDir(dest).removeRecursively(); };

    if (!copyChecked(QDir(sourceDir).filePath(QStringLiteral("theme.json")),
                     QDir(dest).filePath(QStringLiteral("theme.json")))) {
        cleanup();
        return false;
    }
    for (const char *sideName : {"theme.dark.json", "theme.light.json"}) {
        const QString from = QDir(sourceDir).filePath(QLatin1String(sideName));
        if (!QFile::exists(from))
            continue;
        if (!copyChecked(from, QDir(dest).filePath(QLatin1String(sideName)))) {
            cleanup();
            return false;
        }
    }
    const QDir qmlDir(QDir(sourceDir).filePath(QStringLiteral("qml")));
    if (qmlDir.exists() && !QFileInfo(qmlDir.absolutePath()).isSymLink()) {
        const QString qmlDest = QDir(dest).filePath(QStringLiteral("qml"));
        if (!QDir().mkpath(qmlDest)) {
            *error = QStringLiteral("cannot create %1").arg(qmlDest);
            cleanup();
            return false;
        }
        const QFileInfoList qmlFiles = qmlDir.entryInfoList({QStringLiteral("*.qml")}, QDir::Files, QDir::Name);
        for (const QFileInfo &qmlFile : qmlFiles) {
            if (!isValidSlotName(qmlFile.completeBaseName()))
                continue;
            if (!copyChecked(qmlFile.absoluteFilePath(), QDir(qmlDest).filePath(qmlFile.fileName()))) {
                cleanup();
                return false;
            }
        }
    }
    return true;
}

bool ThemeManager::extractZipTheme(const QString &zipPath, const QString &destDir, QString *error) const
{
    QFile file(zipPath);
    if (!file.open(QIODevice::ReadOnly)) {
        *error = QStringLiteral("cannot open %1: %2").arg(zipPath, file.errorString());
        return false;
    }
    if (!zipIsSafe(file, error))
        return false;

    QString readError;
    const QList<drift::zip::Entry> entries = drift::zip::readEntries(file, &readError);
    if (entries.isEmpty() && !readError.isEmpty()) {
        *error = readError;
        return false;
    }

    struct Item
    {
        QString relative;
        drift::zip::Entry entry;
    };
    QList<Item> items;
    for (const drift::zip::Entry &entry : entries) {
        if (entry.isDir || isJunkZipPath(entry.path))
            continue;
        items.append({entry.path, entry});
    }

    QString prefix;
    if (!items.isEmpty()) {
        const int slash = items.first().relative.indexOf(QLatin1Char('/'));
        if (slash > 0) {
            const QString candidate = items.first().relative.left(slash + 1);
            bool shared = true;
            for (const Item &item : items) {
                if (!item.relative.startsWith(candidate)) {
                    shared = false;
                    break;
                }
            }
            if (shared)
                prefix = candidate;
        }
    }

    QStringList relatives;
    relatives.reserve(items.size());
    for (const Item &item : items)
        relatives.append(item.relative.mid(prefix.size()));
    bool hasThemeJson = false;
    for (const QString &relative : relatives) {
        if (relative == QLatin1String("theme.json"))
            hasThemeJson = true;
    }

    if (!QDir().mkpath(destDir)) {
        *error = QStringLiteral("cannot create a temporary directory");
        return false;
    }

    bool wroteTheme = false;
    bool wroteLoneJson = false;
    qint64 total = 0;
    for (int i = 0; i < items.size(); ++i) {
        const QString relative = relatives.at(i);
        if (relative.isEmpty())
            continue;
        const bool allowed = isAllowedThemeRelative(relative);
        const bool loneJson = !hasThemeJson && !relative.contains(QLatin1Char('/'))
                              && relative.endsWith(QLatin1String(".json"), Qt::CaseInsensitive);
        if (!allowed && !loneJson)
            continue;
        if (loneJson && wroteLoneJson) {
            *error = QStringLiteral("archive has more than one theme file");
            return false;
        }

        QByteArray data;
        if (!drift::zip::extractEntry(file, items.at(i).entry, data)) {
            *error = QStringLiteral("cannot extract %1").arg(items.at(i).relative);
            return false;
        }
        if (data.size() > kMaxThemeFileBytes || total + data.size() > kMaxThemeTotalBytes) {
            *error = QStringLiteral("theme archive is too large");
            return false;
        }
        total += data.size();

        const QString outRelative = loneJson ? QStringLiteral("theme.json") : relative;
        const QString outPath = QDir(destDir).filePath(outRelative);
        if (!pathIsInside(destDir, outPath)) {
            *error = QStringLiteral("theme archive path escapes the destination");
            return false;
        }
        if (!QDir().mkpath(QFileInfo(outPath).path())) {
            *error = QStringLiteral("cannot create %1").arg(QFileInfo(outPath).path());
            return false;
        }
        QFile out(outPath);
        if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate) || out.write(data) != data.size()) {
            *error = QStringLiteral("cannot write %1").arg(outRelative);
            return false;
        }
        if (outRelative == QLatin1String("theme.json"))
            wroteTheme = true;
        if (loneJson)
            wroteLoneJson = true;
    }
    if (!wroteTheme) {
        *error = QStringLiteral("archive does not contain a theme");
        return false;
    }
    return true;
}

#include "moc_ThemeManager.cpp"
