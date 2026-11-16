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
    readonly property color baseFill: root.selectedState ? "#FFE900" : (root.pressedState ? "#000000" : (root.hoveredState ? "#3A3712" : "#151515"))
    radius: 2
    color: root.idleGhost ? "transparent" : baseFill
    border.width: root.idleGhost ? 0 : (root.focusedState ? 4 : 2)
    border.color: root.focusedState ? "#FFE900" : (root.selectedState ? "#FFE900" : "#FFFFFF")
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
    Rectangle { anchors.fill: parent; anchors.margins: 2; color: "transparent"; border.width: root.focusedState ? 2 : 0; border.color: "#FFE900" }
    }
}
