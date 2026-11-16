import QtQuick
import Drift

// Original visual-only Dafin skin. Trusted QML; no commands, I/O, or input handlers.
Rectangle {
    id: root
    property var control
    readonly property bool pressedState: !!(control && control.down === true)
    readonly property bool focusedState: !!(control && (control.visualFocus === true || control.activeFocus === true))
    readonly property bool selectedState: !!(control && (control.selected === true || control.active === true || control.checked === true || control.variant === "primary"))
    readonly property bool hoveredState: !!(control && control.hovered === true)
    readonly property bool activeState: selectedState || hoveredState || focusedState
    readonly property bool idleGhost: !root.activeState
    readonly property color baseFill: root.selectedState ? "#187BB8" : (root.pressedState ? "#D6EAF6" : (root.hoveredState ? "#90C6E5" : "#A7D0E9"))
    radius: 12
    color: root.idleGhost ? "transparent" : baseFill
    border.width: root.idleGhost ? 0 : (root.focusedState ? 2 : 1)
    border.color: root.focusedState ? "#168FD1" : (root.selectedState ? "#168FD1" : "#5B9ECC")
    opacity: control && control.enabled === false ? 0.55 : 1.0
    clip: true
    Rectangle {
        anchors.fill: parent; anchors.margins: 1
        radius: root.radius
        color: "transparent"
        visible: !root.idleGhost
        gradient: Gradient {
            GradientStop { position: 0.0; color: root.selectedState ? "#388DC2" : (root.pressedState ? "#D6EAF6" : "#BBDBEE") }
            GradientStop { position: 1.0; color: root.selectedState ? "#187BB8" : root.pressedState ? "#A7D0E9" : "#B3D6EC" }
        }
    }
    Item { anchors.fill: parent; visible: !root.idleGhost
    Rectangle { x: 3; y: 2; width: Math.max(0,parent.width-6); height: Math.max(0,parent.height*0.38); radius: root.radius; color: "#55FFFFFF"; opacity: root.pressedState ? 0.18 : 0.75 }
    }
    Rectangle { x: 3; y: 3; width: Math.max(0,parent.width-6); height: Math.max(0,parent.height*.28); radius: root.radius; color: "#55FFFFFF"; visible: !root.idleGhost }
}
