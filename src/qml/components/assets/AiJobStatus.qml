import QtQuick
import Drift

// Dafin: an AI tool's run in progress: what it is doing, how far it got, and Cancel.
Column {
    id: root

    property bool running: false
    property real progress: 0
    property string status: ""
    property string cancelTooltip: qsTr("Stop")
    signal cancelRequested()

    width: parent ? parent.width : implicitWidth
    spacing: Theme.spacingSm
    visible: running

    Text {
        width: root.width
        elide: Text.ElideRight
        text: root.status.length > 0 ? root.status : qsTr("Working… %1%").arg(Math.round(root.progress * 100))
        color: Theme.c.subtitlesTab.statusForeground
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSizeXs
    }

    ThemedProgressBar {
        width: root.width
        value: root.progress
    }

    ThemedButton {
        text: qsTr("Cancel")
        variant: "destructive"
        glyph: Theme.icons.x
        tooltip: root.cancelTooltip
        onClicked: root.cancelRequested()
    }
}
