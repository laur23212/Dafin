import QtQuick
import QtQuick.Controls.Basic
import Drift
// Dafin: "Add to group ▸" in an asset card's right-click menu: the tab's groups (a tick on the
// ones the item is in; choosing one of those takes it out again), then "New group…".
ThemedContextMenu {
    id: root

    property string tabId: ""
    property string itemId: ""
    // Something in the card's window, to put the name prompt over.
    property Item anchorItem: null

    property var options: []

    title: qsTr("Add to group")
    enabled: tabId.length > 0 && itemId.length > 0

    function refresh() {
        const list = EditorState.assetGroups(root.tabId)
        const out = []
        for (let i = 0; i < list.length; ++i)
            out.push({ "id": list[i].id, "name": list[i].name,
                       "member": EditorState.isInAssetGroup(root.tabId, list[i].id, root.itemId) })
        root.options = out
    }

    onAboutToShow: refresh()

    Instantiator {
        model: root.options
        delegate: ThemedMenuItem {
            required property var modelData
            text: modelData.name
            icon.name: modelData.member ? Theme.icons.check : ""
            onTriggered: modelData.member
                         ? EditorState.removeFromAssetGroup(root.tabId, modelData.id, root.itemId)
                         : EditorState.addToAssetGroup(root.tabId, modelData.id, root.itemId)
        }
        onObjectAdded: (i, object) => root.insertItem(i, object)
        onObjectRemoved: (i, object) => root.removeItem(object)
    }

    ThemedMenuItem {
        text: qsTr("New group…")
        icon.name: Theme.icons.plus
        onTriggered: AssetGroups.newGroup(root.anchorItem, root.tabId, root.itemId, null)
    }
}
