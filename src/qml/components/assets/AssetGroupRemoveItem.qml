import QtQuick
import Drift
// Dafin: "Remove from this group" in an asset card's menu, shown while the tab shows one of the
// user's groups and the item is in it. Removes it from the group only.
ThemedMenuItem {
    id: root

    property string tabId: ""
    property string itemId: ""
    readonly property string groupId: {
        const list = AssetGroups.activeLists[root.tabId] || ""
        return AssetGroups.isGroup(list) ? list : ""
    }

    visible: groupId.length > 0 && AssetGroups.inGroup(tabId, groupId, itemId)
    height: visible ? implicitHeight : 0
    text: qsTr("Remove from this group")
    icon.name: Theme.icons.x
    onTriggered: EditorState.removeFromAssetGroup(root.tabId, root.groupId, root.itemId)
}
