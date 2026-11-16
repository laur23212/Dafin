import QtQuick
import Drift

// Original visual-only Dafin skin. Trusted QML; no commands, I/O, or input handlers.
Rectangle {
    id: root
    property var control
    readonly property bool activeState: !!(control && control.checked === true)
    readonly property bool focusedState: !!(control && control.visualFocus === true)
    radius: 3
    color: root.activeState ? "#FFE900" : "#000000"
    border.width: root.focusedState ? 4 : 1
    border.color: root.focusedState ? "#FFE900" : "#FFFFFF"
    Rectangle {
        id: knob
        width: Math.max(4,parent.height-5)
        height: Math.max(4,parent.height-5)
        radius: width / 2
        x: root.activeState ? parent.width - width - 2 : 2
        y: (parent.height - height)/2
        color: "#0C0C0C"
        border.width: 1
        border.color: "#FFFFFF"
    }
}
