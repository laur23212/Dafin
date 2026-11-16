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
    readonly property bool idleGhost: false
    readonly property color baseFill: root.selectedState ? "#8E4A36" : (root.pressedState ? "#F7EFE0" : (root.hoveredState ? "#EAD3BE" : "#F4E9D5"))
    radius: 7
    color: root.idleGhost ? "transparent" : baseFill
    border.width: root.idleGhost ? 0 : (root.focusedState ? 2 : 1)
    border.color: root.focusedState ? "#B35E43" : (root.selectedState ? "#B35E43" : "#CDB89A")
    opacity: control && control.enabled === false ? 0.55 : 1.0
    clip: true
    Rectangle {
        anchors.fill: parent; anchors.margins: 1
        radius: root.radius
        color: "transparent"
        visible: !root.idleGhost
        gradient: Gradient {
            GradientStop { position: 0.0; color: root.selectedState ? "#9E6352" : (root.pressedState ? "#F7EFE0" : "#F7EEDF") }
            GradientStop { position: 1.0; color: root.selectedState ? "#8E4A36" : root.pressedState ? "#F4E9D5" : "#F5EAD8" }
        }
    }
    Item { anchors.fill: parent; visible: !root.idleGhost
    Rectangle { x: 2; y: 2; width: Math.max(0,parent.width-4); height: 2; color: "#B35E43"; opacity: 0.38 }
    }
}
