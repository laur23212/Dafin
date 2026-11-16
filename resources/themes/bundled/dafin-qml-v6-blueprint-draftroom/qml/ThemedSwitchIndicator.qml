import QtQuick
import Drift

// Original visual-only Dafin skin. Trusted QML; no commands, I/O, or input handlers.
Rectangle {
    id: root
    property var control
    readonly property bool activeState: !!(control && control.checked === true)
    readonly property bool focusedState: !!(control && control.visualFocus === true)
    radius: 12
    color: root.activeState ? "#C0EEFF" : "#082B4D"
    border.width: root.focusedState ? 2 : 1
    border.color: root.focusedState ? "#B5EEFF" : "#8BC6E8"
    Rectangle {
        id: knob
        width: Math.max(4,parent.height-5)
        height: Math.max(4,parent.height-5)
        radius: width / 2
        x: root.activeState ? parent.width - width - 2 : 2
        y: (parent.height - height)/2
        color: "#103D69"
        border.width: 1
        border.color: "#8BC6E8"
        Behavior on x { NumberAnimation { duration: 140 } }
    }
}
