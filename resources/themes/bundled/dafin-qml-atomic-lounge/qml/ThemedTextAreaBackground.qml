import QtQuick
import Drift

// Original visual-only Dafin skin. Trusted QML; no commands, I/O, or input handlers.
Rectangle {
    id: root
    property var control
    readonly property bool activeState: !!(control && (control.activeFocus === true || control.visualFocus === true || control.down === true))
    readonly property bool pressedState: !!(control && control.down === true)
    radius: 13
    color: "#FFF5E4"
    border.width: root.activeState ? 2 : 1
    border.color: root.activeState ? "#A94E2B" : "#AD8A68"
    Rectangle { x: 4; y: Math.max(0,parent.height-3); width: Math.max(0,parent.width-8); height: 1; color: "#A988775D" }
}
