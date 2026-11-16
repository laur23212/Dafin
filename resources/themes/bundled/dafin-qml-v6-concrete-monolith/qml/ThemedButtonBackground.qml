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
    readonly property color baseFill: root.selectedState ? "#22292B" : (root.pressedState ? "#949996" : (root.hoveredState ? "#8F9391" : "#A1A5A2"))
    radius: 0
    color: root.idleGhost ? "transparent" : baseFill
    border.width: root.idleGhost ? 0 : (root.focusedState ? 2 : 1)
    border.color: root.focusedState ? "#303638" : (root.selectedState ? "#303638" : "#323638")
    opacity: control && control.enabled === false ? 0.55 : 1.0
    clip: true
    Rectangle {
        anchors.fill: parent; anchors.margins: 1
        radius: root.radius
        color: "transparent"
        visible: !root.idleGhost
        gradient: Gradient {
            GradientStop { position: 0.0; color: root.selectedState ? "#414749" : (root.pressedState ? "#949996" : "#B7BAB7") }
            GradientStop { position: 1.0; color: root.selectedState ? "#22292B" : root.pressedState ? "#A1A5A2" : "#9EA29F" }
        }
    }
    Item { anchors.fill: parent; visible: !root.idleGhost
    Rectangle { x: 3; y: 2; width: Math.max(0,parent.width-6); height: 2; color: "#D5D6D5" }
    }
    Rectangle { x: 2; y: 2; width: Math.max(0,parent.width-4); height: 2; color: "#9ACCCFCD" }
    Rectangle { x: 0; y: Math.max(0,parent.height-3); width: parent.width; height: 3; color: "#4E313536" }
}
