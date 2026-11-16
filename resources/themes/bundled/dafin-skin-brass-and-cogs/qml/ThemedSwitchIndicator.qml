import QtQuick
import Drift

// Dafin original passive visual skin: no script execution or input handlers.
Rectangle {
    property var control
    radius: 4
    color: control && control.checked ? "#d69d56" : "#0d0907"
    border.width: 1
    border.color: "#966d42"
    Rectangle {
        width: Math.max(4,parent.height-5); height: Math.max(4,parent.height-5)
        x: control && control.checked ? parent.width-width-2 : 2
        y: (parent.height-height)/2
        radius: 8
        color: "#eed4a7"
        border.width: 1; border.color: "#966d42"
        Behavior on x { NumberAnimation { duration: 130 } }
    }
}
