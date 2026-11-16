import QtQuick
import Drift

// Dafin original passive visual skin: no script execution or input handlers.
Rectangle {
    property var control
    radius: 3
    color: control && control.checked ? "#d69d56" : "#513826"
    border.width: 1; border.color: "#966d42"
    Text {
        anchors.centerIn: parent
        text: "✓"; visible: !!(control && control.checked)
        color: "#27180c"
        font.pixelSize: 14; font.bold: true
    }
}
