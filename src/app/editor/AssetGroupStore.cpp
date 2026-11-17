#include "app/editor/AssetGroupStore.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QSettings>
#include <QUuid>

namespace {

const QString kGroupsKey = QStringLiteral("assetGroups");
const QString kOldFavoritesKey = QStringLiteral("assetFavorites");

// A tab id becomes a settings key, so one that would open a sub-group is not a tab.
bool validTab(const QString &tab)
{
    return !tab.isEmpty() && !tab.contains(QLatin1Char('/')) && !tab.contains(QLatin1Char('\\'));
}

// Strings only, no blanks, each id once (the first place it appears wins).
QStringList cleanIds(const QJsonArray &array)
{
    QStringList out;
    QSet<QString> seen;
    for (const QJsonValue &v : array) {
        const QString id = v.toString();
        if (id.isEmpty() || seen.contains(id))
            continue;
        seen.insert(id);
        out.append(id);
    }
    return out;
}

QStringList cleanIds(const QStringList &ids)
{
    QJsonArray array;
    for (const QString &id : ids)
        array.append(id);
    return cleanIds(array);
}

} // namespace

QString AssetGroupStore::uniqueName(const QStringList &taken, const QString &wanted)
{
    const QString base = wanted.simplified();
    if (base.isEmpty())
        return {};
    auto used = [&taken](const QString &name) {
        for (const QString &t : taken)
            if (t.compare(name, Qt::CaseInsensitive) == 0)
                return true;
        return false;
    };
    if (!used(base))
        return base;
    for (int n = 2;; ++n) {
        const QString candidate = QStringLiteral("%1 %2").arg(base).arg(n);
        if (!used(candidate))
            return candidate;
    }
}

AssetGroupStore::Tab AssetGroupStore::parse(const QByteArray &json)
{
    Tab tab;
    QJsonParseError error{};
    const QJsonDocument doc = QJsonDocument::fromJson(json, &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject())
        return tab;
    const QJsonObject root = doc.object();
    tab.favorites = cleanIds(root.value(QStringLiteral("favorites")).toArray());
    QSet<QString> groupIds;
    QStringList names;
    for (const QJsonValue &v : root.value(QStringLiteral("groups")).toArray()) {
        const QJsonObject o = v.toObject();
        Group g;
        g.id = o.value(QStringLiteral("id")).toString();
        if (!isGroupId(g.id) || groupIds.contains(g.id))
            continue;
        // A damaged or doubled name is kept usable rather than dropping the group and its items.
        g.name = uniqueName(names, o.value(QStringLiteral("name")).toString());
        if (g.name.isEmpty())
            g.name = uniqueName(names, QStringLiteral("Group"));
        g.items = cleanIds(o.value(QStringLiteral("items")).toArray());
        groupIds.insert(g.id);
        names.append(g.name);
        tab.groups.append(g);
    }
    return tab;
}

