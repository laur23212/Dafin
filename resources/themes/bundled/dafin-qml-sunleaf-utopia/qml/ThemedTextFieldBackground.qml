import QtQuick
import Drift

// Original visual-only Dafin skin. Trusted QML; no commands, I/O, or input handlers.
Rectangle {
    id: root
    property var control
    readonly property bool activeState: !!(control && (control.activeFocus === true || control.visualFocus === true || control.down === true))
    readonly property bool pressedState: !!(control && control.down === true)
    radius: 11
    color: "#F8FAEA"
    border.width: root.activeState ? 2 : 1
    border.color: root.activeState ? "#2E856A" : "#8EB7A2"
    Rectangle { x: 4; y: Math.max(0,parent.height-3); width: Math.max(0,parent.width-8); height: 1; color: "#992E856A" }
}
