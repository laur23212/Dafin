import QtQuick
import Drift

// Original visual-only Dafin skin. Trusted QML; no commands, I/O, or input handlers.
Rectangle {
    id: root
    property var control
    readonly property bool activeState: !!(control && (control.activeFocus === true || control.visualFocus === true || control.down === true))
    readonly property bool pressedState: !!(control && control.down === true)
    radius: 3
    color: "#FFFCF3"
    border.width: root.activeState ? 2 : 1
    border.color: root.activeState ? "#DA365E" : "#32516A"
    Rectangle { x: Math.max(0,parent.width-16); y: 3; width: 10; height: 3; color: "#DA365E" }
}
