import QtQuick
import Drift

// Original visual-only Dafin skin. Trusted QML; no commands, I/O, or input handlers.
Rectangle {
    id: root
    property var control
    readonly property bool activeState: !!(control && (control.pressed === true || control.hovered === true || control.visualFocus === true))
    readonly property bool pressedState: !!(control && control.pressed === true)
    radius: 6
    color: root.pressedState ? "#255E7E" : "#E6EBF1"
    border.width: 2
    border.color: root.activeState ? "#386B8B" : "#BAC7D4"
    Rectangle { width: 2; height: Math.max(2,parent.height*.32); radius: 1; anchors.centerIn: parent; color: "#263445" }
}
