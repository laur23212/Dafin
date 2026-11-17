import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Window
import Drift
// One built-in text style pack card.
Column {
    id: root
    required property var modelData
    required property real cardSize
    signal added()
    width: cardSize
    spacing: Theme.spacingSm

    opacity: packDrag.active ? Theme.c.textStylePackCard.dragOpacity : 1
    scale: packDrag.active ? 1.04 : packDrag.pressed ? 0.97 : (packDrag.hovered ? 1.02 : 1.0)
    Behavior on scale {
        NumberAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
    }

    TextStylePackThumb {
        width: parent.width
        height: Math.round(width * 0.55)
        presetId: root.modelData.id
        hovered: packDrag.hovered

        AssetDragSource {
            id: packDrag
            hasContextMenu: true
            onContextMenuRequested: cardMenu.popup()
            anchors.fill: parent
            kind: "textStyle"
            payload: root.modelData.id
            groupTab: "titles"
            groupItem: root.modelData.id
            label: root.modelData.label
            glyph: Theme.icons.type
            onTapped: {
                EditorState.addTextClip("", -1, root.modelData.id)
                added()
            }
        }

        ThemedToolTip {
            text: qsTr("%1 — click to add at the playhead, or drag it onto the timeline or the preview. It lands with its own words: click it on the preview to change them.").arg(root.modelData.label)
            visible: packDrag.hovered && !packDrag.active
        }

        AssetQuickAddButton {
            z: 5
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.margins: 4
            revealed: packDrag.hovered
            tooltip: qsTr("Add at playhead")
            onClicked: packDrag.tapped()
        }

        ThemedContextMenu {
            id: cardMenu

            ThemedMenuItem {
                text: qsTr("Add at playhead")
                icon.name: Theme.icons.plus
                onTriggered: packDrag.tapped()
            }
            ThemedMenuSeparator { }
            ThemedMenuItem {
                text: AssetGroups.tick >= 0 && EditorState.isAssetFavorite("titles", root.modelData.id)
                      ? qsTr("Remove from favorites") : qsTr("Add to favorites")
                icon.name: Theme.icons.star
                onTriggered: EditorState.toggleAssetFavorite("titles", root.modelData.id)
            }
            AssetGroupSubmenu {
                tabId: "titles"
                itemId: root.modelData.id
                anchorItem: root
            }
            AssetGroupRemoveItem {
                tabId: "titles"
                itemId: root.modelData.id
            }
        }

        // Dafin: titles can be favorites too.
        AssetFavoriteButton {
            anchors.left: parent.left
            anchors.top: parent.top
            anchors.margins: 3
            tabId: "titles"
            itemId: root.modelData.id
        }
    }

    Text {
        width: parent.width
        text: root.modelData.label
        elide: Text.ElideRight
        horizontalAlignment: Text.AlignHCenter
        color: packDrag.hovered ? Theme.c.textStylePackCard.foregroundHover : Theme.c.textStylePackCard.foregroundMuted
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSizeXs

        Behavior on color {
            ColorAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
        }
    }
}
