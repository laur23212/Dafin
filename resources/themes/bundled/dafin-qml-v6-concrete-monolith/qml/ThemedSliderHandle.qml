import QtQuick
import Drift

// Original visual-only Dafin skin. Trusted QML; no commands, I/O, or input handlers.
Rectangle {
    id: root
    property var control
    readonly property bool activeState: !!(control && (control.pressed === true || control.hovered === true || control.visualFocus === true))
    readonly property bool pressedState: !!(control && control.pressed === true)
    radius: 0
    color: root.pressedState ? "#22292B" : "#A1A5A2"
    border.width: 2
    border.color: root.activeState ? "#303638" : "#323638"
    Rectangle { width: 2; height: Math.max(2,parent.height*.32); radius: 1; anchors.centerIn: parent; color: "#171C1D" }
}
