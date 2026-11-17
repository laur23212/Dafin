#pragma once

#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>

// Dafin: the user's own "My" area of every asset tab (effects, transitions, stickers, shapes,
// audio effects, effect templates, titles, looks, fonts): an ordered favorites list and the
// user's named groups, each an ordered list of item ids. Keyed by tab id ("effects", "fonts", …).
//
// Same design as TemplateCategoryStore: stable ids, so renaming touches nothing else, and
// deleting a group never deletes an item. Items are only ids; the tab decides what they are, so
// an id whose item is gone (an unlinked folder, a removed add-on) stays stored and is simply not
// shown, and comes back if the item does.
//
// Persisted in QSettings, one JSON value per tab under "assetGroups/<tab>". The old unordered
// favorites ("assetFavorites/<tab>") move in the first time a tab is read. Damaged values read as
// an empty tab rather than failing.
class AssetGroupStore
{
public:
    struct Group
    {
        QString id; // "grp-…"
        QString name;
        QStringList items;
    };

    // The favorites list answers to this id wherever a list id is taken.
    static QString favoritesId() { return QStringLiteral("__favorites__"); }
    static bool isGroupId(const QString &listId) { return listId.startsWith(QLatin1String("grp-")); }

    QStringList favorites(const QString &tab) const;
    bool isFavorite(const QString &tab, const QString &itemId) const;
    // Appends to the end, or removes. Returns whether the item is a favorite now.
    bool toggleFavorite(const QString &tab, const QString &itemId);
    bool setFavorite(const QString &tab, const QString &itemId, bool favorite);

    QList<Group> groups(const QString &tab) const; // in the user's order
    // Returns the new group's id; empty for a blank name. A name already used in this tab is
    // numbered ("Intros 2") instead of making a twin.
    QString addGroup(const QString &tab, const QString &name);
    // Blank names are refused; a name another group of the tab already has is numbered.
    bool renameGroup(const QString &tab, const QString &groupId, const QString &name);
    // Removes the group only; its items stay wherever else they are.
    bool removeGroup(const QString &tab, const QString &groupId);
    bool addToGroup(const QString &tab, const QString &groupId, const QString &itemId);
    bool removeFromGroup(const QString &tab, const QString &groupId, const QString &itemId);
    bool isInGroup(const QString &tab, const QString &groupId, const QString &itemId) const;
    QStringList groupsContaining(const QString &tab, const QString &itemId) const;

    // The ordered ids of a list: favoritesId() or a group id. Unknown list: empty.
    QStringList items(const QString &tab, const QString &listId) const;
    // Puts itemId where targetId is now (the rest shift by one), in favorites or a group.
    bool moveItem(const QString &tab, const QString &listId, const QString &itemId, const QString &targetId);

    // Forgets what was read, so the next call reads the settings again (a restart, in tests).
    void reload();

    static QString uniqueName(const QStringList &taken, const QString &wanted);

private:
    struct Tab
    {
        QStringList favorites;
        QList<Group> groups;
    };

    Tab &tab(const QString &tabId) const; // reads the tab on first use
    static Tab parse(const QByteArray &json);
    static QByteArray serialise(const Tab &tab);
    void save(const QString &tabId) const;
    Group *findGroup(const QString &tabId, const QString &groupId) const;
    QStringList *list(const QString &tabId, const QString &listId) const;

    mutable QHash<QString, Tab> m_tabs;
};
