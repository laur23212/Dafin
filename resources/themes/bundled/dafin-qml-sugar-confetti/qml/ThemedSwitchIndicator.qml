import QtQuick
import Drift

// Original visual-only Dafin skin. Trusted QML; no commands, I/O, or input handlers.
Rectangle {
    id: root
    property var control
    readonly property bool activeState: !!(control && control.checked === true)
    readonly property bool focusedState: !!(control && control.visualFocus === true)
    radius: 12
    color: root.activeState ? "#A83E94" : "#FDE1ED"
    border.width: root.focusedState ? 2 : 1
    border.color: root.focusedState ? "#B552A6" : "#D5A8D7"
    Rectangle {
        id: knob
        width: Math.max(4,parent.height-5)
        height: Math.max(4,parent.height-5)
        radius: width / 2
        x: root.activeState ? parent.width - width - 2 : 2
        y: (parent.height - height)/2
        color: "#FFF9FC"
        border.width: 1
        border.color: "#D5A8D7"
        Behavior on x { SpringAnimation { spring: 2.8; damping: 0.38; epsilon: 0.12 } }
    }
}
