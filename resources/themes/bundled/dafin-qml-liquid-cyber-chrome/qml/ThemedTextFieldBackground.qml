import QtQuick
import Drift

// Original visual-only Dafin skin. Trusted QML; no commands, I/O, or input handlers.
Rectangle {
    id: root
    property var control
    readonly property bool activeState: !!(control && (control.activeFocus === true || control.visualFocus === true || control.down === true))
    readonly property bool pressedState: !!(control && control.down === true)
    radius: 18
    color: "#F2F6FB"
    border.width: root.activeState ? 2 : 1
    border.color: root.activeState ? "#B041C9" : "#8395B6"
    Rectangle { x: 3; y: 2; width: Math.max(0,parent.width-6); height: 4; radius: 2; color: "#66FFFFFF" }
}
