import QtQuick
import Drift

// Dafin original passive visual skin: no script execution or input handlers.
Rectangle {
    property var control
    radius: 0
    color: control && control.checked ? "#126f87" : "#b6bac1"
    border.width: 1; border.color: "#536270"
    Text {
        anchors.centerIn: parent
        text: "✓"; visible: !!(control && control.checked)
        color: "#ffffff"
        font.pixelSize: 14; font.bold: true
    }
}
