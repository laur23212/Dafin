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
    readonly property color baseFill: root.selectedState ? "#CB0012" : (root.pressedState ? "#0E0F10" : (root.hoveredState ? "#482328" : "#2A2A2C"))
    radius: 0
    color: root.idleGhost ? "transparent" : baseFill
    border.width: root.idleGhost ? 0 : (root.focusedState ? 2 : 1)
    border.color: root.focusedState ? "#E60012" : (root.selectedState ? "#E60012" : "#E8E8E8")
    opacity: control && control.enabled === false ? 0.55 : 1.0
    clip: true
    Rectangle {
        anchors.fill: parent; anchors.margins: 1
        radius: root.radius
        color: "transparent"
        visible: !root.idleGhost
        gradient: Gradient {
            GradientStop { position: 0.0; color: root.selectedState ? "#D22433" : (root.pressedState ? "#0E0F10" : "#3B3B3D") }
            GradientStop { position: 1.0; color: root.selectedState ? "#CB0012" : root.pressedState ? "#2A2A2C" : "#232325" }
        }
    }
    Item { anchors.fill: parent; visible: !root.idleGhost
    Rectangle { x: 3; y: 2; width: Math.max(0,parent.width-6); height: 2; color: "#6C1B23" }
    }
    // Hard-edged streetwear label and micro-serial mark.
    Rectangle { x: 0; y: 0; width: 5; height: parent.height; color: "#E60012" }
    Rectangle { x: 7; y: Math.max(0,parent.height-5); width: Math.max(0,parent.width-14); height: 2; color: "#55FFFFFF" }
}
