import QtQuick
import Drift

// Original visual-only Dafin skin. Trusted QML; no commands, I/O, or input handlers.
Rectangle {
    id: root
    property var control
    readonly property bool activeState: !!(control && (control.activeFocus === true || control.visualFocus === true || control.down === true))
    readonly property bool pressedState: !!(control && control.down === true)
    radius: 0
    color: "#103D69"
    border.width: root.activeState ? 2 : 1
    border.color: root.activeState ? "#B5EEFF" : "#8BC6E8"
    Rectangle { x: 3; y: 3; width: 7; height: 1; color: "#C2F3FF" }
    Rectangle { x: 3; y: 3; width: 1; height: 7; color: "#C2F3FF" }
}
