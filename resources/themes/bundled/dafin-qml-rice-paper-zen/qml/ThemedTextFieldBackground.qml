import QtQuick
import Drift

// Original visual-only Dafin skin. Trusted QML; no commands, I/O, or input handlers.
Rectangle {
    id: root
    property var control
    readonly property bool activeState: !!(control && (control.activeFocus === true || control.visualFocus === true || control.down === true))
    readonly property bool pressedState: !!(control && control.down === true)
    radius: 5
    color: "#FFFDF6"
    border.width: root.activeState ? 2 : 1
    border.color: root.activeState ? "#536E57" : "#A8AC9C"
    Rectangle { x: 7; y: Math.max(0,parent.height-3); width: Math.max(0,parent.width-14); height: 1; color: "#9C8D927C" }
}
