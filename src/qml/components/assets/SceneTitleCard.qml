import QtQuick
import QtQuick.Controls.Basic
import Drift
// Dafin: one Scene Title card (Titles › Scenes). The picture is the native scene renderer's own
// frame (image://scenetitle); on hover it plays the scene from its first entrance to its exit.
Column {
    id: root
    required property var modelData
    required property real cardSize
    signal added()
    signal deleteRequested(var scene)
    width: cardSize
    spacing: Theme.spacingSm

    property real playT: -1
    readonly property real duration: Number(root.modelData.duration || 6)

    opacity: sceneDrag.active ? Theme.c.textStylePackCard.dragOpacity : 1
    scale: sceneDrag.active ? 1.04 : sceneDrag.pressed ? 0.97 : (sceneDrag.hovered ? 1.02 : 1.0)
    Behavior on scale {
        NumberAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
    }

    Rectangle {
        width: parent.width
        height: Math.round(width * 0.5625)
        radius: Theme.radiusSm
        color: "#090c11"
        clip: true

        Image {
            anchors.fill: parent
            asynchronous: true
            cache: true
            fillMode: Image.PreserveAspectFit
            sourceSize: Qt.size(384, 216)
            // 0.2 s steps keep the hover preview to 30 renders, each cached once drawn.
            source: "image://scenetitle/" + root.modelData.id
                    + (root.playT >= 0 ? "?t=" + root.playT.toFixed(1) : "")
        }

        Timer {
            interval: 200
            repeat: true
            running: sceneDrag.hovered && !sceneDrag.active
            onRunningChanged: root.playT = running ? 0 : -1
            onTriggered: root.playT = root.playT + 0.2 >= root.duration ? 0 : root.playT + 0.2
        }

        AssetDragSource {
            id: sceneDrag
            hasContextMenu: true
            onContextMenuRequested: cardMenu.popup()
            anchors.fill: parent
            kind: "sceneTitle"
            payload: root.modelData.id
            label: root.modelData.label
            glyph: Theme.icons.type
            onTapped: {
                EditorState.addSceneTitleClip(root.modelData.id, -1, -1)
                root.added()
            }
        }

        ThemedToolTip {
            text: qsTr("%1 — click to add at the playhead, or drag it onto the timeline. It lands as one clip.").arg(root.modelData.label)
            visible: sceneDrag.hovered && !sceneDrag.active
        }

        AssetQuickAddButton {
            z: 5
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.margins: 4
            revealed: sceneDrag.hovered
            tooltip: qsTr("Add at playhead")
            onClicked: sceneDrag.tapped()
        }

        ThemedContextMenu {
            id: cardMenu
            ThemedMenuItem {
                text: qsTr("Add at playhead")
                icon.name: Theme.icons.plus
                onTriggered: sceneDrag.tapped()
            }
            ThemedMenuItem {
                visible: root.modelData.origin === "installed"
                height: visible ? implicitHeight : 0
                text: qsTr("Remove")
                icon.name: Theme.icons.trash
                onTriggered: root.deleteRequested(root.modelData)
            }
        }
    }

    Text {
        width: parent.width
        text: root.modelData.label
        elide: Text.ElideRight
        horizontalAlignment: Text.AlignHCenter
        color: sceneDrag.hovered ? Theme.c.textStylePackCard.foregroundHover : Theme.c.textStylePackCard.foregroundMuted
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSizeXs
    }
}
