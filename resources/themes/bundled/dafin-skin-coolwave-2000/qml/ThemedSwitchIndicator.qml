import QtQuick
import Drift

// Dafin original passive visual skin: no script execution or input handlers.
Rectangle {
    property var control
    radius: 1
    color: control && control.checked ? "#126f87" : "#6e7581"
    border.width: 2
    border.color: "#536270"
    Rectangle {
        width: Math.max(4,parent.height-5); height: Math.max(4,parent.height-5)
        x: control && control.checked ? parent.width-width-2 : 2
        y: (parent.height-height)/2
        radius: 0
        color: "#ffffff"
        border.width: 1; border.color: "#536270"
        Behavior on x { NumberAnimation { duration: 55 } }
    }
}
