import QtQuick
import Drift

// Original visual-only Dafin skin. Trusted QML; no commands, I/O, or input handlers.
Rectangle {
    id: root
    property var control
    readonly property bool activeState: !!(control && (control.activeFocus === true || control.visualFocus === true || control.down === true))
    readonly property bool pressedState: !!(control && control.down === true)
    radius: 0
    color: "#B2B4B0"
    border.width: root.activeState ? 2 : 1
    border.color: root.activeState ? "#303638" : "#323638"
    Rectangle { x: 2; y: 2; width: Math.max(0,parent.width-4); height: 2; color: "#9A666B6B" }
}
