import QtQuick
import Drift

// Original visual-only Dafin skin. Trusted QML; no commands, I/O, or input handlers.
Rectangle {
    id: root
    property var control
    color: "#ECF1F4"
    radius: 3
    border.width: 1; border.color: "#173149"
    Rectangle {
        x: 1; y: 1
        height: Math.max(0,parent.height-2)
        width: Math.max(0,(parent.width-2)*Math.max(0,Math.min(1,control && control.value !== undefined ? control.value : 0)))
        radius: root.radius
        color: "#D1253D"
    }
}
