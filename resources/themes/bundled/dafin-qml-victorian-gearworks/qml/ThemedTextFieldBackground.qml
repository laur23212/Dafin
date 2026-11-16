import QtQuick
import Drift

// Original visual-only Dafin skin. Trusted QML; no commands, I/O, or input handlers.
Rectangle {
    id: root
    property var control
    readonly property bool activeState: !!(control && (control.activeFocus === true || control.visualFocus === true || control.down === true))
    readonly property bool pressedState: !!(control && control.down === true)
    radius: 3
    color: "#2B241C"
    border.width: root.activeState ? 2 : 1
    border.color: root.activeState ? "#D8B56C" : "#A78350"
    Rectangle { x: 3; y: 3; width: Math.max(0,parent.width-6); height: 2; color: "#8A4C3023" }
}
