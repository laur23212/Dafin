import QtQuick
import QtQuick.Controls.Basic
import Drift
// Category filter row: favorites star, then horizontal category chips.
// ⭐ | [Color] [Glitch] …
//
// Dafin: with a tabId, the row starts with the user's "My" area: ⭐ Favorites, then their own
// groups and a + chip that makes a new one: ⭐ [Intros] [Client A] [+] [My Effects] | [Color] …
// Right-click a group to rename or delete it (deleting never deletes items). A card dropped on
// ⭐ or on a group goes into it. The lists themselves are AssetGroups / EditorState.
Item {
    id: root

    readonly property string favoritesId: "__favorites__"
    // "My effects": what the user imported or dropped in by hand. Off for pickers with no such notion.
    readonly property string mineId: "__mine__"
    property bool showMine: false
    property string mineLabel: qsTr("My Effects")

    // Dafin: the tab's id in AssetGroupStore; empty = no groups (favorites star only).
    property string tabId: ""
    readonly property var groups: tabId.length > 0 ? AssetGroups.groupsOf(tabId) : []
    readonly property bool showsMyList: AssetGroups.isMyList(activeCategory)

    property var categories: []
    property string activeCategory: ""
    property bool searching: false

    signal categoryActivated(string categoryId)

    width: parent ? parent.width : 0
    height: visible ? Theme.controlHeightSm : 0
    visible: !searching && (categories.length > 0 || showMine || tabId.length > 0)

    onActiveCategoryChanged: {
        AssetGroups.setActive(root.tabId, root.activeCategory)
        checkGroup()
    }
    Component.onCompleted: {
        AssetGroups.setActive(root.tabId, root.activeCategory)
        checkGroup()
    }
    onGroupsChanged: checkGroup()

    // The group being shown is gone (deleted, or a remembered tab from before): back to the
    // first category, or Favorites.
    function groupMissing() {
        if (root.tabId.length === 0 || !AssetGroups.isGroup(root.activeCategory))
            return false
        for (let i = 0; i < root.groups.length; ++i) {
            if (root.groups[i].id === root.activeCategory)
                return false
        }
        return true
    }

    function checkGroup() {
        if (root.groupMissing())
            Qt.callLater(root.leaveGroup)
    }

    function leaveGroup() {
        if (root.groupMissing())
            root.categoryActivated(root.categories.length > 0 ? root.categories[0].id : root.favoritesId)
    }


    Row {
        id: row
        anchors.fill: parent
        anchors.leftMargin: Theme.pagePadding
        anchors.rightMargin: Theme.pagePadding
        spacing: Theme.spacingSm

        IconButton {
            id: favButton
            anchors.verticalCenter: parent.verticalCenter
            glyph: Theme.icons.star
            variant: "ghost"
            buttonSize: Theme.controlHeightSm
            iconSize: 14
            active: root.activeCategory === root.favoritesId || favDrop.containsDrag
            tooltip: qsTr("Favorites")
            onClicked: root.categoryActivated(root.favoritesId)

            DropArea {
                id: favDrop
                anchors.fill: parent
                // A card of this tab dropped on ⭐ becomes a favorite.
                onEntered: (drag) => { drag.accepted = AssetGroups.draggedItem(root.tabId, drag).length > 0 }
                onDropped: (drop) => {
                    const item = AssetGroups.draggedItem(root.tabId, drop)
                    if (item.length === 0)
                        return
                    drop.accept(Qt.CopyAction)
                    if (!EditorState.isAssetFavorite(root.tabId, item))
                        EditorState.toggleAssetFavorite(root.tabId, item)
                }
            }
        }

        // Dafin: Recently Used, the last cards applied or dropped from this tab, newest first.
        IconButton {
            id: recentButton
            visible: root.tabId.length > 0
            anchors.verticalCenter: parent.verticalCenter
            glyph: Theme.icons.clock
            variant: "ghost"
            buttonSize: Theme.controlHeightSm
            iconSize: 14
            active: root.activeCategory === AssetGroups.recentId
            tooltip: qsTr("Recently used")
            onClicked: root.categoryActivated(AssetGroups.recentId)
        }

        ScrollStrip {
            width: Math.max(0, row.width - favButton.width - (recentButton.visible ? recentButton.width + row.spacing : 0) - row.spacing)
            height: parent.height

            Row {
                id: categoryRow
                height: parent.height
                spacing: Theme.spacingSm

                // Dafin: the user's groups, in their order.
                Repeater {
                    model: root.groups
                    delegate: ThemedChip {
                        id: groupChip
                        required property var modelData
                        anchors.verticalCenter: parent.verticalCenter
                        text: modelData.name
                        variant: "outline"
                        selected: root.activeCategory === modelData.id || groupDrop.containsDrag
                        tooltip: qsTr("%1 · %n item(s). Right-click to rename or delete.", "", modelData.count)
                                    .arg(modelData.name)
                        onClicked: root.categoryActivated(modelData.id)

                        TapHandler {
                            acceptedButtons: Qt.RightButton
                            onTapped: groupMenu.openFor(groupChip, groupChip.modelData)
                        }

                        DropArea {
                            id: groupDrop
                            anchors.fill: parent
                            // A card of this tab dropped on a group goes into it.
                            onEntered: (drag) => { drag.accepted = AssetGroups.draggedItem(root.tabId, drag).length > 0 }
                            onDropped: (drop) => {
                                const item = AssetGroups.draggedItem(root.tabId, drop)
                                if (item.length === 0)
                                    return
                                drop.accept(Qt.CopyAction)
                                EditorState.addToAssetGroup(root.tabId, groupChip.modelData.id, item)
                            }
                        }
                    }
                }

                IconButton {
                    visible: root.tabId.length > 0
                    anchors.verticalCenter: parent.verticalCenter
                    glyph: Theme.icons.plus
                    variant: "ghost"
                    buttonSize: Theme.controlHeightSm
                    iconSize: 14
                    tooltip: qsTr("New group…")
                    onClicked: AssetGroups.newGroup(root, root.tabId, "", function(id) {
                        root.categoryActivated(id)
                    })
                }

                ThemedChip {
                    id: mineButton
                    visible: root.showMine
                    anchors.verticalCenter: parent.verticalCenter
                    text: root.mineLabel
                    variant: "outline"
                    selected: root.activeCategory === root.mineId
                    onClicked: root.categoryActivated(root.mineId)
                }

                Rectangle {
                    id: divider
                    visible: root.categories.length > 0
                    anchors.verticalCenter: parent.verticalCenter
                    width: Theme.c.assetCategoryChips.dividerWidth
                    height: Theme.controlHeightSm - 6
                    color: Theme.c.assetCategoryChips.divider
                }

                Repeater {
                    model: root.categories
                    delegate: ThemedChip {
                        required property var modelData
                        required property int index
                        anchors.verticalCenter: parent.verticalCenter
                        text: modelData.label
                        variant: "secondary"
                        accentColor: Theme.categoryColor(index)
                        selected: root.activeCategory === modelData.id
                        onClicked: root.categoryActivated(modelData.id)
                    }
                }
            }
        }
    }

    ThemedContextMenu {
        id: groupMenu

        property var group: null
        property Item chip: null

        function openFor(chip, group) {
            groupMenu.chip = chip
            groupMenu.group = group
            groupMenu.popup()
        }

        ThemedMenuItem {
            text: qsTr("Rename group…")
            icon.name: Theme.icons.pencil
            onTriggered: {
                const group = groupMenu.group
                if (!group)
                    return
                AssetGroups.askName(root, qsTr("Rename group"), group.name, function(name) {
                    EditorState.renameAssetGroup(root.tabId, group.id, name)
                })
            }
        }
        ThemedMenuItem {
            text: qsTr("Delete group…")
            icon.name: Theme.icons.trash
            onTriggered: deleteDialog.openFor(groupMenu.group)
        }
    }

    // Deleting a group never deletes anything in it.
    ThemedDialog {
        id: deleteDialog

        property var group: null

        title: qsTr("Delete group")
        acceptText: qsTr("Delete group")
        acceptVariant: "destructive"
        preferredWidth: Theme.dialogWidthSm
        acceptOnReturn: false

        function openFor(group) {
            if (!group)
                return
            deleteDialog.group = group
            open()
        }

        onAccepted: {
            if (deleteDialog.group)
                EditorState.removeAssetGroup(root.tabId, deleteDialog.group.id)
        }

        contentItem: Text {
            width: parent ? parent.width : 320
            wrapMode: Text.WordWrap
            text: qsTr("Delete the group “%1”? Only the group goes; everything in it stays where it is.")
                      .arg(deleteDialog.group ? deleteDialog.group.name : "")
            color: Theme.c.effectBrowser.foreground
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeSm
        }
    }
}
