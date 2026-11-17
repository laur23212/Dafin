#include <QtTest>

#include <QCoreApplication>
#include <QSettings>
#include <QStandardPaths>

#include "app/editor/AssetGroupStore.h"

// The "My" area of the asset tabs: ordered favorites and the user's groups (AssetGroupStore).
class AssetGroupsTest : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void createRenameDelete();
    void addRemoveAndNoTwins();
    void moveReordersAndPersists();
    void favoritesKeepOrderAndPersist();
    void oldFavoritesCarryOver();
    void unknownIdsAreKeptAndHarmless();
    void damagedSettingsReadAsEmpty();
    void tabsAreSeparate();

private:
    QString m_org;
    QString m_app;
};

// Real settings, in a throwaway organisation under the test-mode config root.
void AssetGroupsTest::init()
{
    m_org = QCoreApplication::organizationName();
    m_app = QCoreApplication::applicationName();
    QStandardPaths::setTestModeEnabled(true);
    QCoreApplication::setOrganizationName(QStringLiteral("DafinAssetGroupsTest"));
    QCoreApplication::setApplicationName(QStringLiteral("DafinAssetGroupsTest"));
    QSettings().clear();
}

void AssetGroupsTest::cleanup()
{
    QSettings().clear();
    QCoreApplication::setOrganizationName(m_org);
    QCoreApplication::setApplicationName(m_app);
}

void AssetGroupsTest::createRenameDelete()
{
    AssetGroupStore store;
    QVERIFY(store.addGroup(QStringLiteral("effects"), QStringLiteral("   ")).isEmpty());

    const QString intros = store.addGroup(QStringLiteral("effects"), QStringLiteral(" Intros "));
    QVERIFY(intros.startsWith(QStringLiteral("grp-")));
    QCOMPARE(store.groups(QStringLiteral("effects")).at(0).name, QStringLiteral("Intros"));

    // A duplicate name (any case) is numbered, never a twin.
    const QString twin = store.addGroup(QStringLiteral("effects"), QStringLiteral("intros"));
    QVERIFY(!twin.isEmpty() && twin != intros);
    QCOMPARE(store.groups(QStringLiteral("effects")).at(1).name, QStringLiteral("intros 2"));

    QVERIFY(!store.renameGroup(QStringLiteral("effects"), intros, QString()));
    QVERIFY(store.renameGroup(QStringLiteral("effects"), intros, QStringLiteral("Openers")));
    QVERIFY(store.renameGroup(QStringLiteral("effects"), twin, QStringLiteral("Openers")));
    QCOMPARE(store.groups(QStringLiteral("effects")).at(1).name, QStringLiteral("Openers 2"));
    QVERIFY(!store.renameGroup(QStringLiteral("effects"), QStringLiteral("grp-nothere"), QStringLiteral("X")));

    // Deleting a group never touches favorites or the other groups' items.
    store.setFavorite(QStringLiteral("effects"), QStringLiteral("blur"), true);
    store.addToGroup(QStringLiteral("effects"), intros, QStringLiteral("blur"));
    store.addToGroup(QStringLiteral("effects"), twin, QStringLiteral("blur"));
    QVERIFY(store.removeGroup(QStringLiteral("effects"), intros));
    QVERIFY(!store.removeGroup(QStringLiteral("effects"), intros));
    QCOMPARE(store.groups(QStringLiteral("effects")).size(), 1);
    QVERIFY(store.isFavorite(QStringLiteral("effects"), QStringLiteral("blur")));
    QVERIFY(store.isInGroup(QStringLiteral("effects"), twin, QStringLiteral("blur")));

    store.reload();
    QCOMPARE(store.groups(QStringLiteral("effects")).size(), 1);
    QCOMPARE(store.groups(QStringLiteral("effects")).at(0).name, QStringLiteral("Openers 2"));
}

