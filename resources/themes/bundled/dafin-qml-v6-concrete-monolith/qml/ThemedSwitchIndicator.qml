import QtQuick
import Drift

// Original visual-only Dafin skin. Trusted QML; no commands, I/O, or input handlers.
Rectangle {
    id: root
    property var control
    readonly property bool activeState: !!(control && control.checked === true)
    readonly property bool focusedState: !!(control && control.visualFocus === true)
    radius: 0
    color: root.activeState ? "#22292B" : "#949996"
    border.width: root.focusedState ? 2 : 1
    border.color: root.focusedState ? "#303638" : "#323638"
    Rectangle {
        id: knob
        width: Math.max(4,parent.height-5)
        height: Math.max(4,parent.height-5)
        radius: 0
        x: root.activeState ? parent.width - width - 2 : 2
        y: (parent.height - height)/2
        color: "#B2B4B0"
        border.width: 1
        border.color: "#323638"
        Behavior on x { NumberAnimation { duration: 140 } }
    }
}
