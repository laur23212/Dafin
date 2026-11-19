import QtQuick
import Drift
// Dafin: drag-to-reorder for the "My" lists (favorites and the user's groups). Laid over a
// GridView or ListView of cards; while the tab shows one of those lists, a card of this tab
// dropped on another card's place takes that place (first, second, and so on), and the order is
// saved. Any other drag passes through untouched, so cards still drag onto the timeline and the
// preview exactly as before.
DropArea {
    id: root

    property Item view: null
    property string tabId: ""
    property string listId: ""
    // The item id at a model index of the view.
    property var keyAt: function(index) { return "" }

    // The card being dragged, set when a drag of this tab's card enters during a "My" list.
    property string draggedId: ""
    readonly property bool armed: draggedId.length > 0
    property int hoverIndex: -1
    property rect marker: Qt.rect(0, 0, 0, 0)

    function indexAt(x, y) {
        if (!root.view)
            return -1
        const p = root.mapToItem(root.view.contentItem, x, y)
        return root.view.indexAt(p.x, p.y)
    }

    function track(x, y) {
        const i = indexAt(x, y)
        root.hoverIndex = i
        const item = i >= 0 && root.view ? root.view.itemAtIndex(i) : null
        if (!item) {
            root.marker = Qt.rect(0, 0, 0, 0)
            return
        }
        const p = item.mapToItem(root, 0, 0)
        root.marker = Qt.rect(p.x, p.y, item.width, item.height)
    }

    onEntered: (drag) => {
        root.draggedId = (AssetGroups.isMyList(root.listId) && !AssetGroups.isRecent(root.listId)) ? AssetGroups.draggedItem(root.tabId, drag) : ""
        if (!root.armed) {
            drag.accepted = false
            return
        }
        drag.accept(Qt.CopyAction)
        track(drag.x, drag.y)
    }
    onPositionChanged: (drag) => {
        if (root.armed)
            track(drag.x, drag.y)
    }
    onExited: {
        root.hoverIndex = -1
        root.draggedId = ""
    }
    onDropped: (drop) => {
        const i = indexAt(drop.x, drop.y)
        const moving = root.draggedId
        root.hoverIndex = -1
        root.draggedId = ""
        if (moving.length === 0)
            return
        drop.accept(Qt.CopyAction)
        const target = i >= 0 ? String(root.keyAt(i)) : ""
        if (target.length > 0 && target !== moving)
            EditorState.moveAssetInList(root.tabId, root.listId, moving, target)
    }

    // Where the card will land.
    Rectangle {
        visible: root.containsDrag && root.armed && root.hoverIndex >= 0 && root.marker.width > 0
        x: root.marker.x - 2
        y: root.marker.y - 2
        width: root.marker.width + 4 - Theme.assetCardGap
        height: root.marker.height + 4 - Theme.assetCardGap
        radius: 6
        color: "transparent"
        border.width: 2
        border.color: Theme.c.themedChip.primaryBackground
    }
}
