import QtQuick
import Drift

// Original visual-only Dafin skin. Trusted QML; no commands, I/O, or input handlers.
Rectangle {
    id: root
    property var control
    readonly property bool activeState: !!(control && (control.activeFocus === true || control.visualFocus === true || control.down === true))
    readonly property bool pressedState: !!(control && control.down === true)
    radius: 12
    color: "#284B43"
    border.width: root.activeState ? 2 : 1
    border.color: root.activeState ? "#E7C57A" : "#B4A474"
    Rectangle { x: 7; y: Math.max(0,parent.height-4); width: Math.max(0,parent.width-14); height: 1; color: "#88E7C57A" }
}
