import QtQuick
import Drift

// Original visual-only Dafin skin. Trusted QML; no commands, I/O, or input handlers.
Rectangle {
    id: root
    property var control
    readonly property bool activeState: !!(control && (control.pressed === true || control.hovered === true || control.visualFocus === true))
    readonly property bool pressedState: !!(control && control.pressed === true)
    radius: 21
    color: root.pressedState ? "#774A9B" : "#E9CFF4"
    border.width: 2
    border.color: root.activeState ? "#9B5CBF" : "#CBA9DF"
    Rectangle { width: 2; height: Math.max(2,parent.height*.32); radius: 1; anchors.centerIn: parent; color: "#4A355F" }
}
