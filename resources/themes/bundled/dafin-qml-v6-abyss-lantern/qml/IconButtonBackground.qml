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
    readonly property color baseFill: root.selectedState ? "#5CDFF2" : (root.pressedState ? "#031221" : (root.hoveredState ? "#1E4F65" : "#103349"))
    radius: 15
    color: root.idleGhost ? "transparent" : baseFill
    border.width: root.idleGhost ? 0 : (root.focusedState ? 2 : 1)
    border.color: root.focusedState ? "#68E5FA" : (root.selectedState ? "#68E5FA" : "#2B7088")
    opacity: control && control.enabled === false ? 0.55 : 1.0
    clip: true
    Rectangle {
        anchors.fill: parent; anchors.margins: 1
        radius: root.radius
        color: "transparent"
        visible: !root.idleGhost
        gradient: Gradient {
            GradientStop { position: 0.0; color: root.selectedState ? "#73E3F4" : (root.pressedState ? "#031221" : "#234358") }
            GradientStop { position: 1.0; color: root.selectedState ? "#5CDFF2" : root.pressedState ? "#103349" : "#0D2B3F" }
        }
    }
    Item { anchors.fill: parent; visible: !root.idleGhost
    Rectangle { x: 3; y: 2; width: Math.max(0,parent.width-6); height: 2; color: "#2F7187" }
    }
    Rectangle { x: 3; y: 2; width: Math.max(0,parent.width-6); height: 4; radius: 3; color: "#5A68E5FA" }
    Rectangle { x: Math.max(0,parent.width-14); y: 4; width: 7; height: 7; radius: 4; color: "#55AB92ED" }
}
