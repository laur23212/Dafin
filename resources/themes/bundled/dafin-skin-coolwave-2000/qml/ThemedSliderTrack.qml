import QtQuick
import Drift

// Dafin original passive visual skin: no script execution or input handlers.
Rectangle {
    property var control
    radius: 0
    color: "#6e7581"
    border.color: "#536270"
    border.width: 1
    Rectangle {
        height: Math.max(0,parent.height - 2)
        width: Math.max(0,(parent.width-2) * (control ? control.visualPosition : 0))
        x: 1; y: 1
        radius: 0
        color: "#147b92"
    }
}
