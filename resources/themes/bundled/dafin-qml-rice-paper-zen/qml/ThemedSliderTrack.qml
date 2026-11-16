import QtQuick
import Drift

// Original visual-only Dafin skin. Trusted QML; no commands, I/O, or input handlers.
Rectangle {
    id: root
    property var control
    radius: 1
    color: "#F2EEE5"
    border.width: 1
    border.color: "#A8AC9C"
    Rectangle {
        x: 1; y: 1
        width: Math.max(0,(parent.width - 2) * (control ? Math.max(0,Math.min(1,control.visualPosition)) : 0))
        height: Math.max(0,parent.height - 2)
        color: "#536E57"
        radius: root.radius
    }
}
