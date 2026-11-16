import QtQuick
import Drift

// Original visual-only Dafin skin. Trusted QML; no commands, I/O, or input handlers.
Rectangle {
    id: root
    property var control
    readonly property bool activeState: !!(control && (control.activeFocus === true || control.visualFocus === true || control.down === true))
    readonly property bool pressedState: !!(control && control.down === true)
    radius: 2
    color: "#EAE2F2"
    border.width: root.activeState ? 2 : 1
    border.color: root.activeState ? "#7B4894" : "#8F72A8"
    Rectangle { x: 1; y: 1; height: 1; width: Math.max(0,parent.width-2); color: "#8F72A8" }
}
