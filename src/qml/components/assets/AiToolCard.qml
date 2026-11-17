import QtQuick
import Drift

// Dafin: one tool in the AI category. A tool that is not built yet stays visible but greyed out,
// with a "Coming soon" badge, and does not open. In low-spec mode a built tool is greyed out the
// same way, with an "Off" badge and a tooltip saying why.
Rectangle {
    id: root

    required property var tool
    // Low-spec (compatibility) mode: local AI is off on this computer.
    property bool lowSpecOff: false
    readonly property bool available: tool.available === true && !lowSpecOff

    signal openRequested()

    implicitHeight: content.implicitHeight + Theme.spacingLg * 2
    radius: Theme.c.driftAssetCard.radius
    color: Theme.c.driftAssetCard.background
    border.width: Theme.c.scenesShotRow.borderWidth
    border.color: root.available && (hover.hovered || root.activeFocus) ? Theme.c.scenesShotRow.borderHover
                                                                         : "transparent"
    opacity: root.available ? 1.0 : 0.45
    activeFocusOnTab: root.available

    Accessible.role: Accessible.Button
    Accessible.name: tool.title
    Accessible.description: root.available ? tool.description
                                           : (root.lowSpecOff ? root.lowSpecTip : qsTr("Coming soon"))

    readonly property string lowSpecTip: qsTr("Off in compatibility mode: this computer is below Dafin's recommended specs. Settings › Performance › Low-spec mode turns it back on.")

    Keys.onReturnPressed: if (root.available) root.openRequested()
    Keys.onEnterPressed: if (root.available) root.openRequested()
    Keys.onSpacePressed: if (root.available) root.openRequested()

    // A lighter wash on hover, the way the other categories light a card.
    Rectangle {
        anchors.fill: parent
        radius: parent.radius
        color: Theme.c.scenesShotRow.backgroundHover
        visible: root.available && hover.hovered
        opacity: 0.6
    }

    Column {
        id: content
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: Theme.spacingLg
        spacing: Theme.spacingSm

        Item {
            width: parent.width
            height: iconTile.height

            Rectangle {
                id: iconTile
                width: 36
                height: 36
                radius: Theme.c.driftAssetCard.radius
                color: Theme.c.scenesShotRow.meterBackground

                IconGlyph {
                    anchors.centerIn: parent
                    glyph: root.tool.icon
                    iconSize: 20
                    iconColor: root.available ? Theme.c.driftAssetCard.accent : Theme.c.driftAssetCard.icon
                }
            }

            Rectangle {
                visible: !root.available
                anchors.right: parent.right
                anchors.top: parent.top
                width: soonLabel.implicitWidth + Theme.spacingMd * 2
                height: soonLabel.implicitHeight + Theme.spacingXs * 2
                radius: Theme.c.driftAssetCard.radiusPill
                color: Theme.c.scenesShotRow.meterBackground

                Text {
                    id: soonLabel
                    anchors.centerIn: parent
                    text: root.lowSpecOff ? qsTr("Off") : qsTr("Coming soon")
                    color: Theme.c.driftAssetCard.foregroundMuted
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeXs
                }
            }
        }

        Text {
            width: parent.width
            text: root.tool.title
            color: Theme.c.driftAssetCard.foreground
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeSm
            font.weight: Font.Medium
            elide: Text.ElideRight
        }

        // Room for three lines whatever the text, so every card in the grid is the same height.
        Item {
            width: parent.width
            height: Math.ceil(descMetrics.lineSpacing * 3)

            Text {
                width: parent.width
                text: root.tool.description
                color: Theme.c.driftAssetCard.foregroundMuted
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeXs
                wrapMode: Text.WordWrap
                maximumLineCount: 3
                elide: Text.ElideRight
            }
        }
    }

    FontMetrics {
        id: descMetrics
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSizeXs
    }

    ThemedToolTip {
        text: root.lowSpecTip
        visible: root.lowSpecOff && hover.hovered
    }

    HoverHandler {
        id: hover
        cursorShape: root.available ? Qt.PointingHandCursor : Qt.ArrowCursor
    }

    TapHandler {
        enabled: root.available
        onTapped: {
            root.forceActiveFocus()
            root.openRequested()
        }
    }
}
