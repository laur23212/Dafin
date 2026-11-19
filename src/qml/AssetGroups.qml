pragma Singleton
import QtQuick
import Drift

// Dafin: the "My" area of the asset tabs and the font picker — ordered favorites and the user's
// own groups. The data lives in EditorState (AssetGroupStore, one list of ids per tab); this
// keeps what the chip bar, the cards' menus and the reorder drop areas share.
//
// Tab ids: effects, sounds, templates, transitions, shapes, stickers, titles, looks, fonts.
QtObject {
    id: groups

    readonly property string favoritesId: "__favorites__"
    // "Recently Used": filled when a card is applied or dropped (EditorState.noteAssetUsed),
    // newest first. A list like favorites, but it cannot be reordered.
    readonly property string recentId: "__recent__"

    // Bumped on every change, so a binding that reads lists through the functions below refreshes.
    property int tick: 0

    // tabId -> the list the tab shows now (a category, favorites or a group id). Cards read it for
    // "Remove from this group" and for whether a drag may reorder.
    property var activeLists: ({})

    // The card being dragged, by item id: a drag's payload is not always the id (video stickers
    // drag as media with an empty payload), so cards name themselves. The drag's source item
    // carries groupTab / groupItem; these two are the fallback, set when the drag starts.
    property string dragTab: ""
    property string dragItem: ""

    function isGroup(listId) {
        return typeof listId === "string" && listId.indexOf("grp-") === 0
    }

    function isRecent(listId) {
        return listId === recentId
    }

    function isMyList(listId) {
        return listId === favoritesId || listId === recentId || isGroup(listId)
    }

    // Counts the card as used: it leads the tab's Recently Used list.
    function noteUsed(tabId, itemId) {
        if (tabId && itemId)
            EditorState.noteAssetUsed(tabId, String(itemId))
    }

    function setActive(tabId, listId) {
        if (!tabId || activeLists[tabId] === listId)
            return
        const next = Object.assign({}, activeLists)
        next[tabId] = listId
        activeLists = next
    }

    function activeList(tabId) {
        return activeLists[tabId] || ""
    }

    function groupsOf(tabId) {
        void tick
        return tabId ? EditorState.assetGroups(tabId) : []
    }

    function inGroup(tabId, groupId, itemId) {
        void tick
        return EditorState.isInAssetGroup(tabId, groupId, itemId)
    }

    // The items of a list in the user's order. Ids whose item is gone (an unlinked folder, a
    // removed add-on) are skipped, not removed: they come back with their item.
    function pick(tabId, listId, all, keyOf) {
        void tick
        const byKey = Object.create(null)
        for (let i = 0; i < all.length; ++i) {
            const key = String(keyOf(all[i]))
            if (key.length > 0 && !(key in byKey))
                byKey[key] = all[i]
        }
        const ids = EditorState.assetListItems(tabId, listId)
        const out = []
        for (let j = 0; j < ids.length; ++j) {
            if (ids[j] in byKey)
                out.push(byKey[ids[j]])
        }
        return out
    }

    // The item id of the card a drag (DropArea drag or drop event) carries for tabId, or "".
    function draggedItem(tabId, drag) {
        if (!tabId)
            return ""
        const source = drag ? drag.source : null
        if (source && source.groupTab === tabId && typeof source.groupItem === "string"
                && source.groupItem.length > 0)
            return source.groupItem
        return dragTab === tabId ? dragItem : ""
    }

    function beginDrag(tabId, itemId) {
        dragTab = tabId || ""
        dragItem = itemId || ""
    }

    function endDrag() {
        dragTab = ""
        dragItem = ""
    }

    // Asks for a name over the window that holds `anchor` and calls done(name) with it.
    function askName(anchor, title, initial, done) {
        const win = anchor ? anchor.Window.window : null
        if (!win || !win.contentItem)
            return
        const dialog = _nameDialog.createObject(win.contentItem)
        if (!dialog)
            return
        dialog.submitted.connect(done)
        dialog.openWith(title, initial || "")
    }

    // "New group…" from a card's menu or the + chip: asks for the name, makes the group and,
    // when itemId is given, puts that item in it. done(groupId) runs after.
    function newGroup(anchor, tabId, itemId, done) {
        askName(anchor, qsTr("New group"), "", function(name) {
            const id = EditorState.addAssetGroup(tabId, name, itemId || "")
            if (id.length > 0 && done)
                done(id)
        })
    }

    property Component _nameDialog: Component {
        NameDialog {
            acceptText: qsTr("OK")
            placeholder: qsTr("Group name")
            onClosed: destroy()
        }
    }

    property Connections _watch: Connections {
        target: EditorState
        function onAssetGroupsChanged() {
            groups.tick++
        }
    }
}
