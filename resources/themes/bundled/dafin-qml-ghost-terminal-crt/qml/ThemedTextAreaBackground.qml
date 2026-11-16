import QtQuick
import Drift

// Original visual-only Dafin skin. Trusted QML; no commands, I/O, or input handlers.
Rectangle {
    id: root
    property var control
    readonly property bool activeState: !!(control && (control.activeFocus === true || control.visualFocus === true || control.down === true))
    readonly property bool pressedState: !!(control && control.down === true)
    radius: 0
    color: "#07170C"
    border.width: root.activeState ? 2 : 1
    border.color: root.activeState ? "#70FF9B" : "#428A61"
    Rectangle { x: 4; y: Math.max(0,parent.height-3); width: Math.max(0,parent.width-8); height: 1; color: "#8870FF9B" }
}