void AssetGroupsTest::addRemoveAndNoTwins()
{
    AssetGroupStore store;
    const QString g = store.addGroup(QStringLiteral("stickers"), QStringLiteral("Arrows"));
    QVERIFY(store.addToGroup(QStringLiteral("stickers"), g, QStringLiteral("a1")));
    QVERIFY(!store.addToGroup(QStringLiteral("stickers"), g, QStringLiteral("a1")));
    QVERIFY(!store.addToGroup(QStringLiteral("stickers"), g, QString()));
    QVERIFY(!store.addToGroup(QStringLiteral("stickers"), QStringLiteral("grp-missing"), QStringLiteral("a1")));
    QVERIFY(store.addToGroup(QStringLiteral("stickers"), g, QStringLiteral("a2")));
    QCOMPARE(store.items(QStringLiteral("stickers"), g), (QStringList{QStringLiteral("a1"), QStringLiteral("a2")}));
    QCOMPARE(store.groupsContaining(QStringLiteral("stickers"), QStringLiteral("a2")), QStringList{g});

    QVERIFY(store.removeFromGroup(QStringLiteral("stickers"), g, QStringLiteral("a1")));
    QVERIFY(!store.removeFromGroup(QStringLiteral("stickers"), g, QStringLiteral("a1")));
    QCOMPARE(store.items(QStringLiteral("stickers"), g), QStringList{QStringLiteral("a2")});
}

void AssetGroupsTest::moveReordersAndPersists()
{
    AssetGroupStore store;
    const QString g = store.addGroup(QStringLiteral("transitions"), QStringLiteral("Mine"));
    for (const char *id : {"a", "b", "c", "d"})
        store.addToGroup(QStringLiteral("transitions"), g, QString::fromLatin1(id));

    // Down: d takes b's place. Up: a takes d's place.
    QVERIFY(store.moveItem(QStringLiteral("transitions"), g, QStringLiteral("d"), QStringLiteral("b")));
    QCOMPARE(store.items(QStringLiteral("transitions"), g).join(QString()), QStringLiteral("adbc"));
    QVERIFY(store.moveItem(QStringLiteral("transitions"), g, QStringLiteral("a"), QStringLiteral("c")));
    QCOMPARE(store.items(QStringLiteral("transitions"), g).join(QString()), QStringLiteral("dbca"));
    QVERIFY(!store.moveItem(QStringLiteral("transitions"), g, QStringLiteral("a"), QStringLiteral("a")));
    QVERIFY(!store.moveItem(QStringLiteral("transitions"), g, QStringLiteral("zz"), QStringLiteral("a")));
    QVERIFY(!store.moveItem(QStringLiteral("transitions"), QStringLiteral("grp-x"), QStringLiteral("a"), QStringLiteral("b")));

    store.reload();
    QCOMPARE(store.items(QStringLiteral("transitions"), g).join(QString()), QStringLiteral("dbca"));
}

void AssetGroupsTest::favoritesKeepOrderAndPersist()
{
    AssetGroupStore store;
    QVERIFY(store.toggleFavorite(QStringLiteral("fonts"), QStringLiteral("Roboto")));
    QVERIFY(store.toggleFavorite(QStringLiteral("fonts"), QStringLiteral("Kanit")));
    QVERIFY(store.toggleFavorite(QStringLiteral("fonts"), QStringLiteral("Anton")));
    const QString fav = AssetGroupStore::favoritesId();
    QVERIFY(store.moveItem(QStringLiteral("fonts"), fav, QStringLiteral("Anton"), QStringLiteral("Roboto")));
    QCOMPARE(store.favorites(QStringLiteral("fonts")),
             (QStringList{QStringLiteral("Anton"), QStringLiteral("Roboto"), QStringLiteral("Kanit")}));
    QVERIFY(!store.toggleFavorite(QStringLiteral("fonts"), QStringLiteral("Roboto")));

    AssetGroupStore restarted;
    QCOMPARE(restarted.favorites(QStringLiteral("fonts")), (QStringList{QStringLiteral("Anton"), QStringLiteral("Kanit")}));
    QCOMPARE(restarted.items(QStringLiteral("fonts"), fav), restarted.favorites(QStringLiteral("fonts")));
}

void AssetGroupsTest::oldFavoritesCarryOver()
{
    {
        QSettings settings;
        settings.setValue(QStringLiteral("assetFavorites/looks"),
                          QStringList{QStringLiteral("teal"), QStringLiteral("warm"), QStringLiteral("teal")});
    }
    AssetGroupStore store;
    QCOMPARE(store.favorites(QStringLiteral("looks")), (QStringList{QStringLiteral("teal"), QStringLiteral("warm")}));
    QVERIFY(store.isFavorite(QStringLiteral("looks"), QStringLiteral("warm")));
    // Moved, not copied: the old key is gone, so removing a favorite can't bring it back.
    QVERIFY(!QSettings().contains(QStringLiteral("assetFavorites/looks")));
    store.setFavorite(QStringLiteral("looks"), QStringLiteral("teal"), false);
    AssetGroupStore restarted;
    QCOMPARE(restarted.favorites(QStringLiteral("looks")), QStringList{QStringLiteral("warm")});
}

