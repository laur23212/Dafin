import QtQuick
import Drift

// Dafin original passive visual skin: no script execution or input handlers.
Rectangle {
    property var control
    radius: 3
    color: "#0d0907"
    border.color: "#966d42"
    border.width: 1
    Rectangle {
        height: Math.max(0,parent.height - 2)
        width: Math.max(0,(parent.width-2) * (control ? control.visualPosition : 0))
        x: 1; y: 1
        radius: 3
        color: "#d1a05b"
    }
}
