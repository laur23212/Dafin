import QtQuick
import Drift

// Dafin original passive visual skin: no script execution or input handlers.
Rectangle {
    property var control
    radius: 4
    color: control && control.checked ? "#00a5bf" : "#b8e5ed"
    border.width: 1; border.color: "#51b6d0"
    Text {
        anchors.centerIn: parent
        text: "✓"; visible: !!(control && control.checked)
        color: "#ffffff"
        font.pixelSize: 14; font.bold: true
    }
}
