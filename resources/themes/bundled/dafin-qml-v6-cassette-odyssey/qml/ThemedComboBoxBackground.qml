import QtQuick
import Drift

// Original visual-only Dafin skin. Trusted QML; no commands, I/O, or input handlers.
Rectangle {
    id: root
    property var control
    readonly property bool activeState: !!(control && (control.activeFocus === true || control.visualFocus === true || control.down === true))
    readonly property bool pressedState: !!(control && control.down === true)
    radius: 2
    color: "#F0E7D7"
    border.width: root.activeState ? 2 : 1
    border.color: root.activeState ? "#A63D23" : "#887967"
    Rectangle { x: 2; y: 2; width: Math.max(0,parent.width-4); height: 2; color: "#99887662" }
}
