import QtQuick
import Drift

// Dafin original passive visual skin: no script execution or input handlers.
Rectangle {
    property var control
    radius: 12
    color: control && control.checked ? "#d84e9e" : "#d17aa8"
    border.width: 1
    border.color: "#e59bbf"
    Rectangle {
        width: Math.max(4,parent.height-5); height: Math.max(4,parent.height-5)
        x: control && control.checked ? parent.width-width-2 : 2
        y: (parent.height-height)/2
        radius: 8
        color: "#ffffff"
        border.width: 1; border.color: "#e59bbf"
        Behavior on x { NumberAnimation { duration: 130 } }
    }
}
