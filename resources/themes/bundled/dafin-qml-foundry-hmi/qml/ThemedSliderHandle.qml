import QtQuick
import Drift

// Original visual-only Dafin skin. Trusted QML; no commands, I/O, or input handlers.
Rectangle {
    id: root
    property var control
    readonly property bool activeState: !!(control && (control.pressed === true || control.hovered === true || control.visualFocus === true))
    readonly property bool pressedState: !!(control && control.pressed === true)
    radius: width / 2
    color: root.pressedState ? "#F3B84D" : "#454D53"
    border.width: 2
    border.color: root.activeState ? "#F3B84D" : "#79848A"
    Rectangle { width: 3; height: Math.max(2,parent.height*.32); radius: 1; anchors.centerIn: parent; color: "#F4F3EC" }
    Rectangle { anchors.fill: parent; anchors.margins: 3; radius: width/2; color: "transparent"; border.width: 1; border.color: "#B1B7B1" }
}