void AssetGroupsTest::unknownIdsAreKeptAndHarmless()
{
    AssetGroupStore store;
    const QString g = store.addGroup(QStringLiteral("effects"), QStringLiteral("Linked folder"));
    store.addToGroup(QStringLiteral("effects"), g, QStringLiteral("gone-with-the-folder"));
    store.addToGroup(QStringLiteral("effects"), g, QStringLiteral("blur"));
    // The store doesn't know which items exist: a vanished one stays (the tab hides it) and can
    // be moved past or removed like any other.
    QVERIFY(store.moveItem(QStringLiteral("effects"), g, QStringLiteral("blur"), QStringLiteral("gone-with-the-folder")));
    QCOMPARE(store.items(QStringLiteral("effects"), g).first(), QStringLiteral("blur"));
    QVERIFY(store.items(QStringLiteral("effects"), QStringLiteral("grp-unknown")).isEmpty());
    QVERIFY(store.items(QStringLiteral("nosuchtab"), g).isEmpty());
    QVERIFY(store.groups(QString()).isEmpty());
    QVERIFY(store.addGroup(QStringLiteral("bad/tab"), QStringLiteral("x")).isEmpty());
}

void AssetGroupsTest::damagedSettingsReadAsEmpty()
{
    {
        QSettings settings;
        settings.setValue(QStringLiteral("assetGroups/shapes"), QStringLiteral("{not json"));
        settings.setValue(QStringLiteral("assetGroups/sounds"), QStringLiteral("[1,2,3]"));
        // Half-broken: wrong types, a bad id, a twin id, a blank and a twin name, doubled items.
        settings.setValue(QStringLiteral("assetGroups/looks"), QStringLiteral(
            R"({"favorites":[1,"a","a",""],"groups":[{"id":"grp-1","name":"","items":["x","x",3]},)"
            R"({"id":"grp-1","name":"dup id"},{"id":"bad","name":"no prefix"},)"
            R"({"id":"grp-2","name":"Group","items":"notalist"},"junk"]})"));
    }
    AssetGroupStore store;
    QVERIFY(store.favorites(QStringLiteral("shapes")).isEmpty());
    QVERIFY(store.groups(QStringLiteral("shapes")).isEmpty());
    QVERIFY(store.groups(QStringLiteral("sounds")).isEmpty());
    QCOMPARE(store.favorites(QStringLiteral("looks")), QStringList{QStringLiteral("a")});
    const auto groups = store.groups(QStringLiteral("looks"));
    QCOMPARE(groups.size(), 2);
    QCOMPARE(groups.at(0).name, QStringLiteral("Group"));
    QCOMPARE(groups.at(0).items, QStringList{QStringLiteral("x")});
    QCOMPARE(groups.at(1).name, QStringLiteral("Group 2"));
    QVERIFY(groups.at(1).items.isEmpty());
    // Still usable afterwards.
    QVERIFY(!store.addGroup(QStringLiteral("shapes"), QStringLiteral("Fine")).isEmpty());
}

void AssetGroupsTest::tabsAreSeparate()
{
    AssetGroupStore store;
    store.setFavorite(QStringLiteral("effects"), QStringLiteral("x"), true);
    QVERIFY(!store.isFavorite(QStringLiteral("stickers"), QStringLiteral("x")));
    const QString g = store.addGroup(QStringLiteral("effects"), QStringLiteral("Same"));
    QCOMPARE(store.groups(QStringLiteral("stickers")).size(), 0);
    QVERIFY(!store.addToGroup(QStringLiteral("stickers"), g, QStringLiteral("x")));
    // The same name in another tab is not a duplicate.
    store.addGroup(QStringLiteral("stickers"), QStringLiteral("Same"));
    QCOMPARE(store.groups(QStringLiteral("stickers")).at(0).name, QStringLiteral("Same"));
}

QTEST_GUILESS_MAIN(AssetGroupsTest)
#include "tst_assetgroups.moc"
