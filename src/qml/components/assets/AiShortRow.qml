import QtQuick
import QtQuick.Controls.Basic
import Drift

// Dafin: one proposed highlight in AI › Smart short clips: tick box, score, length, where it starts
// and its first words. A click plays it on the timeline (again stops it); the score's tooltip says
// what made it.
Rectangle {
    id: root

    required property int index
    required property var modelData

    readonly property bool playing: EditorState.ai.shorts.previewing === root.index

    height: Math.max(Theme.controlHeight, body.implicitHeight + Theme.spacingSm * 2)
    radius: Theme.c.scenesShotRow.radius
    color: hover.hovered ? Theme.c.scenesShotRow.backgroundHover : "transparent"

    Accessible.role: Accessible.ListItem
    Accessible.name: qsTr("Highlight %1, score %2, %3: %4").arg(root.index + 1).arg(root.modelData.score)
                     .arg(root.modelData.lengthLabel).arg(root.modelData.firstWords)

    HoverHandler { id: hover }

    // The highlight playing now.
    Rectangle {
        anchors.fill: parent
        radius: parent.radius
        color: Theme.c.subtitlesTab.accent
        opacity: root.playing ? 0.14 : 0
    }

    ThemedCheckBox {
        id: check
        anchors.left: parent.left
        anchors.leftMargin: Theme.spacingXs
        anchors.verticalCenter: parent.verticalCenter
        checked: root.modelData.checked
        enabled: !EditorState.ai.shorts.busy
        tooltip: qsTr("Make this highlight a short")
        onToggled: EditorState.ai.shorts.setChecked(root.index, checked)
    }

    Rectangle {
        id: scoreBadge
        anchors.left: check.right
        anchors.leftMargin: Theme.spacingXs
        anchors.verticalCenter: parent.verticalCenter
        width: 30
        height: 20
        radius: 4
        color: "transparent"
        border.width: 1
        border.color: Theme.c.subtitlesTab.accent

        Text {
            anchors.centerIn: parent
            text: root.modelData.score
            color: Theme.c.subtitlesTab.accent
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeXs
            font.weight: Font.DemiBold
        }

        HoverHandler { id: scoreHover }
        ThemedToolTip {
            visible: scoreHover.hovered && root.modelData.why.length > 0
            text: qsTr("Score %1 of 100: %2").arg(root.modelData.score).arg(root.modelData.why)
        }
    }

    Column {
        id: body
        anchors.left: scoreBadge.right
        anchors.leftMargin: Theme.spacingSm
        anchors.right: playButton.left
        anchors.rightMargin: Theme.spacingXs
        anchors.verticalCenter: parent.verticalCenter
        spacing: 2

        Text {
            width: body.width
            elide: Text.ElideRight
            text: qsTr("%1 · from %2").arg(root.modelData.lengthLabel).arg(root.modelData.startLabel)
            color: Theme.c.subtitlesTab.foregroundMuted
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeXs
        }
        Text {
            width: body.width
            elide: Text.ElideRight
            maximumLineCount: 2
            wrapMode: Text.WordWrap
            text: root.modelData.firstWords
            color: Theme.c.subtitlesTab.foreground
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeXs
        }
    }

    // The words play it too; the tick box and the play button keep their own clicks.
    MouseArea {
        anchors.fill: body
        cursorShape: Qt.PointingHandCursor
        onClicked: EditorState.ai.shorts.preview(root.index)
    }

    IconButton {
        id: playButton
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        glyph: root.playing ? Theme.icons.square : Theme.icons.play
        tooltip: root.playing ? qsTr("Stop") : qsTr("Play this highlight on the timeline")
        Accessible.name: tooltip
        onClicked: EditorState.ai.shorts.preview(root.index)
    }
}
