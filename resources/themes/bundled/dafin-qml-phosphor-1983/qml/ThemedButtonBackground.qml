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
    readonly property color baseFill: root.selectedState ? "#8BFFAA" : (root.pressedState ? "#010904" : (root.hoveredState ? "#1E452B" : "#0C2113"))
    radius: 0
    color: root.idleGhost ? "transparent" : baseFill
    border.width: root.idleGhost ? 0 : (root.focusedState ? 2 : 1)
    border.color: root.focusedState ? "#7EFFAA" : (root.selectedState ? "#7EFFAA" : "#2D7250")
    opacity: control && control.enabled === false ? 0.55 : 1.0
    clip: true
    Rectangle {
        anchors.fill: parent; anchors.margins: 1
        radius: root.radius
        color: "transparent"
        visible: !root.idleGhost
        gradient: Gradient {
            GradientStop { position: 0.0; color: root.selectedState ? "#9BFFB6" : (root.pressedState ? "#010904" : "#1F3326") }
            GradientStop { position: 1.0; color: root.selectedState ? "#8BFFAA" : root.pressedState ? "#0C2113" : "#091B0F" }
        }
    }
    Item { anchors.fill: parent; visible: !root.idleGhost
    Rectangle { x: 1; y: 1; width: Math.max(0,parent.width-2); height: 1; color: "#7EFFAA"; opacity: root.activeState ? 0.95 : 0.5 }
    }
    Rectangle { x: 3; width: Math.max(0,parent.width-6); height: 1; y: Math.max(0,parent.height-3); color: "#7EFFAA"; opacity: .4 }
}
