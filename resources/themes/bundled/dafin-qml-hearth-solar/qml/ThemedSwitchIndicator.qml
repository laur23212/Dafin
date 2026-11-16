import QtQuick
import Drift

// Original visual-only Dafin skin. Trusted QML; no commands, I/O, or input handlers.
Rectangle {
    id: root
    property var control
    readonly property bool activeState: !!(control && control.checked === true)
    readonly property bool focusedState: !!(control && control.visualFocus === true)
    radius: 12
    color: root.activeState ? "#8E4A36" : "#F7EFE0"
    border.width: root.focusedState ? 2 : 1
    border.color: root.focusedState ? "#B35E43" : "#CDB89A"
    Rectangle {
        id: knob
        width: Math.max(4,parent.height-5)
        height: Math.max(4,parent.height-5)
        radius: width / 2
        x: root.activeState ? parent.width - width - 2 : 2
        y: (parent.height - height)/2
        color: "#FFF9EC"
        border.width: 1
        border.color: "#CDB89A"
        Behavior on x { NumberAnimation { duration: 140 } }
    }
}
