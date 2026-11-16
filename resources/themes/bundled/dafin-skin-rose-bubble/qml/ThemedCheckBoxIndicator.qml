import QtQuick
import Drift

// Dafin original passive visual skin: no script execution or input handlers.
Rectangle {
    property var control
    radius: 7
    color: control && control.checked ? "#d84e9e" : "#f9c8e3"
    border.width: 1; border.color: "#e59bbf"
    Text {
        anchors.centerIn: parent
        text: "✓"; visible: !!(control && control.checked)
        color: "#ffffff"
        font.pixelSize: 14; font.bold: true
    }
}
