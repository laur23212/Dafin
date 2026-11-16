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
    readonly property color baseFill: root.selectedState ? "#151515" : (root.pressedState ? "#F0F0F0" : (root.hoveredState ? "#C6C6C6" : "#E5E5E5"))
    radius: 1
    color: root.idleGhost ? "transparent" : baseFill
    border.width: root.idleGhost ? 0 : (root.focusedState ? 2 : 1)
    border.color: root.focusedState ? "#222222" : (root.selectedState ? "#222222" : "#A0A0A0")
    opacity: control && control.enabled === false ? 0.55 : 1.0
    clip: true
    Rectangle {
        anchors.fill: parent; anchors.margins: 1
        radius: root.radius
        color: "transparent"
        visible: !root.idleGhost
        gradient: Gradient {
            GradientStop { position: 0.0; color: root.baseFill }
            GradientStop { position: 1.0; color: root.baseFill }
        }
    }
    Item { anchors.fill: parent; visible: !root.idleGhost
    Rectangle { x: 1; y: parent.height - 1; width: Math.max(0,parent.width - 2); height: 1; color: "#A0A0A0"; opacity: 0.62 }
    }
}
