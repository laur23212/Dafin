import QtQuick
import Drift

Rectangle {
    property string selectedFamily: ""
    property real rowWidth: 0
    // Dafin: favorites or one of the user's groups is shown: a handle drags the row to a new place.
    property bool reorderable: false
    signal familyChosen(string familyName)
    signal removeRequested(string familyName)
    id: row
    required property string name
    required property string previewFamily
    required property bool removable

    width: rowWidth
    height: 34
    color: rowMouse.containsMouse ? Theme.c.fontPickerFamilyRow.backgroundHover
                                  : (row.name === selectedFamily ? Theme.c.fontPickerFamilyRow.backgroundSelected
                                                              : "transparent")

    Behavior on color {
        ColorAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
    }

    // Dafin: in favorites and groups the row drags (by the handle) onto another row's place.
    readonly property string groupTab: "fonts"
    readonly property string groupItem: row.name
    Drag.active: rowDrag.active
    Drag.dragType: Drag.Automatic
    Drag.supportedActions: Qt.CopyAction
    Drag.mimeData: ({ "application/x-dafin-asset-reorder": row.name })

    Text {
        anchors.left: dragHandle.visible ? dragHandle.right : parent.left
        anchors.leftMargin: dragHandle.visible ? 2 : 8
        anchors.right: favButton.left
        anchors.rightMargin: 4
        anchors.verticalCenter: parent.verticalCenter
        text: row.name
        font.family: row.previewFamily === "" ? Theme.fontFamily : row.previewFamily
        font.pixelSize: 18
        color: Theme.c.fontPickerFamilyRow.foreground
        elide: Text.ElideRight
    }

    // Hover for the star: a HoverHandler still sees the pointer over the buttons on the row.
    HoverHandler {
        id: rowHover
    }

    MouseArea {
        id: rowMouse
        anchors.fill: parent
        hoverEnabled: true
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        cursorShape: Qt.PointingHandCursor
        onClicked: (mouse) => {
            if (mouse.button === Qt.RightButton)
                rowMenu.popup()
            else
                row.familyChosen(row.name)
        }
    }

    IconGlyph {
        id: dragHandle
        visible: row.reorderable
        anchors.left: parent.left
        anchors.leftMargin: 4
        anchors.verticalCenter: parent.verticalCenter
        width: visible ? 16 : 0
        glyph: Theme.icons.gripVertical
        iconSize: 12
        iconColor: Theme.c.fontPickerFamilyRow.foreground
        opacity: rowHover.hovered || rowDrag.active ? 0.9 : 0.4

        HoverHandler {
            cursorShape: rowDrag.active ? Qt.ClosedHandCursor : Qt.OpenHandCursor
        }

        DragHandler {
            id: rowDrag
            target: null
            enabled: row.reorderable
            acceptedButtons: Qt.LeftButton
            onActiveChanged: active ? AssetGroups.beginDrag("fonts", row.name) : AssetGroups.endDrag()
        }
    }

    AssetFavoriteButton {
        id: favButton
        anchors.right: removeButton.visible ? removeButton.left : parent.right
        anchors.rightMargin: 4
        anchors.verticalCenter: parent.verticalCenter
        visible: rowHover.hovered || favorited
        tabId: "fonts"
        itemId: row.name
    }

    IconButton {
        id: removeButton
        visible: row.removable
        anchors.right: parent.right
        anchors.rightMargin: 4
        anchors.verticalCenter: parent.verticalCenter
        glyph: Theme.icons.x
        variant: "ghost"
        buttonSize: 24
        iconSize: 12
        tooltip: qsTr("Remove font")
        onClicked: row.removeRequested(row.name)
    }

    ThemedContextMenu {
        id: rowMenu
        ThemedMenuItem {
            text: AssetGroups.tick >= 0 && EditorState.isAssetFavorite("fonts", row.name)
                  ? qsTr("Remove from favorites") : qsTr("Add to favorites")
            icon.name: Theme.icons.star
            onTriggered: EditorState.toggleAssetFavorite("fonts", row.name)
        }
        AssetGroupSubmenu {
            tabId: "fonts"
            itemId: row.name
            anchorItem: row
        }
        AssetGroupRemoveItem {
            tabId: "fonts"
            itemId: row.name
        }
    }
}
