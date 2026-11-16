import QtQuick
import Drift

// Dafin original passive visual skin: no script execution or input handlers.
Rectangle {
    property var control
    radius: 10
    color: control && control.checked ? "#00a5bf" : "#469fc0"
    border.width: 1
    border.color: "#51b6d0"
    Rectangle {
        width: Math.max(4,parent.height-5); height: Math.max(4,parent.height-5)
        x: control && control.checked ? parent.width-width-2 : 2
        y: (parent.height-height)/2
        radius: 8
        color: "#ffffff"
        border.width: 1; border.color: "#51b6d0"
        Behavior on x { NumberAnimation { duration: 130 } }
    }
}
