import QtQuick
import Drift

// Dafin original passive visual skin: no script execution or input handlers.
Rectangle {
    property var control
    radius: 10
    color: "#d17aa8"
    border.color: "#e59bbf"
    border.width: 1
    Rectangle {
        height: Math.max(0,parent.height - 2)
        width: Math.max(0,(parent.width-2) * (control ? control.visualPosition : 0))
        x: 1; y: 1
        radius: 10
        color: "#c53084"
    }
}
