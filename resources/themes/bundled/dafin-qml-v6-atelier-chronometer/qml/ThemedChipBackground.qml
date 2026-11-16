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
    readonly property color baseFill: root.selectedState ? "#D6B18A" : (root.pressedState ? "#0B1820" : (root.hoveredState ? "#4B5356" : "#30414B"))
    radius: 8
    color: root.idleGhost ? "transparent" : baseFill
    border.width: root.idleGhost ? 0 : (root.focusedState ? 2 : 1)
    border.color: root.focusedState ? "#D9B48D" : (root.selectedState ? "#D9B48D" : "#AB8D6B")
    opacity: control && control.enabled === false ? 0.55 : 1.0
    clip: true
    Rectangle {
        anchors.fill: parent; anchors.margins: 1
        radius: root.radius
        color: "transparent"
        visible: !root.idleGhost
        gradient: Gradient {
            GradientStop { position: 0.0; color: root.selectedState ? "#DCBC9A" : (root.pressedState ? "#0B1820" : "#415059") }
            GradientStop { position: 1.0; color: root.selectedState ? "#D6B18A" : root.pressedState ? "#30414B" : "#273740" }
        }
    }
    Item { anchors.fill: parent; visible: !root.idleGhost
    Rectangle { x: 3; y: 2; width: Math.max(0,parent.width-6); height: 2; color: "#6B6962" }
    }
    Rectangle { anchors.fill: parent; anchors.margins: 3; radius: 6; color: "transparent"; border.width: 1; border.color: "#AAD9B48D" }
    Rectangle { x: Math.max(0,parent.width-14); y: 4; width: 7; height: 1; color: "#C5D9B48D" }
}
