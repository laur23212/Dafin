import QtQuick
import Drift

// Original visual-only Dafin skin. Trusted QML; no commands, I/O, or input handlers.
Rectangle {
    id: root
    property var control
    readonly property bool activeState: !!(control && (control.activeFocus === true || control.visualFocus === true || control.down === true))
    readonly property bool pressedState: !!(control && control.down === true)
    radius: 15
    color: "#071A2B"
    border.width: root.activeState ? 2 : 1
    border.color: root.activeState ? "#68E5FA" : "#2B7088"
    Rectangle { x: 4; y: Math.max(0,parent.height-4); width: Math.max(0,parent.width-8); height: 1; color: "#8C68E5FA" }
}
