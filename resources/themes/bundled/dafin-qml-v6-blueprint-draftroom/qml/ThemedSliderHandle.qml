import QtQuick
import Drift

// Original visual-only Dafin skin. Trusted QML; no commands, I/O, or input handlers.
Rectangle {
    id: root
    property var control
    readonly property bool activeState: !!(control && (control.pressed === true || control.hovered === true || control.visualFocus === true))
    readonly property bool pressedState: !!(control && control.pressed === true)
    radius: 0
    color: root.pressedState ? "#C0EEFF" : "#235D8D"
    border.width: 2
    border.color: root.activeState ? "#B5EEFF" : "#8BC6E8"
    Rectangle { width: 2; height: Math.max(2,parent.height*.32); radius: 1; anchors.centerIn: parent; color: "#F0F8FD" }
}
