import QtQuick
import Drift

// Original visual-only Dafin skin. Trusted QML; no commands, I/O, or input handlers.
Rectangle {
    id: root
    property var control
    readonly property bool activeState: !!(control && (control.activeFocus === true || control.visualFocus === true || control.down === true))
    readonly property bool pressedState: !!(control && control.down === true)
    radius: 15
    color: "#E5EAF0"
    border.width: root.activeState ? 2 : 1
    border.color: root.activeState ? "#537F9C" : "#9DAEBE"
    Rectangle { x: 2; y: 2; width: Math.max(0,parent.width-4); height: 2; color: "#64839EAF" }
    Rectangle { x: 2; y: 4; width: 2; height: Math.max(0,parent.height-8); color: "#66839EAF" }
}
