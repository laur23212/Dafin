import QtQuick
import Drift

// Original visual-only Dafin skin. Trusted QML; no commands, I/O, or input handlers.
Rectangle {
    id: root
    property var control
    readonly property bool activeState: !!(control && (control.activeFocus === true || control.visualFocus === true || control.down === true))
    readonly property bool pressedState: !!(control && control.down === true)
    radius: 16
    color: "#FFDC9D"
    border.width: root.activeState ? 2 : 1
    border.color: root.activeState ? "#B62966" : "#A34F6D"
    Rectangle { x: 4; y: Math.max(0,parent.height-4); width: Math.max(0,parent.width-8); height: 2; radius: 1; color: "#AEB62966" }
}
