import QtQuick
import Drift

// Original visual-only Dafin skin. Trusted QML; no commands, I/O, or input handlers.
Rectangle {
    id: root
    property var control
    color: "#EFC185"
    radius: 16
    border.width: 1; border.color: "#A34F6D"
    Rectangle {
        x: 1; y: 1
        height: Math.max(0,parent.height-2)
        width: Math.max(0,(parent.width-2)*Math.max(0,Math.min(1,control && control.value !== undefined ? control.value : 0)))
        radius: root.radius
        color: "#A52865"
    }
}