QByteArray AssetGroupStore::serialise(const Tab &tab)
{
    QJsonArray groups;
    for (const Group &g : tab.groups)
        groups.append(QJsonObject{{QStringLiteral("id"), g.id},
                                  {QStringLiteral("name"), g.name},
                                  {QStringLiteral("items"), QJsonArray::fromStringList(g.items)}});
    const QJsonObject root{{QStringLiteral("version"), 1},
                           {QStringLiteral("favorites"), QJsonArray::fromStringList(tab.favorites)},
                           {QStringLiteral("groups"), groups}};
    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

AssetGroupStore::Tab &AssetGroupStore::tab(const QString &tabId) const
{
    auto it = m_tabs.find(tabId);
    if (it != m_tabs.end())
        return it.value();

    Tab loaded;
    QSettings settings;
    const QString key = kGroupsKey + QLatin1Char('/') + tabId;
    if (settings.contains(key)) {
        loaded = parse(settings.value(key).toString().toUtf8());
    } else {
        // First read since favorites became ordered: the old set moves in, kept as it was stored.
        const QString oldKey = kOldFavoritesKey + QLatin1Char('/') + tabId;
        const QStringList old = cleanIds(settings.value(oldKey).toStringList());
        if (!old.isEmpty()) {
            loaded.favorites = old;
            settings.setValue(key, QString::fromUtf8(serialise(loaded)));
            settings.remove(oldKey);
        }
    }
    return m_tabs.insert(tabId, loaded).value();
}

void AssetGroupStore::save(const QString &tabId) const
{
    QSettings settings;
    settings.setValue(kGroupsKey + QLatin1Char('/') + tabId, QString::fromUtf8(serialise(m_tabs.value(tabId))));
}

void AssetGroupStore::reload()
{
    m_tabs.clear();
}

AssetGroupStore::Group *AssetGroupStore::findGroup(const QString &tabId, const QString &groupId) const
{
    for (Group &g : tab(tabId).groups)
        if (g.id == groupId)
            return &g;
    return nullptr;
}

QStringList *AssetGroupStore::list(const QString &tabId, const QString &listId) const
{
    if (listId == favoritesId())
        return &tab(tabId).favorites;
    Group *g = findGroup(tabId, listId);
    return g ? &g->items : nullptr;
}

// --- favorites ---------------------------------------------------------------------------------

QStringList AssetGroupStore::favorites(const QString &tabId) const
{
    return validTab(tabId) ? tab(tabId).favorites : QStringList();
}

bool AssetGroupStore::isFavorite(const QString &tabId, const QString &itemId) const
{
    return validTab(tabId) && !itemId.isEmpty() && tab(tabId).favorites.contains(itemId);
}

bool AssetGroupStore::setFavorite(const QString &tabId, const QString &itemId, bool favorite)
{
    if (!validTab(tabId) || itemId.isEmpty())
        return false;
    QStringList &ids = tab(tabId).favorites;
    const bool has = ids.contains(itemId);
    if (has == favorite)
        return false;
    if (favorite)
        ids.append(itemId);
    else
        ids.removeAll(itemId);
    save(tabId);
    return true;
}

bool AssetGroupStore::toggleFavorite(const QString &tabId, const QString &itemId)
{
    const bool now = !isFavorite(tabId, itemId);
    setFavorite(tabId, itemId, now);
    return isFavorite(tabId, itemId);
}

// --- groups ------------------------------------------------------------------------------------

QList<AssetGroupStore::Group> AssetGroupStore::groups(const QString &tabId) const
{
    return validTab(tabId) ? tab(tabId).groups : QList<Group>();
}

QString AssetGroupStore::addGroup(const QString &tabId, const QString &name)
{
    if (!validTab(tabId))
        return {};
    Tab &t = tab(tabId);
    QStringList names;
    for (const Group &g : std::as_const(t.groups))
        names.append(g.name);
    Group g;
    g.name = uniqueName(names, name);
    if (g.name.isEmpty())
        return {};
    do {
        g.id = QStringLiteral("grp-") + QUuid::createUuid().toString(QUuid::WithoutBraces).left(8);
    } while (findGroup(tabId, g.id));
    t.groups.append(g);
    save(tabId);
    return g.id;
}

bool AssetGroupStore::renameGroup(const QString &tabId, const QString &groupId, const QString &name)
{
    if (!validTab(tabId) || name.simplified().isEmpty())
        return false;
    Group *target = findGroup(tabId, groupId);
    if (!target)
        return false;
    QStringList others;
    for (const Group &g : std::as_const(tab(tabId).groups))
        if (g.id != groupId)
            others.append(g.name);
    const QString unique = uniqueName(others, name);
    if (unique == target->name)
        return true;
    target->name = unique;
    save(tabId);
    return true;
}

bool AssetGroupStore::removeGroup(const QString &tabId, const QString &groupId)
{
    if (!validTab(tabId))
        return false;
    QList<Group> &groups = tab(tabId).groups;
    for (int i = 0; i < groups.size(); ++i) {
        if (groups.at(i).id == groupId) {
            groups.removeAt(i);
            save(tabId);
            return true;
        }
    }
    return false;
}

bool AssetGroupStore::addToGroup(const QString &tabId, const QString &groupId, const QString &itemId)
{
    if (!validTab(tabId) || itemId.isEmpty())
        return false;
    Group *g = findGroup(tabId, groupId);
    if (!g || g->items.contains(itemId))
        return false;
    g->items.append(itemId);
    save(tabId);
    return true;
}

bool AssetGroupStore::removeFromGroup(const QString &tabId, const QString &groupId, const QString &itemId)
{
    if (!validTab(tabId))
        return false;
    Group *g = findGroup(tabId, groupId);
    if (!g || g->items.removeAll(itemId) == 0)
        return false;
    save(tabId);
    return true;
}

bool AssetGroupStore::isInGroup(const QString &tabId, const QString &groupId, const QString &itemId) const
{
    if (!validTab(tabId))
        return false;
    const Group *g = findGroup(tabId, groupId);
    return g && g->items.contains(itemId);
}

QStringList AssetGroupStore::groupsContaining(const QString &tabId, const QString &itemId) const
{
    QStringList out;
    if (!validTab(tabId))
        return out;
    for (const Group &g : std::as_const(tab(tabId).groups))
        if (g.items.contains(itemId))
            out.append(g.id);
    return out;
}

QStringList AssetGroupStore::items(const QString &tabId, const QString &listId) const
{
    if (!validTab(tabId))
        return {};
    const QStringList *ids = list(tabId, listId);
    return ids ? *ids : QStringList();
}

bool AssetGroupStore::moveItem(const QString &tabId, const QString &listId, const QString &itemId,
                               const QString &targetId)
{
    if (!validTab(tabId) || itemId == targetId)
        return false;
    QStringList *ids = list(tabId, listId);
    if (!ids)
        return false;
    const int from = ids->indexOf(itemId);
    const int to = ids->indexOf(targetId);
    if (from < 0 || to < 0)
        return false;
    ids->move(from, to);
    save(tabId);
    return true;
}
