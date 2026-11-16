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
    readonly property bool idleGhost: !!(control && control.variant === "ghost" && !root.activeState)
    readonly property color baseFill: root.selectedState ? "#96351D" : (root.pressedState ? "#D4C9B6" : (root.hoveredState ? "#C7AB94" : "#CDC0AA"))
    radius: 2
    color: root.idleGhost ? "transparent" : baseFill
    border.width: root.idleGhost ? 0 : (root.focusedState ? 2 : 1)
    border.color: root.focusedState ? "#A63D23" : (root.selectedState ? "#A63D23" : "#887967")
    opacity: control && control.enabled === false ? 0.55 : 1.0
    clip: true
    Rectangle {
        anchors.fill: parent; anchors.margins: 1
        radius: root.radius
        color: "transparent"
        visible: !root.idleGhost
        gradient: Gradient {
            GradientStop { position: 0.0; color: root.selectedState ? "#A5513D" : (root.pressedState ? "#D4C9B6" : "#D8CEBE") }
            GradientStop { position: 1.0; color: root.selectedState ? "#96351D" : root.pressedState ? "#CDC0AA" : "#CFC2AD" }
        }
    }
    Item { anchors.fill: parent; visible: !root.idleGhost
    Rectangle { x: 3; y: 2; width: Math.max(0,parent.width-6); height: 2; color: "#E8E3D9" }
    }
    Rectangle { x: 2; y: 2; width: Math.max(0,parent.width-4); height: 2; color: "#A9FFFFFF" }
    Rectangle { x: 2; y: Math.max(0,parent.height-4); width: Math.max(0,parent.width-4); height: 2; color: "#99887662" }
    Rectangle { x: Math.max(0,parent.width-8); y: 4; width: 3; height: Math.max(0,parent.height-8); color: "#B86B2B" }
}
