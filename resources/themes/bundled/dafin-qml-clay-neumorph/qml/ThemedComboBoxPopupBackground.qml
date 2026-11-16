import QtQuick
import Drift

// Original visual-only Dafin skin. Trusted QML; no commands, I/O, or input handlers.
Rectangle {
    id: root
    property var control
    readonly property bool activeState: !!(control && (control.activeFocus === true || control.visualFocus === true || control.down === true))
    readonly property bool pressedState: !!(control && control.down === true)
    radius: 15
    color: "#E5EAF0"
    border.width: root.activeState ? 2 : 1
    border.color: root.activeState ? "#537F9C" : "#9DAEBE"
    Rectangle { anchors.fill: parent; anchors.margins: 2; radius: parent.radius; color: "transparent"; border.width: 2; border.color: "#B1FFFFFF" }
    Rectangle { x: 6; y: Math.max(0,parent.height-5); width: Math.max(0,parent.width-12); height: 2; color: "#39798FA9" }
}
