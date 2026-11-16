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
    readonly property color baseFill: root.selectedState ? "#365F7C" : (root.pressedState ? "#CBD5E0" : (root.hoveredState ? "#C8D5E0" : "#DEE5ED"))
    radius: 15
    color: root.idleGhost ? "transparent" : baseFill
    border.width: root.idleGhost ? 0 : (root.focusedState ? 2 : 1)
    border.color: root.focusedState ? "#537F9C" : (root.selectedState ? "#537F9C" : "#9DAEBE")
    opacity: control && control.enabled === false ? 0.55 : 1.0
    clip: true
    Rectangle {
        anchors.fill: parent; anchors.margins: 1
        radius: root.radius
        color: "transparent"
        visible: !root.idleGhost
        gradient: Gradient {
            GradientStop { position: 0.0; color: root.selectedState ? "#52758E" : (root.pressedState ? "#CBD5E0" : "#E6EBF1") }
            GradientStop { position: 1.0; color: root.selectedState ? "#365F7C" : root.pressedState ? "#DEE5ED" : "#D9E1EA" }
        }
    }
    Item { anchors.fill: parent; visible: !root.idleGhost
    Rectangle { x: 3; y: 2; width: Math.max(0,parent.width-6); height: 2; color: "#F0F3F7" }
    }
    // Opposing inner bevels mimic extruded soft UI without Qt graphical effects.
    Rectangle { x: 2; y: 2; width: Math.max(0,parent.width-4); height: 2; radius: 1; color: "#D9FFFFFF" }
    Rectangle { x: 2; y: 4; width: 2; height: Math.max(0,parent.height-8); color: "#A7FFFFFF" }
    Rectangle { x: 3; y: Math.max(0,parent.height-4); width: Math.max(0,parent.width-6); height: 2; color: "#648BA0B5" }
    Rectangle { x: Math.max(0,parent.width-4); y: 4; width: 2; height: Math.max(0,parent.height-8); color: "#55869EB1" }
}
