import QtQuick
import Drift

// Original visual-only Dafin skin. Trusted QML; no commands, I/O, or input handlers.
Rectangle {
    id: root
    property var control
    readonly property bool activeState: !!(control && control.checked === true)
    readonly property bool focusedState: !!(control && control.visualFocus === true)
    radius: 3
    color: root.activeState ? "#F3B84D" : "#20272D"
    border.width: root.focusedState ? 2 : 1
    border.color: root.focusedState ? "#F3B84D" : "#79848A"
    Rectangle {
        id: knob
        width: Math.max(4,parent.height-5)
        height: Math.max(4,parent.height-5)
        radius: width / 2
        x: root.activeState ? parent.width - width - 2 : 2
        y: (parent.height - height)/2
        color: "#2B3339"
        border.width: 2
        border.color: "#79848A"
        Rectangle { anchors.centerIn: parent; height: 2; width: Math.max(2,parent.width-7); color: "#E6BB78" }
        Behavior on x { NumberAnimation { duration: 140 } }
    }
}
