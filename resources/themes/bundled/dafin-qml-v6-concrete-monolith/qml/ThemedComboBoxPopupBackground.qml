import QtQuick
import Drift

// Original visual-only Dafin skin. Trusted QML; no commands, I/O, or input handlers.
Rectangle {
    id: root
    property var control
    readonly property bool activeState: !!(control && (control.activeFocus === true || control.visualFocus === true || control.down === true))
    readonly property bool pressedState: !!(control && control.down === true)
    radius: 0
    color: "#B2B4B0"
    border.width: root.activeState ? 2 : 1
    border.color: root.activeState ? "#303638" : "#323638"
    Rectangle { x: 0; y: 0; width: parent.width; height: 4; color: "#63323739" }
    Rectangle { x: 5; y: 8; width: Math.min(40,parent.width/5); height: 1; color: "#696D6F6E" }
    Rectangle { x: Math.max(0,parent.width-20); y: Math.max(0,parent.height-14); width: 2; height: 2; color: "#74686A68" }
    Rectangle { x: Math.max(0,parent.width-30); y: Math.max(0,parent.height-22); width: 1; height: 1; color: "#70686A68" }
}
